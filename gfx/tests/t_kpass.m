// Single-draw passes as compute kernels (plan item 217): gfx/tools/dmsh_kernel.py's kernels against Metal's own render pass, pixel by pixel, from the
// same starting contents. The shaders (kpass/*.metal) are written the way the chain's spirv-cross MSL is; the Makefile runs the generator on them
// (t_kpass_*_k.metal). Each case is drawn twice, once by a render pipeline of the stage functions and once by tk_vert + tk_frag_w / tk_frag_rw with
// gfx/nvn/nvn_kpass.h's parameter blocks, and the targets are compared:
//   coverage of edges through pixel centres (counted with additive blending, exact), culling of both windings under both front-face settings,
//   perspective / no-perspective / flat interpolation and the fragment position (to 2e-3), viewport and scissor, blending (alpha, min / max /
//   subtract with the blend colour, a partial write mask), an implicit-LOD sample from a 16x16 target of a 64x64 texture with 7 levels (the
//   quad's gradients pick level 2), vertex formats and an indexed draw with a base vertex, a write-only RG11B10Float target, depth clipping,
//   a vertex behind the eye (w < 0), and a triangle strip's second triangle with culling on; and with each, the samples-passed count against the
//   render passes' visibility counting.
// Metal's rules as found here: edges through a pixel centre count for top and left edges (window coordinates, y down), the clip volume's z is
// closed (every vertex at z = 0 or 1 is drawn), flat inputs take a triangle's first vertex.
// Broken variants: -DDM_TEST_KPASS_STRIP_AS_LISTED (the strip's second triangle with its listed winding): 2 fail; kernels generated with
// DM_TEST_KERNEL_NO_GRAD=1 (implicit-LOD samples left as they are): 1 fail (level 0 sampled); with DM_TEST_KERNEL_TIE=1 (the other tie rule): 14 fail.
#import <Metal/Metal.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../nvn/nvn_kpass.h"

static id<MTLDevice> D; static id<MTLCommandQueue> Q;
static int fail, count;
static void check(const char *name, int ok, const char *detail) { count++; fail += !ok; printf("%-100s %s%s%s\n", name, ok ? "✓" : "✗", ok ? "" : "\n    ", ok ? "" : detail); }

static id<MTLLibrary> lib(const char *path) {
    NSError *e = nil; NSString *src = [NSString stringWithContentsOfFile:@(path) encoding:NSUTF8StringEncoding error:&e];
    if (!src) { printf("[red] no %s\n", path); exit(1); }
    MTLCompileOptions *o = [MTLCompileOptions new]; o.mathMode = MTLMathModeSafe; o.preserveInvariance = YES;
    id<MTLLibrary> l = [D newLibraryWithSource:src options:o error:&e];
    if (!l) { printf("[red] %s: %s\n", path, e.localizedDescription.UTF8String); exit(1); }
    return l;
}
static id<MTLLibrary> L_vs, L_fs[3], K_vs, K_fs[3];   // fs: 0 interpolants (4 outputs), 1 texture, 2 colour

typedef struct {
    const char *name; int fs; MTLPixelFormat fmt; uint32_t tw, th;
    int prim; uint32_t nv; const void *vb; uint32_t vb_len, stride; MTLVertexFormat vf[3]; uint32_t vo[3];
    const void *ib; uint32_t itype, nidx; int32_t base_vertex;
    float vp[6]; int has_sc; uint32_t sc[4];
    MTLCullMode cull; MTLWinding wind; int zclamp;
    int blend; MTLBlendFactor sr, dr, sa, da; MTLBlendOperation orgb, oa; uint32_t mask; float bc[4];
    double tol; int draws;   // tolerance (0: exact); draws: the case is drawn this many times in a row (with vb advanced by vb_len each)
} kcase;

static id<MTLTexture> target(const kcase *c, int i) {
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:c->fmt width:c->tw height:c->th mipmapped:NO];
    d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite; d.storageMode = MTLStorageModeShared;
    id<MTLTexture> t = [D newTextureWithDescriptor:d];
    // the same starting contents for both: a pattern by pixel and target
    size_t bpp = c->fmt == MTLPixelFormatRGBA32Float ? 16 : c->fmt == MTLPixelFormatRGBA16Float ? 8 : 4;
    uint8_t *px = calloc(c->tw * c->th, bpp);
    for (uint32_t y = 0; y < c->th; y++) for (uint32_t x = 0; x < c->tw; x++) {
        float v[4] = {(x % 7) / 8.0f, (y % 5) / 8.0f, ((x + y + i) % 3) / 4.0f, 0.5f};
        uint8_t *p = px + (y * c->tw + x) * bpp;
        if (bpp == 16) memcpy(p, v, 16);
        else if (bpp == 8) for (int k = 0; k < 4; k++) { __fp16 h = (__fp16)v[k]; memcpy(p + 2 * k, &h, 2); }
        else if (c->fmt == MTLPixelFormatRG11B10Float) memset(p, 0, 4);
        else for (int k = 0; k < 4; k++) p[k] = (uint8_t)lrintf(v[k] * 255);
    }
    [t replaceRegion:MTLRegionMake2D(0, 0, c->tw, c->th) mipmapLevel:0 withBytes:px bytesPerRow:c->tw * bpp];
    free(px); return t;
}
static int nout(const kcase *c) { return c->fs == 0 ? 4 : 1; }
static id<MTLTexture> g_tex; static id<MTLSamplerState> g_samp;   // the texture case's
static id<MTLBuffer> argbuf(void) {
    id<MTLBuffer> b = [D newBufferWithLength:16 options:MTLResourceStorageModeShared];
    uint64_t ids[2] = {g_tex.gpuResourceID._impl, g_samp.gpuResourceID._impl}; memcpy(b.contents, ids, 16); return b;
}

static uint64_t g_count_r, g_count_k;   // the samples-passed counts of the last case: the render passes' (visibility counting) and the kernels'
static void draw_render(const kcase *c, __strong id<MTLTexture> *t) {
    MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
    pd.vertexFunction = [L_vs newFunctionWithName:@"main0"]; pd.fragmentFunction = [L_fs[c->fs] newFunctionWithName:@"main0"];
    MTLVertexDescriptor *vd = [MTLVertexDescriptor vertexDescriptor];
    for (int a = 0; a < 3; a++) { vd.attributes[a].format = c->vf[a]; vd.attributes[a].offset = c->vo[a]; vd.attributes[a].bufferIndex = 16; }
    vd.layouts[16].stride = c->stride; pd.vertexDescriptor = vd;
    for (int i = 0; i < nout(c); i++) {
        MTLRenderPipelineColorAttachmentDescriptor *a = pd.colorAttachments[i]; a.pixelFormat = c->fmt;
        a.writeMask = (c->mask & 1 ? MTLColorWriteMaskRed : 0) | (c->mask & 2 ? MTLColorWriteMaskGreen : 0) | (c->mask & 4 ? MTLColorWriteMaskBlue : 0) | (c->mask & 8 ? MTLColorWriteMaskAlpha : 0);
        if (c->blend) { a.blendingEnabled = YES; a.sourceRGBBlendFactor = c->sr; a.destinationRGBBlendFactor = c->dr; a.sourceAlphaBlendFactor = c->sa; a.destinationAlphaBlendFactor = c->da;
                        a.rgbBlendOperation = c->orgb; a.alphaBlendOperation = c->oa; }
    }
    NSError *e = nil; id<MTLRenderPipelineState> ps = [D newRenderPipelineStateWithDescriptor:pd error:&e];
    if (!ps) { printf("[red] render pipeline: %s\n", e.localizedDescription.UTF8String); exit(1); }
    id<MTLCommandBuffer> cb = [Q commandBuffer];
    id<MTLBuffer> vis = [D newBufferWithLength:64 options:MTLResourceStorageModeShared]; memset(vis.contents, 0, 64);
    for (int k = 0; k < (c->draws ? c->draws : 1); k++) {
        MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor]; rp.visibilityResultBuffer = vis;
        for (int i = 0; i < nout(c); i++) { rp.colorAttachments[i].texture = t[i]; rp.colorAttachments[i].loadAction = MTLLoadActionLoad; rp.colorAttachments[i].storeAction = MTLStoreActionStore; }
        id<MTLRenderCommandEncoder> en = [cb renderCommandEncoderWithDescriptor:rp];
        [en setRenderPipelineState:ps]; [en setVisibilityResultMode:MTLVisibilityResultModeCounting offset:8 * k];
        id<MTLBuffer> vb = [D newBufferWithBytes:(const uint8_t *)c->vb + k * c->vb_len length:c->vb_len options:MTLResourceStorageModeShared];
        [en setVertexBuffer:vb offset:0 atIndex:16];
        [en setViewport:(MTLViewport){c->vp[0], c->vp[1], c->vp[2], c->vp[3], c->vp[4], c->vp[5]}];
        if (c->has_sc) [en setScissorRect:(MTLScissorRect){c->sc[0], c->sc[1], c->sc[2], c->sc[3]}];
        [en setCullMode:c->cull]; [en setFrontFacingWinding:c->wind]; [en setDepthClipMode:c->zclamp ? MTLDepthClipModeClamp : MTLDepthClipModeClip];
        [en setBlendColorRed:c->bc[0] green:c->bc[1] blue:c->bc[2] alpha:c->bc[3]];
        if (c->fs == 1) { [en setFragmentBuffer:argbuf() offset:0 atIndex:0]; [en useResource:g_tex usage:MTLResourceUsageRead stages:MTLRenderStageFragment]; }
        MTLPrimitiveType pt = c->prim == 5 ? MTLPrimitiveTypeTriangleStrip : MTLPrimitiveTypeTriangle;
        if (c->ib) { id<MTLBuffer> ib = [D newBufferWithBytes:c->ib length:c->nidx * (c->itype == 1 ? 2 : 4) options:MTLResourceStorageModeShared];
                     [en drawIndexedPrimitives:pt indexCount:c->nidx indexType:c->itype == 1 ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32 indexBuffer:ib indexBufferOffset:0 instanceCount:1 baseVertex:c->base_vertex baseInstance:0]; }
        else [en drawPrimitives:pt vertexStart:0 vertexCount:c->nv];
        [en endEncoding];
    }
    [cb commit]; [cb waitUntilCompleted];
    g_count_r = 0; for (int k = 0; k < 8; k++) g_count_r += ((const uint64_t *)vis.contents)[k];
}
static id<MTLComputePipelineState> cps(id<MTLLibrary> l, NSString *n) {
    NSError *e = nil; id<MTLComputePipelineState> p = [D newComputePipelineStateWithFunction:[l newFunctionWithName:n] error:&e];
    if (!p) { printf("[red] %s: %s\n", n.UTF8String, e.localizedDescription.UTF8String); exit(1); }
    return p;
}
static void draw_kernel(const kcase *c, __strong id<MTLTexture> *t) {
    id<MTLCommandBuffer> cb = [Q commandBuffer];
    id<MTLBuffer> vis = [D newBufferWithLength:8 options:MTLResourceStorageModeShared]; memset(vis.contents, 0, 8);
    for (int k = 0; k < (c->draws ? c->draws : 1); k++) {
        id<MTLBuffer> vb = [D newBufferWithBytes:(const uint8_t *)c->vb + k * c->vb_len length:c->vb_len options:MTLResourceStorageModeShared];
        id<MTLBuffer> ib = c->ib ? [D newBufferWithBytes:c->ib length:c->nidx * (c->itype == 1 ? 2 : 4) options:MTLResourceStorageModeShared] : nil;
        uint32_t n = c->ib ? c->nidx : c->nv;
        id<MTLBuffer> rec = [D newBufferWithLength:6 * KP_TKS * 16 options:MTLResourceStorageModePrivate];
        kp_v v = {0};
        for (int a = 0; a < 3; a++) v.a[a] = (kp_va){vb.gpuAddress, c->vo[a], c->stride, (uint32_t)c->vf[a], 0};
        if (ib) { v.ib = ib.gpuAddress; v.itype = c->itype; v.base_vertex = (uint32_t)c->base_vertex; }
        v.count = n;
        kp_f f = {0};
        memcpy(f.vp, c->vp, 4 * sizeof(float)); f.dr[0] = c->vp[4]; f.dr[1] = c->vp[5];
        if (!kp_rect(&f, c->tw, c->th, c->vp, c->has_sc ? c->sc : NULL)) continue;
        int nt = kp_tris(c->prim, n, (uint32_t (*)[4])f.tri); if (!nt) { printf("[red] %s: no triangles\n", c->name); exit(1); }
        f.ntri = (uint32_t)nt; f.cull = c->cull == MTLCullModeFront ? 1 : c->cull == MTLCullModeBack ? 2 : 0; f.front_cw = c->wind == MTLWindingClockwise; f.zclip = !c->zclamp;
        int rw = 0;
        for (int i = 0; i < nout(c); i++) { kp_blend(f.bl[i], c->blend, c->sr, c->dr, c->sa, c->da, c->orgb, c->oa, c->mask, kp_fmt_kind(c->fmt)); rw |= c->blend || (c->mask & 15) != 15; }
        memcpy(f.bc, c->bc, sizeof f.bc); f.vis = 1;
        id<MTLComputeCommandEncoder> en = [cb computeCommandEncoder];
        [en setComputePipelineState:cps(K_vs, @"tk_vert")];
        [en setBytes:&v length:sizeof v atIndex:KP_TKP]; [en setBuffer:rec offset:0 atIndex:KP_TKR];
        [en useResource:vb usage:MTLResourceUsageRead]; if (ib) [en useResource:ib usage:MTLResourceUsageRead];
        [en dispatchThreads:MTLSizeMake(n, 1, 1) threadsPerThreadgroup:MTLSizeMake(n, 1, 1)];
        [en setComputePipelineState:cps(K_fs[c->fs], rw ? @"tk_frag_rw" : @"tk_frag_w")];
        [en setBytes:&f length:sizeof f atIndex:KP_TKP]; [en setBuffer:rec offset:0 atIndex:KP_TKR];
        for (int i = 0; i < nout(c); i++) [en setTexture:t[i] atIndex:KP_TKO + i];
        [en setBuffer:vis offset:0 atIndex:KP_TKV];
        if (c->fs == 1) { [en setBuffer:argbuf() offset:0 atIndex:0]; [en useResource:g_tex usage:MTLResourceUsageRead]; }
        [en dispatchThreads:MTLSizeMake(f.nq * 4, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
        [en endEncoding];
    }
    [cb commit]; [cb waitUntilCompleted];
    if (cb.error) { printf("[red] %s: %s\n", c->name, cb.error.localizedDescription.UTF8String); exit(1); }
    g_count_k = *(const uint64_t *)vis.contents;
}
static float texel(id<MTLTexture> t, uint32_t x, uint32_t y, int ch) {   // channel ch of the pixel as a float
    uint8_t p[16] = {0}; [t getBytes:p bytesPerRow:t.width * 16 fromRegion:MTLRegionMake2D(x, y, 1, 1) mipmapLevel:0];
    switch (t.pixelFormat) {
    case MTLPixelFormatRGBA32Float: { float f[4]; memcpy(f, p, 16); return f[ch]; }
    case MTLPixelFormatRGBA16Float: { __fp16 h[4]; memcpy(h, p, 8); return (float)h[ch]; }
    case MTLPixelFormatRG11B10Float: { uint32_t w; memcpy(&w, p, 4); uint32_t b = ch == 0 ? w & 0x7ff : ch == 1 ? (w >> 11) & 0x7ff : w >> 22;   // 5-bit exponent, 6 / 6 / 5-bit mantissa
        int mb = ch == 2 ? 5 : 6; uint32_t e = b >> mb, m = b & ((1u << mb) - 1); return ch == 3 ? 1 : e ? ldexpf(1 + m / (float)(1 << mb), (int)e - 15) : ldexpf(m / (float)(1 << mb), -14); }
    default: return p[ch] / 255.0f;
    }
}
static void compare(const kcase *c) {
    id<MTLTexture> a[4], b[4];
    for (int i = 0; i < nout(c); i++) { a[i] = target(c, i); b[i] = target(c, i); }
    draw_render(c, a); draw_kernel(c, b);
    int bad = 0; double worst = 0; char detail[300] = "";
    for (int i = 0; i < nout(c); i++) for (uint32_t y = 0; y < c->th; y++) for (uint32_t x = 0; x < c->tw; x++) for (int ch = 0; ch < 4; ch++) {
        float r = texel(a[i], x, y, ch), k = texel(b[i], x, y, ch); double d = fabs((double)r - k);
        if (d > c->tol && !(isnan(r) && isnan(k))) { if (!bad++) snprintf(detail, sizeof detail, "output %d pixel %u,%u channel %d: render %g, kernel %g", i, x, y, ch, r, k); if (d > worst) worst = d; }
    }
    char name[200], d2[400]; snprintf(name, sizeof name, "%s: the kernels give the render pass's pixels%s", c->name, c->tol ? " (to the tolerance)" : "");
    snprintf(d2, sizeof d2, "%d values differ (worst %g); first: %s", bad, worst, detail);
    check(name, !bad, d2);
    snprintf(name, sizeof name, "... and counts the render passes' samples passed (%llu)", (unsigned long long)g_count_r); snprintf(d2, sizeof d2, "kernels %llu", (unsigned long long)g_count_k);
    check(name, g_count_r == g_count_k, d2);
}

// vertex: position (clip x, y, z, w), colour, uv: 12 floats
#define V(x, y, z, w, r, g, b_, a_, u, v_) x, y, z, w, r, g, b_, a_, u, v_, 0, 1
static kcase base(const char *name, int fs, MTLPixelFormat fmt, uint32_t tw, uint32_t th, int prim, uint32_t nv, const float *vb) {
    kcase c = {name, fs, fmt, tw, th, prim, nv, vb, nv * 48, 48, {MTLVertexFormatFloat4, MTLVertexFormatFloat4, MTLVertexFormatFloat4}, {0, 16, 32}};
    c.vp[2] = tw; c.vp[3] = th; c.vp[5] = 1; c.cull = MTLCullModeNone; c.wind = MTLWindingCounterClockwise; c.mask = 15; return c;
}
static float nx(float px, float w) { return (px / 32.0f - 1) * w; }   // window x on a 64-wide viewport → clip x
static float ny(float py, float w) { return (1 - py / 32.0f) * w; }

int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0);
    D = MTLCreateSystemDefaultDevice(); Q = [D newCommandQueue];
    L_vs = lib("kpass/vs.metal"); L_fs[0] = lib("kpass/fs_i.metal"); L_fs[1] = lib("kpass/fs_t.metal"); L_fs[2] = lib("kpass/fs_c.metal");
    K_vs = lib("t_kpass_vs_k.metal"); K_fs[0] = lib("t_kpass_fs_i_k.metal"); K_fs[1] = lib("t_kpass_fs_t_k.metal"); K_fs[2] = lib("t_kpass_fs_c_k.metal");
    {   // the texture case's: 64x64, 7 levels, each a colour of its own; a sampler with nearest levels
        MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:64 height:64 mipmapped:YES];
        g_tex = [D newTextureWithDescriptor:d];
        for (int l = 0; l < 7; l++) { uint32_t s = 64 >> l, col = 0xff000000u | (uint32_t)(40 * l) | (uint32_t)(255 - 30 * l) << 8 | (uint32_t)(l * 17 % 256) << 16;
            uint32_t *px = malloc(s * s * 4); for (uint32_t i = 0; i < s * s; i++) px[i] = col;
            [g_tex replaceRegion:MTLRegionMake2D(0, 0, s, s) mipmapLevel:l withBytes:px bytesPerRow:s * 4]; free(px); }
        MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new]; sd.minFilter = MTLSamplerMinMagFilterNearest; sd.magFilter = MTLSamplerMinMagFilterNearest;
        sd.mipFilter = MTLSamplerMipFilterNearest; sd.supportArgumentBuffers = YES; g_samp = [D newSamplerStateWithDescriptor:sd];
    }
    // 1. a full-target strip, interpolants and the position
    static const float strip[] = {V(-1, -1, 0.2, 1, 1, 0, 0, 1, 0, 1), V(1, -1, 0.4, 1, 0, 1, 0, 1, 1, 1), V(-1, 1, 0.6, 1, 0, 0, 1, 1, 0, 0), V(1, 1, 0.8, 1, 1, 1, 1, 0, 1, 0)};
    kcase c = base("full-target strip, RGBA32Float, 4 outputs", 0, MTLPixelFormatRGBA32Float, 64, 64, 5, 4, strip); c.tol = 2e-3; compare(&c);
    // 2. perspective: w 1, 2, 4
    float pw[] = {V(nx(4, 1), ny(4, 1), 0.1, 1, 1, 0, 0, 1, 0, 0), V(nx(60, 2), ny(10, 2), 0.5 * 2, 2, 0, 1, 0, 1, 1, 0), V(nx(20, 4), ny(60, 4), 0.9 * 4, 4, 0, 0, 1, 1, 0, 1)};
    c = base("perspective triangle (w 1, 2, 4): perspective, flat and no-perspective interpolants, position", 0, MTLPixelFormatRGBA32Float, 64, 64, 4, 3, pw); c.tol = 2e-3; compare(&c);
    // 3. edges through pixel centres: additive counts, exact. Squares split both ways, a shared vertical edge at a centre, a shared horizontal one
    static float tie[6 * 4 * 12];
    { float sq[4][6][2] = {{{8, 8}, {40, 8}, {40, 40}, {8, 8}, {40, 40}, {8, 40}},          // diagonal through centres (k + 0.5, k + 0.5)
                           {{40, 8}, {56, 8}, {40, 24}, {56, 8}, {56, 24}, {40, 24}},        // the other diagonal
                           {{10.5, 30}, {20.5, 50}, {10.5, 60}, {20.5, 50}, {30.5, 30}, {20.5, 60}},   // edges along x = 20.5
                           {{30, 40.5}, {60, 40.5}, {45, 50.5}, {30, 40.5}, {45, 30.5}, {60, 40.5}}};   // a shared edge along y = 40.5
      for (int s = 0; s < 4; s++) for (int v = 0; v < 6; v++) { float *o = tie + (s * 6 + v) * 12;
          float q[12] = {V(nx(sq[s][v][0], 1), ny(sq[s][v][1], 1), 0.5, 1, 1 / 64.0, 1 / 32.0, 1 / 16.0, 1 / 8.0, 0, 0)}; memcpy(o, q, sizeof q); } }
    c = base("edges through pixel centres: each pixel covered once (additive counts)", 2, MTLPixelFormatRGBA16Float, 64, 64, 4, 6, tie); c.draws = 4;
    c.blend = 1; c.sr = c.dr = c.sa = c.da = MTLBlendFactorOne; c.orgb = c.oa = MTLBlendOperationAdd; compare(&c);
    // 4. culling: a clockwise and a counter-clockwise triangle (as seen on the target), all four settings
    float cw2[] = {V(nx(4, 1), ny(4, 1), 0.5, 1, 1, 0, 0, 1, 0, 0), V(nx(28, 1), ny(4, 1), 0.5, 1, 1, 0, 0, 1, 0, 0), V(nx(4, 1), ny(28, 1), 0.5, 1, 1, 0, 0, 1, 0, 0),
                   V(nx(36, 1), ny(36, 1), 0.5, 1, 0, 1, 0, 1, 0, 0), V(nx(36, 1), ny(60, 1), 0.5, 1, 0, 1, 0, 1, 0, 0), V(nx(60, 1), ny(36, 1), 0.5, 1, 0, 1, 0, 1, 0, 0)};
    const char *cn[4] = {"cull back, front counter-clockwise", "cull back, front clockwise", "cull front, front counter-clockwise", "cull front, front clockwise"};
    for (int k = 0; k < 4; k++) { c = base(cn[k], 2, MTLPixelFormatRGBA8Unorm, 64, 64, 4, 6, cw2); c.cull = k < 2 ? MTLCullModeBack : MTLCullModeFront; c.wind = k & 1 ? MTLWindingClockwise : MTLWindingCounterClockwise; compare(&c); }
    // 5. viewport and scissor
    c = base("viewport (8, 4, 40x50) and scissor (10, 10, 30x30)", 0, MTLPixelFormatRGBA32Float, 64, 64, 5, 4, strip); c.tol = 2e-3;
    c.vp[0] = 8; c.vp[1] = 4; c.vp[2] = 40; c.vp[3] = 50; c.has_sc = 1; c.sc[0] = 10; c.sc[1] = 10; c.sc[2] = 30; c.sc[3] = 30; compare(&c);
    c = base("odd viewport (5, 7, 33x21): the first column and row are odd", 2, MTLPixelFormatRGBA8Unorm, 64, 64, 5, 4, strip); c.tol = 1.01 / 255; c.vp[0] = 5; c.vp[1] = 7; c.vp[2] = 33; c.vp[3] = 21; compare(&c);
    // 6. blending
    float half_a[] = {V(-1, -1, 0.5, 1, 1, 0.2, 0, 0.25, 0, 0), V(1, -1, 0.5, 1, 0, 1, 0.3, 0.5, 0, 0), V(-1, 1, 0.5, 1, 0.1, 0, 1, 0.75, 0, 0), V(1, 1, 0.5, 1, 1, 1, 0, 1, 0, 0)};
    c = base("blending source alpha / one minus source alpha, RGBA8Unorm", 2, MTLPixelFormatRGBA8Unorm, 64, 64, 5, 4, half_a); c.tol = 1.01 / 255;
    c.blend = 1; c.sr = c.sa = MTLBlendFactorSourceAlpha; c.dr = c.da = MTLBlendFactorOneMinusSourceAlpha; c.orgb = c.oa = MTLBlendOperationAdd; compare(&c);
    c = base("blending: max (rgb) and reverse subtract with the blend colour (alpha), write mask r, b; RGBA16Float", 2, MTLPixelFormatRGBA16Float, 64, 64, 5, 4, half_a); c.tol = 2e-3;
    c.blend = 1; c.sr = MTLBlendFactorOne; c.dr = MTLBlendFactorOne; c.sa = MTLBlendFactorBlendColor; c.da = MTLBlendFactorOneMinusBlendAlpha;
    c.orgb = MTLBlendOperationMax; c.oa = MTLBlendOperationReverseSubtract; c.mask = 1 | 4; c.bc[0] = 0.25; c.bc[1] = 0.5; c.bc[2] = 0.75; c.bc[3] = 0.4; compare(&c);
    c = base("no blending, write mask g, a only (read-write kernel), RGBA8Unorm", 2, MTLPixelFormatRGBA8Unorm, 64, 64, 5, 4, half_a); c.tol = 1.01 / 255; c.mask = 2 | 8; compare(&c);
    // 7. an implicit-LOD sample: 16x16 target, uv over the whole 64x64 texture: level 2 by the quad's gradients
    c = base("implicit-LOD sample of a 7-level 64x64 texture into 16x16: level 2 [DM_TEST_KERNEL_NO_GRAD=1: level 0]", 1, MTLPixelFormatRGBA8Unorm, 16, 16, 5, 4, strip); compare(&c);
    // 8. vertex formats and an indexed draw with a base vertex: position Float2, colour UChar4Normalized, uv Half2 (16 bytes a vertex)
    static uint8_t fv[6 * 16]; static const uint16_t idx[6] = {0, 1, 2, 2, 1, 3};
    for (int k = 0; k < 6; k++) { int q = k - 2; float px = q >= 0 ? nx((q & 1) ? 60 : 3, 1) : 0, py = q >= 0 ? ny((q & 2) ? 61 : 2, 1) : 0; uint8_t *o = fv + k * 16;
        memcpy(o, &px, 4); memcpy(o + 4, &py, 4); o[8] = (uint8_t)(k * 40); o[9] = (uint8_t)(255 - k * 30); o[10] = 128; o[11] = 255;
        __fp16 u = (__fp16)((q & 1) * 1.0), v = (__fp16)((q >> 1) * 0.5); memcpy(o + 12, &u, 2); memcpy(o + 14, &v, 2); }
    c = base("vertex formats Float2 / UChar4Normalized / Half2, 16-bit indices, base vertex 2", 0, MTLPixelFormatRGBA32Float, 64, 64, 4, 6, (const float *)fv);
    c.vb_len = sizeof fv; c.stride = 16; c.vf[0] = MTLVertexFormatFloat2; c.vf[1] = MTLVertexFormatUChar4Normalized; c.vf[2] = MTLVertexFormatHalf2; c.vo[0] = 0; c.vo[1] = 8; c.vo[2] = 12;
    c.ib = idx; c.itype = 1; c.nidx = 6; c.base_vertex = 2; c.tol = 2e-3; compare(&c);
    // 9. a write-only RG11B10Float target
    c = base("RG11B10Float target, no blending (write-only kernel)", 2, MTLPixelFormatRG11B10Float, 64, 64, 5, 4, half_a); c.tol = 0.02; compare(&c);
    // 10. depth clipping: z from -0.49 to 1.51 across the target (z = 0 and 1 between pixel centres: on a centre the hardware's rounding decides),
    // and every vertex at z = 0 or at z = 1 (kept: the clip volume is closed)
    static const float zs[] = {V(-1, -1, -0.49, 1, 1, 0, 0, 1, 0, 1), V(1, -1, 0.51, 1, 0, 1, 0, 1, 1, 1), V(-1, 1, 0.51, 1, 0, 0, 1, 1, 0, 0), V(1, 1, 1.51, 1, 1, 1, 1, 0, 1, 0)};
    c = base("depth clipping: z from -0.49 to 1.51", 0, MTLPixelFormatRGBA32Float, 64, 64, 5, 4, zs); c.tol = 2e-3; compare(&c);
    c = base("depth clamp: the same, nothing clipped", 0, MTLPixelFormatRGBA32Float, 64, 64, 5, 4, zs); c.tol = 2e-3; c.zclamp = 1; compare(&c);
    static const float z0[] = {V(-1, -1, 0, 1, 1, 0, 0, 1, 0, 1), V(1, -1, 0, 1, 0, 1, 0, 1, 1, 1), V(-1, 1, 0, 1, 0, 0, 1, 1, 0, 0), V(1, 1, 0, 1, 1, 1, 1, 0, 1, 0)};
    static const float z1[] = {V(-1, -1, 1, 1, 1, 0, 0, 1, 0, 1), V(1, -1, 1, 1, 0, 1, 0, 1, 1, 1), V(-1, 1, 1, 1, 0, 0, 1, 1, 0, 0), V(1, 1, 1, 1, 1, 1, 1, 0, 1, 0)};
    c = base("depth clipping, every vertex at z = 0 (kept)", 2, MTLPixelFormatRGBA8Unorm, 64, 64, 5, 4, z0); compare(&c);
    c = base("depth clipping, every vertex at z = 1 (kept)", 2, MTLPixelFormatRGBA8Unorm, 64, 64, 5, 4, z1); compare(&c);
    // 11. a vertex behind the eye (w < 0)
    float beh[] = {V(nx(10, 1), ny(10, 1), 0.5, 1, 1, 0, 0, 1, 0, 0), V(nx(50, 1), ny(12, 1), 0.5, 1, 0, 1, 0, 1, 1, 0), V(0.3, -0.2, 0.5, -0.5, 0, 0, 1, 1, 0, 1)};
    c = base("a vertex behind the eye (w = -0.5)", 2, MTLPixelFormatRGBA8Unorm, 64, 64, 4, 3, beh); c.tol = 1.01 / 255; compare(&c);
    // 12. a strip's second triangle under culling [-DDM_TEST_KPASS_STRIP_AS_LISTED: its winding flips, it is culled]
    c = base("triangle strip with culling (back, front counter-clockwise) [-DDM_TEST_KPASS_STRIP_AS_LISTED: half missing]", 2, MTLPixelFormatRGBA8Unorm, 64, 64, 5, 4, strip);
    c.cull = MTLCullModeBack; c.wind = MTLWindingCounterClockwise; compare(&c);
    printf("== %d checks, %d failed ==\n", count, fail);
    return fail != 0;
} }
