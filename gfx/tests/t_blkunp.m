// BC textures drawn through a block view on a GPU without BC (plan item 62c3; DM_TEST_NO_BC=1), as Tears of the Kingdom's encoders do: a full-screen
// draw through an RGBA16UI view writes one 8-byte block per texel into an auxiliary texture, decoded into the texture after the pass:
//   an 8x8 BC4 texture (kept as R8): a block with endpoints 200 / 100 and every index 0 → every texel 200
//   an 8x8 RGB_DXT1 texture (kept as ASTC 4x4): a block red / blue, every index 0 → each of the four ASTC blocks the C transcoder's for that block
//   the BC4 texture drawn again with endpoints 50 / 100 → every texel 50 (the auxiliary texture kept, decoded again)
//   an 8x8x4 3D BC4 texture (Tears of the Kingdom's encoders write 3D ones) drawn at depth plane 2 → that plane every texel 200 (bcd3)
// Left out, as before (-DDM_TEST_BLKUNP_OFF): 4 fail; every plane decoded into plane 0 (-DDM_TEST_BLKUNP_3D_PLANE0): 1 fails.
#include "../nvn/nvn_mtl.m"
#include "../nvn/hle_nvn.c"
#include "../tex/nvn_bc1astc.h"
const dm_game_profile dm_game = { .id = "test", .nvn_api_major = 55, .nvn_api_minor = 15, .nvn_c2 = {0x168, 128, 0x568, 128, 0x120, 8}, .nvn_depth_mode = 1 };
uint64_t g_t0_ns;
void dm_touch_screen(int w, int h) { (void)w; (void)h; }
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int n, fail;
static void check(const char *what, int ok) { n++; fail += !ok; printf("%-100s %s\n", what, ok ? "✓" : "✗"); }
static pool_t g_u_pool; static void *g_u_mem; static NvProg *g_u_keep[4];
static prog_t mkprog(id<MTLLibrary> lib, const char *frag, uint64_t serial, int k) {
    prog_t pg = {.serial = serial}; NvProg *p = [NvProg new];
    p->sh[0] = [NvShader new]; p->sh[0]->fn = [lib newFunctionWithName:@"v"]; p->sh[1] = [NvShader new]; p->sh[1]->fn = [lib newFunctionWithName:@(frag)]; p->sh[1]->stage = 1; p->sh[1]->rf.color_mask = 1;
    serial_put(serial, p); g_u_keep[k] = p; return pg;
}
static int g_u_layer = -1;   // a view of this layer / depth plane (3D)
static void draw_blocks(tex_t *t, prog_t *pg, ptrdiff_t off) {   // one submission: t through an RGBA16UI view, a full-screen draw of 2x2 block texels
    NVNcommandBuffer c; h_nvnCommandBufferInitialize(&c, NULL); h_nvnCommandBufferAddCommandMemory(&c, (const NVNmemoryPool *)&g_u_pool, off, 16384); h_nvnCommandBufferBeginRecording(&c);
    NVNtextureView vw; h_nvnTextureViewSetDefaults(&vw); h_nvnTextureViewSetFormat(&vw, NVN_FORMAT_RGBA16UI); if (g_u_layer >= 0) h_nvnTextureViewSetLayers(&vw, g_u_layer, 1);
    const NVNtexture *tg[] = {(const NVNtexture *)t}; const NVNtextureView *tv[] = {&vw}; h_nvnCommandBufferSetRenderTargets(&c, 1, tg, tv, NULL, NULL);
    h_nvnCommandBufferSetViewports(&c, 0, 1, (float[]){0, 0, 2, 2}); h_nvnCommandBufferSetScissor(&c, 0, 0, 2, 2);
    h_nvnCommandBufferBindProgram(&c, (const NVNprogram *)pg, 3); h_nvnCommandBufferDrawArrays(&c, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3);
    NVNcommandHandle h = h_nvnCommandBufferEndRecording(&c); uint64_t hs[1] = {(uint64_t)h}; state_reset(); memset(&R, 0, sizeof R);
    mtl_sub_begin(); mtl_sub_index(hs, 1); mtl_sub_snapshot(hs, 1); mtl_exec_rest(NULL, 0); mtl_exec((const uint8_t *)(uintptr_t)h); mtl_wait(mtl_sub_commit(), 5000000000ull);
}
static void read_back(id<MTLTexture> mt, void *out, size_t row, size_t bytes, MTLSize sz) {
    id<MTLBuffer> b = [g_dev newBufferWithLength:bytes options:MTLResourceStorageModeShared]; id<MTLCommandBuffer> cb = [g_q commandBuffer]; id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
    [bl copyFromTexture:mt sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:sz toBuffer:b destinationOffset:0 destinationBytesPerRow:row destinationBytesPerImage:bytes];
    [bl endEncoding]; [cb commit]; [cb waitUntilCompleted]; memcpy(out, b.contents, bytes);
}
static int all8(const uint8_t *p, int k, uint8_t v) { for (int i = 0; i < k; i++) if (p[i] != v) return 0; return 1; }

int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); setenv("DM_TEST_NO_BC", "1", 1); mtl_init();
    if (posix_memalign(&g_u_mem, getpagesize(), 1 << 16)) abort(); memset(g_u_mem, 0, 1 << 16); g_u_pool = (pool_t){.mem = g_u_mem, .size = 1 << 16}; mtl_pool_add(g_u_mem, 1 << 16);
    NSError *e = nil; id<MTLLibrary> lib = [g_dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\nconstant float2 P[3] = {float2(-1,-1), float2(3,-1), float2(-1,3)};\n"
        "vertex float4 v(uint n [[vertex_id]]) { return float4(P[n], 0.5, 1); }\n"
        "fragment uint4 bc4a() { return uint4(200 | (100 << 8), 0, 0, 0); }\n"            // BC4: a0 200, a1 100, every index 0
        "fragment uint4 bc4b() { return uint4(50 | (100 << 8), 0, 0, 0); }\n"
        "fragment uint4 bc1r() { return uint4(0xF800, 0x001F, 0, 0); }\n" options:nil error:&e];   // BC1: c0 red, c1 blue (four colours), every index 0
    if (!lib) { printf("%s\n", e.localizedDescription.UTF8String); return 2; }
    prog_t p4a = mkprog(lib, "bc4a", 2700001, 0), p4b = mkprog(lib, "bc4b", 2700002, 1), p1 = mkprog(lib, "bc1r", 2700003, 2);
    tex_t bc4 = {.magic = MAGIC('T', 'E', 'X', 0), .serial = 2700010}; bc4.d = (nvn_texdesc){.target = NVN_TEXTURE_TARGET_2D, .format = NVN_FORMAT_RGTC1_UNORM, .width = 8, .height = 8, .depth = 1, .levels = 1, .samples = 1};
    tex_t bc1 = bc4; bc1.serial = 2700011; bc1.d.format = NVN_FORMAT_RGB_DXT1;
    id<MTLTexture> m4 = tex_mtl(&bc4), m1 = tex_mtl(&bc1);
    if (!m4 || !m1) { printf("no textures\n"); return 2; }
    printf("BC4 kept as Metal %lu, BC1 as Metal %lu\n", (unsigned long)m4.pixelFormat, (unsigned long)m1.pixelFormat);
    uint8_t px[64];
    draw_blocks(&bc4, &p4a, 0); read_back(tex_mtl(&bc4), px, 8, 64, MTLSizeMake(8, 8, 1));
    check("BC4 drawn through an RGBA16UI view, endpoints 200 / 100, index 0: every texel 200", all8(px, 64, 200));
    uint8_t blk[8] = {0x00, 0xF8, 0x1F, 0x00, 0, 0, 0, 0}, want[16], got[64];
    nvn_bc1_astc(blk, want, 0);
    draw_blocks(&bc1, &p1, 16384); read_back(tex_mtl(&bc1), got, 32, 64, MTLSizeMake(8, 8, 1));
    int same = 1; for (int b = 0; b < 4; b++) same &= !memcmp(got + 16 * b, want, 16);
    check("RGB_DXT1 kept as ASTC, a red / blue block: each ASTC block the C transcoder's", same);
    draw_blocks(&bc4, &p4b, 32768); read_back(tex_mtl(&bc4), px, 8, 64, MTLSizeMake(8, 8, 1));
    check("... the BC4 drawn again, endpoints 50 / 100: every texel 50", all8(px, 64, 50));
    tex_t v3 = bc4; v3.serial = 2700012; v3.d.target = NVN_TEXTURE_TARGET_3D; v3.d.depth = 4;
    id<MTLTexture> m3 = tex_mtl(&v3); g_u_layer = 2; draw_blocks(&v3, &p4a, 49152); g_u_layer = -1;
    uint8_t pl[64] = {0};
    if (m3) { id<MTLBuffer> b = [g_dev newBufferWithLength:64 options:MTLResourceStorageModeShared]; id<MTLCommandBuffer> cb = [g_q commandBuffer]; id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
              [bl copyFromTexture:tex_mtl(&v3) sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 2) sourceSize:MTLSizeMake(8, 8, 1) toBuffer:b destinationOffset:0 destinationBytesPerRow:8 destinationBytesPerImage:64];
              [bl endEncoding]; [cb commit]; [cb waitUntilCompleted]; memcpy(pl, b.contents, 64); }
    check("a 3D BC4 texture drawn at depth plane 2: that plane every texel 200", m3 && all8(pl, 64, 200));
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
} }
