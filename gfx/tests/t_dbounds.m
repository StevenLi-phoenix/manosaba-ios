// Depth bounds (plan item 64c): nvnCommandBufferSetDepthBounds recorded and replayed with Metal's setDepthTestMinBound:maxBound:, against stored
// depth (the vectors of bayonetta3-mobile's 24-pixel test, MIT, as data, its NaN texel replaced by 0.75: a depth attachment does not keep NaN). A
// 3x2 D32F target holds 0.2 0.3 0.5 / 0.75 0.1 1.0 (one scissored draw a texel); a full-screen draw at 0.5 (depth test ALWAYS, no depth write)
// then lands where the stored depth is within the bounds, inclusive:
//   on [0.2, 0.3]: the first two; on [0.3, 0.2]: none; off: all; on [0, 1]: all; on [0.5, 2] (clamped to 1): 0.5, 0.75, 1.0; on [1.5, 2]: none
// 36 texels. Not given to Metal (-DDM_TEST_DBOUNDS_NOT_SET): 2 fail; bounds that admit nothing drawn as they are (-DDM_TEST_DBOUNDS_EMPTY_DRAWN;
// Metal takes 0 <= min <= max <= 1 only): 2 fail.
#include "../nvn/nvn_mtl.m"
#include "../nvn/hle_nvn.c"
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
static const float k_z[6] = {0.2f, 0.3f, 0.5f, 0.75f, 0.1f, 1.0f};
static prog_t g_db_pz[6], g_db_pred; static NvProg *g_db_keep[7];

static int run(int en, float lo, float hi, int serial) {   // a bitmask of the texels the bounded draw reached
    tex_t color = {.serial = (uint64_t)serial}, depth = {.serial = (uint64_t)serial + 1};
    color.d = (nvn_texdesc){.target = NVN_TEXTURE_TARGET_2D, .format = NVN_FORMAT_RGBA8, .width = 3, .height = 2, .depth = 1, .levels = 1, .samples = 1};
    depth.d = color.d; depth.d.format = NVN_FORMAT_DEPTH32F;
    if (!tex_mtl(&color) || !tex_mtl(&depth)) return -1;
    void *mem = NULL; if (posix_memalign(&mem, getpagesize(), 65536)) abort(); memset(mem, 0, 65536); pool_t pool = {.mem = mem, .size = 65536}; mtl_pool_add(mem, 65536);
    NVNcommandBuffer c; h_nvnCommandBufferInitialize(&c, NULL); h_nvnCommandBufferAddCommandMemory(&c, (const NVNmemoryPool *)&pool, 0, 32768); h_nvnCommandBufferBeginRecording(&c);
    const NVNtexture *tg[] = {(const NVNtexture *)&color};
    h_nvnCommandBufferSetRenderTargets(&c, 1, tg, NULL, (const NVNtexture *)&depth, NULL);
    h_nvnCommandBufferSetViewports(&c, 0, 1, (float[]){0, 0, 3, 2});
    h_nvnCommandBufferClearColor(&c, 0, (float[]){0, 0, 0, 1}, 15); h_nvnCommandBufferClearDepthStencil(&c, 1.0f, 1, 0, 0);
    NVNdepthStencilState ds; h_nvnDepthStencilStateSetDefaults(&ds); h_nvnDepthStencilStateSetDepthTestEnable(&ds, 1); h_nvnDepthStencilStateSetDepthWriteEnable(&ds, 1);
    h_nvnDepthStencilStateSetDepthFunc(&ds, NVN_DEPTH_FUNC_ALWAYS); h_nvnCommandBufferBindDepthStencilState(&c, &ds);
    for (int i = 0; i < 6; i++) { h_nvnCommandBufferSetScissor(&c, i % 3, i / 3, 1, 1); h_nvnCommandBufferBindProgram(&c, (const NVNprogram *)&g_db_pz[i], 3); h_nvnCommandBufferDrawArrays(&c, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3); }
    NVNdepthStencilState rd; h_nvnDepthStencilStateSetDefaults(&rd); h_nvnDepthStencilStateSetDepthTestEnable(&rd, 1); h_nvnDepthStencilStateSetDepthWriteEnable(&rd, 0);
    h_nvnDepthStencilStateSetDepthFunc(&rd, NVN_DEPTH_FUNC_ALWAYS); h_nvnCommandBufferBindDepthStencilState(&c, &rd);
    h_nvnCommandBufferSetDepthBounds(&c, (uint32_t)en, lo, hi); h_nvnCommandBufferSetScissor(&c, 0, 0, 3, 2);
    h_nvnCommandBufferBindProgram(&c, (const NVNprogram *)&g_db_pred, 3); h_nvnCommandBufferDrawArrays(&c, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3);
    h_nvnCommandBufferSetDepthBounds(&c, 0, 0, 1);
    NVNcommandHandle h = h_nvnCommandBufferEndRecording(&c);
    uint64_t hs[1] = {(uint64_t)h}; state_reset(); memset(&R, 0, sizeof R);
    mtl_sub_begin(); mtl_sub_index(hs, 1); mtl_sub_snapshot(hs, 1); mtl_exec_rest(NULL, 0); mtl_exec((const uint8_t *)(uintptr_t)h);
    int done = mtl_wait(mtl_sub_commit(), 5000000000ull) && !mtl_last_commit_error();
    id<MTLBuffer> out = [g_dev newBufferWithLength:24 options:MTLResourceStorageModeShared]; id<MTLCommandBuffer> cb = [g_q commandBuffer]; id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
    [bl copyFromTexture:tex_mtl(&color) sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(3, 2, 1) toBuffer:out destinationOffset:0 destinationBytesPerRow:12 destinationBytesPerImage:24];
    [bl endEncoding]; [cb commit]; [cb waitUntilCompleted];
    int mask = 0; const uint8_t *px = out.contents; for (int i = 0; i < 6; i++) mask |= (px[i * 4] == 255) << i;
    mtl_pool_del(mem); free(mem);
    return done ? mask : -1;
}
static int want(int en, float lo, float hi) { int m = 0; for (int i = 0; i < 6; i++) m |= (!en || (k_z[i] >= lo && k_z[i] <= hi)) << i; return m; }

int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); mtl_init();
    NSMutableString *src = [NSMutableString stringWithString:@"#include <metal_stdlib>\nusing namespace metal;\nconstant float2 P[3] = {float2(-1,-1), float2(3,-1), float2(-1,3)};\n"];
    for (int i = 0; i < 6; i++) [src appendFormat:@"vertex float4 v%d(uint n [[vertex_id]]) { return float4(P[n], %.2f, 1); }\n", i, k_z[i]];
    [src appendString:@"vertex float4 vr(uint n [[vertex_id]]) { return float4(P[n], 0.5, 1); }\nfragment float4 black() { return float4(0, 0, 0, 1); }\nfragment float4 red() { return float4(1, 0, 0, 1); }\n"];
    NSError *e = nil; id<MTLLibrary> lib = [g_dev newLibraryWithSource:src options:nil error:&e];
    if (!lib) { printf("%s\n", e.localizedDescription.UTF8String); return 2; }
    for (int k = 0; k < 7; k++) {
        prog_t *pg = k < 6 ? &g_db_pz[k] : &g_db_pred; *pg = (prog_t){.serial = 2200000 + (uint64_t)k}; NvProg *p = [NvProg new];
        p->sh[0] = [NvShader new]; p->sh[0]->fn = [lib newFunctionWithName:k < 6 ? [NSString stringWithFormat:@"v%d", k] : @"vr"];
        p->sh[1] = [NvShader new]; p->sh[1]->fn = [lib newFunctionWithName:k < 6 ? @"black" : @"red"]; p->sh[1]->stage = 1; p->sh[1]->rf.color_mask = 1;
        serial_put(pg->serial, p); g_db_keep[k] = p;
    }
    struct { int en; float lo, hi; const char *what; } c[6] = {{1, 0.2f, 0.3f, "on [0.2, 0.3]: the texels at 0.2 and 0.3 (inclusive)"}, {1, 0.3f, 0.2f, "on [0.3, 0.2]: none"},
        {0, 0.2f, 0.3f, "off: all"}, {1, 0, 1, "on [0, 1]: all"}, {1, 0.5f, 2, "on [0.5, 2], the top clamped to 1: 0.5, 0.75 and 1.0"}, {1, 1.5f, 2, "on [1.5, 2]: none"}};
    for (int k = 0; k < 6; k++) {
        int got = run(c[k].en, c[k].lo, c[k].hi, 2210000 + 2 * k), w = c[k].en && c[k].lo > 1 ? 0 : want(c[k].en, c[k].lo, c[k].hi);
        char line[160]; snprintf(line, sizeof line, "%s (texels %#04x, want %#04x)", c[k].what, got, w); check(line, got == w);
    }
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
} }
