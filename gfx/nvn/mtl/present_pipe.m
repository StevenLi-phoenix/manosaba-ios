// gfx/nvn/mtl/present_pipe.m: The present pipeline and its encoding, a translation unit of the Metal backend of its own (plan item 264; what others use of it: mtl/mtl.h)
#include "mtl.h"
_Atomic uint64_t g_present_stall_ns;   // For unit tests: an extra pause before each present on the present thread (simulating a drawable that stays unavailable)
#if !DM_NVN_M3
CAMetalLayer *g_present_layer_resident;
#endif
void mtl_present_test_stall(uint64_t ns) { g_present_stall_ns = ns; }
void present_pipeline(MTLPixelFormat format) {
    if (!g_blit_pso || g_blit_fmt != format) {
        static const char *src_msl =
            "#include <metal_stdlib>\nusing namespace metal;\n"
            "struct V { float4 p [[position]]; float2 uv; };\n"
            "vertex V vs(uint id [[vertex_id]]) { float2 uv = float2((id << 1) & 2, id & 2); V o; o.p = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); o.uv = uv; return o; }\n"
            "struct Crop { float4 rect; float4 edge; };\n"
            "fragment float4 fs(V in [[stage_in]], texture2d<float> t [[texture(0)]], sampler s [[sampler(0)]], constant Crop& c [[buffer(0)]]) { return t.sample(s, clamp(in.uv * c.rect.zw + c.rect.xy, c.edge.xy, c.edge.zw)); }\n";
        NSError *e = nil;
        id<MTLLibrary> lib = [g_dev newLibraryWithSource:@(src_msl) options:nil error:&e];
        if (!lib) MDIE("present shader compile failed %s", e.localizedDescription.UTF8String);
        MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
        pd.vertexFunction = [lib newFunctionWithName:@"vs"]; pd.fragmentFunction = [lib newFunctionWithName:@"fs"];
        pd.colorAttachments[0].pixelFormat = format;
        g_blit_pso = [g_dev newRenderPipelineStateWithDescriptor:pd error:&e];
        if (!g_blit_pso) MDIE("present pipeline failed %s", e.localizedDescription.UTF8String);
        g_blit_fmt = format;
        MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new]; sd.minFilter = sd.magFilter = MTLSamplerMinMagFilterLinear; sd.supportArgumentBuffers = YES;
        g_blit_samp = [g_dev newSamplerStateWithDescriptor:sd];
    }
}
// Source for presentation sampling: when the swapchain texture has an sRGB format (the game created the swapchain as RGBA8_SRGB), sample through its non-sRGB view so the screen shows the stored bytes, as the Switch's scan-out does;
// sampling it directly decodes to linear first, and the present layer (BGRA8Unorm) does not encode again, so the whole picture is too dark (public repository #36). Screenshots read the raw bytes and cannot see this.
// Views are cached per source texture, 4 of them, replacing the least recently used: presentation runs on its own queue and cannot use the deferred release of the main submission; at most 3 presents are in flight, so the replaced one is already done
id<MTLTexture> present_src(id<MTLTexture> src) {
    MTLPixelFormat raw = MTLPixelFormatInvalid;
    for (size_t i = 0; i < sizeof g_pfv_srgb / sizeof *g_pfv_srgb; i++) if (src.pixelFormat == g_pfv_srgb[i].b) raw = g_pfv_srgb[i].a;
    if (raw == MTLPixelFormatInvalid) return src;
    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER; static id<MTLTexture> cs[4], cv[4]; static uint64_t used[4], tick;
    pthread_mutex_lock(&mu);
    int k = -1, old = 0;
    for (int i = 0; i < 4; i++) { if (cs[i] == src) k = i; if (used[i] < used[old]) old = i; }
    if (k < 0) { k = old; cs[k] = src; cv[k] = [src newTextureViewWithPixelFormat:raw];
        if (!cv[k]) RED_ONCE(50, "present: cannot create a non-sRGB view of the sRGB swapchain texture (format %lu), sampling it directly (the picture will be darker, first case)", (unsigned long)src.pixelFormat);
        else MLOG("present: swapchain texture is sRGB format %lu, sampled through a view of format %lu", (unsigned long)src.pixelFormat, (unsigned long)raw); }
    used[k] = ++tick; id<MTLTexture> v = cv[k] ?: src;
    pthread_mutex_unlock(&mu);
    return v;
}
// Present samples only the valid source rectangle declared by NVN; kept separate from the raw swapchain readback screenshot. Edges are clamped to the texel centers of the valid area, so bilinear does not read the black texels outside.
int present_encode(id<MTLCommandBuffer> cb, id<MTLTexture> src0, id<MTLTexture> dst, const int crop[4]) {
    id<MTLTexture> src = present_src(src0);
    NSUInteger cx = 0, cy = 0, cw = src.width, ch = src.height;
    if (crop && (crop[2] || crop[3])) {
        if (crop[0] < 0 || crop[1] < 0 || crop[2] <= 0 || crop[3] <= 0 ||
            (uint64_t)crop[0] + crop[2] > src.width || (uint64_t)crop[1] + crop[3] > src.height) {
            MLOG("[red] present crop out of bounds: %d,%d %dx%d / source texture %lux%lu", crop[0], crop[1], crop[2], crop[3], (unsigned long)src.width, (unsigned long)src.height); return 0;
        }
        cx = crop[0]; cy = crop[1]; cw = crop[2]; ch = crop[3];
    }
    static uint64_t last_crop[8]; uint64_t meta[8] = {src.width, src.height, cx, cy, cw, ch, dst.width, dst.height};
    if (memcmp(meta, last_crop, sizeof meta)) { memcpy(last_crop, meta, sizeof meta);
        MLOG("present crop: source texture %lux%lu, valid %lu,%lu %lux%lu → target %lux%lu", (unsigned long)src.width, (unsigned long)src.height,
             (unsigned long)cx, (unsigned long)cy, (unsigned long)cw, (unsigned long)ch, (unsigned long)dst.width, (unsigned long)dst.height); }
    present_pipeline(dst.pixelFormat);
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    // Keep the game picture's aspect ratio and center it, leaving black bars in the rest (no stretching when the screen ratio differs from the game's, e.g. iPhone about 19.5:9 versus the game's 16:9);
    // when the ratio difference is < 1% (the remainder from the game width being a multiple of 8 in the full-screen setup, e.g. 1952x900 versus 2868x1320 differs by 0.17%) fill directly, leaving no few-pixel border
    double dw = dst.width, dh = dst.height;
    MTLViewport vp = present_vp(dw, dh, cw, ch);
    int full = vp.originX == 0 && vp.originY == 0 && vp.width == dw && vp.height == dh;
    static double last_vp[4];   // Log one line when the viewport changes (used by the real-device screen-level acceptance screen_check.py to check the aspect ratio)
    if (memcmp(last_vp, &vp, sizeof last_vp)) { memcpy(last_vp, &vp, sizeof last_vp);
        MLOG("present viewport %.0f,%.0f %.0fx%.0f / canvas %.0fx%.0f (game picture %lux%lu)", vp.originX, vp.originY, vp.width, vp.height, dw, dh, (unsigned long)src.width, (unsigned long)src.height); }
    rp.colorAttachments[0].texture = dst; rp.colorAttachments[0].loadAction = full ? MTLLoadActionDontCare : MTLLoadActionClear;
    rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> en = [cb renderCommandEncoderWithDescriptor:rp];
    [en setViewport:vp];
    [en setRenderPipelineState:g_blit_pso]; [en setFragmentTexture:src atIndex:0]; [en setFragmentSamplerState:g_blit_samp atIndex:0];
    float uv[8] = {(float)cx / src.width, (float)cy / src.height, (float)cw / src.width, (float)ch / src.height,
                   (cx + 0.5f) / src.width, (cy + 0.5f) / src.height, (cx + cw - 0.5f) / src.width, (cy + ch - 0.5f) / src.height};
    [en setFragmentBytes:uv length:sizeof uv atIndex:0];
    [en drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [en endEncoding];
    return 1;
}
// The wide picture (plan item 154, host/aspect.c): the game widened its cameras and the front end shaped the target to the wide ratio, so the
// picture, which holds the wider view squeezed into the game's canvas, is stretched over the whole target. Any thread (the front end's)
static _Atomic int g_present_stretch;
void mtl_present_stretch(int on) {
    int v = on != 0; if (atomic_exchange(&g_present_stretch, v) != v) MLOG("present: the game picture %s", v ? "stretched over the target (the wide picture)" : "at its own aspect ratio");
}
// Viewport of the game picture (cw x ch) on a dw x dh target: its aspect ratio kept and centered; a ratio difference under 1% fills the target.
// With the wide picture on (mtl_present_stretch) the whole target.
// The size is rounded (and held to the target) before it is centred (plan item 155, after Touchable 5cab3ac, MIT): floor((dh - ch * s) / 2) with
// ch * s a hair over dh (contracted to a fused multiply-add, or rounded so) gave an origin of -1 and a picture one pixel off the target. Unit test
// gfx/tests/t_present_vp.m
MTLViewport present_vp(double dw, double dh, double cw, double ch) {
#ifndef DM_TEST_STRETCH_IGNORED   // negative control of t_present_vp only: the aspect ratio kept with the wide picture on
    if (atomic_load(&g_present_stretch)) return (MTLViewport){0, 0, dw, dh, 0, 1};
#endif
    double s = fmin(dw / cw, dh / ch);
    if (fabs((dw / dh) / (cw / ch) - 1) < 0.01) return (MTLViewport){0, 0, dw, dh, 0, 1};
#ifndef DM_TEST_PRESENT_VP_OLD   // negative control of t_present_vp only: centred before rounding, as before
    double w = fmin(round(cw * s), dw), h = fmin(round(ch * s), dh);
    return (MTLViewport){floor((dw - w) / 2), floor((dh - h) / 2), w, h, 0, 1};
#else
    return (MTLViewport){floor((dw - cw * s) / 2), floor((dh - ch * s) / 2), round(cw * s), round(ch * s), 0, 1};
#endif
}
#pragma clang attribute pop
