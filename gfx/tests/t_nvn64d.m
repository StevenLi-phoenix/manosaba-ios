// Rasterizer discard, the scissor / depth range batches and the queue's sync wait (plan item 64d). Recorded through the public NVN calls:
//   rasterizer discard on: a full-screen red draw leaves the 4x4 target clear; off again: red. A vertex stage with a storage binding under it:
//   left out as well (reported once: its writes would need a void vertex variant)
//   SetScissors from 0 with two equal rectangles: viewport 0's scissor is that rectangle; SetDepthRanges from 0: viewport 0's range; a batch whose
//   second entry differs: still the first applied
//   QueueWaitSync: false on a sync never fenced (the SDK), true once fenced
// Discard ignored (-DDM_TEST_DISCARD_DRAWN): 2 fail.
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
static prog_t g_d_red, g_d_store; static NvProg *g_d_keep[2]; static void *g_d_mem; static pool_t g_d_pool; static int g_d_ser = 2300000;

// one submission: clear the 4x4 target black, discard on or off, a red full-screen draw with program p; the first texel's red byte
static int drawn(int discard, prog_t *p) {
    tex_t color = {.serial = (uint64_t)(g_d_ser += 2)}; color.d = (nvn_texdesc){.target = NVN_TEXTURE_TARGET_2D, .format = NVN_FORMAT_RGBA8, .width = 4, .height = 4, .depth = 1, .levels = 1, .samples = 1};
    if (!tex_mtl(&color)) return -1;
    NVNcommandBuffer c; h_nvnCommandBufferInitialize(&c, NULL); h_nvnCommandBufferAddCommandMemory(&c, (const NVNmemoryPool *)&g_d_pool, 0, 16384); h_nvnCommandBufferBeginRecording(&c);
    const NVNtexture *tg[] = {(const NVNtexture *)&color}; h_nvnCommandBufferSetRenderTargets(&c, 1, tg, NULL, NULL, NULL);
    h_nvnCommandBufferSetViewports(&c, 0, 1, (float[]){0, 0, 4, 4}); h_nvnCommandBufferSetScissor(&c, 0, 0, 4, 4); h_nvnCommandBufferClearColor(&c, 0, (float[]){0, 0, 0, 1}, 15);
    h_nvnCommandBufferSetRasterizerDiscard(&c, (uint32_t)discard); h_nvnCommandBufferBindProgram(&c, (const NVNprogram *)p, 3); h_nvnCommandBufferDrawArrays(&c, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3);
    h_nvnCommandBufferSetRasterizerDiscard(&c, 0);
    NVNcommandHandle h = h_nvnCommandBufferEndRecording(&c); uint64_t hs[1] = {(uint64_t)h}; state_reset(); memset(&R, 0, sizeof R);
    mtl_sub_begin(); mtl_sub_index(hs, 1); mtl_sub_snapshot(hs, 1); nvn_exec_rest(NULL, 0); nvn_exec((const uint8_t *)(uintptr_t)h); mtl_wait(mtl_sub_commit(), 5000000000ull);
    id<MTLBuffer> out = [g_dev newBufferWithLength:64 options:MTLResourceStorageModeShared]; id<MTLCommandBuffer> cb = [g_q commandBuffer]; id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
    [bl copyFromTexture:tex_mtl(&color) sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(4, 4, 1) toBuffer:out destinationOffset:0 destinationBytesPerRow:16 destinationBytesPerImage:64];
    [bl endEncoding]; [cb commit]; [cb waitUntilCompleted];
    return ((const uint8_t *)out.contents)[0];
}
static void replay(void (*rec)(NVNcommandBuffer *)) {   // record with rec and replay, leaving the state behind
    NVNcommandBuffer c; h_nvnCommandBufferInitialize(&c, NULL); h_nvnCommandBufferAddCommandMemory(&c, (const NVNmemoryPool *)&g_d_pool, 16384, 16384); h_nvnCommandBufferBeginRecording(&c);
    rec(&c); NVNcommandHandle h = h_nvnCommandBufferEndRecording(&c); uint64_t hs[1] = {(uint64_t)h}; state_reset(); memset(&R, 0, sizeof R);
    mtl_sub_begin(); mtl_sub_index(hs, 1); mtl_sub_snapshot(hs, 1); nvn_exec_rest(NULL, 0); nvn_exec((const uint8_t *)(uintptr_t)h); mtl_wait(mtl_sub_commit(), 5000000000ull);
}
static void rec_batches(NVNcommandBuffer *c) { int sc[8] = {1, 2, 30, 40, 1, 2, 30, 40}; float dr[2] = {0.25f, 0.75f}; h_nvnCommandBufferSetScissors(c, 0, 2, sc); h_nvnCommandBufferSetDepthRanges(c, 0, 1, dr); }
static void rec_mixed(NVNcommandBuffer *c) { int sc[8] = {3, 4, 50, 60, 9, 9, 9, 9}; h_nvnCommandBufferSetScissors(c, 0, 2, sc); }

int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); mtl_init();
    if (posix_memalign(&g_d_mem, getpagesize(), 65536)) abort(); memset(g_d_mem, 0, 65536); g_d_pool = (pool_t){.mem = g_d_mem, .size = 65536}; mtl_pool_add(g_d_mem, 65536);
    NSError *e = nil; id<MTLLibrary> lib = [g_dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\nconstant float2 P[3] = {float2(-1,-1), float2(3,-1), float2(-1,3)};\n"
        "vertex float4 v(uint n [[vertex_id]]) { return float4(P[n], 0.5, 1); }\nfragment float4 red() { return float4(1, 0, 0, 1); }\n" options:nil error:&e];
    if (!lib) { printf("%s\n", e.localizedDescription.UTF8String); return 2; }
    for (int k = 0; k < 2; k++) {
        prog_t *pg = k ? &g_d_store : &g_d_red; *pg = (prog_t){.serial = 2290000 + (uint64_t)k}; NvProg *p = [NvProg new];
        p->sh[0] = [NvShader new]; p->sh[0]->fn = [lib newFunctionWithName:@"v"]; snprintf(p->sh[0]->key, sizeof p->sh[0]->key, "%s", k ? "storevs" : "plainvs");
        if (k) { p->sh[0]->rf.nb = 1; p->sh[0]->rf.b[0] = (nvn_rbind){.kind = NVN_RB_SBUFP, .reg = 0}; }   // a storage binding: the vertex stage may write memory
        p->sh[1] = [NvShader new]; p->sh[1]->fn = [lib newFunctionWithName:@"red"]; p->sh[1]->stage = 1; p->sh[1]->rf.color_mask = 1;
        serial_put(pg->serial, p); g_d_keep[k] = p;
    }
    int on = drawn(1, &g_d_red), off = drawn(0, &g_d_red);
    check("rasterizer discard on: the red draw leaves the target clear; off again: red", on == 0 && off == 255);
    check("... a vertex stage with a storage binding under it: left out as well", drawn(1, &g_d_store) == 0);
    replay(rec_batches);
    check("SetScissors from 0, two equal rectangles: viewport 0's scissor; SetDepthRanges: its range", R.has_sc && R.sc.x == 1 && R.sc.y == 2 && R.sc.w == 30 && R.sc.h == 40 && S.dr[0] == 0.25f && S.dr[1] == 0.75f);
    replay(rec_mixed);
    check("... a batch whose second rectangle differs: the first applied", R.has_sc && R.sc.x == 3 && R.sc.y == 4 && R.sc.w == 50 && R.sc.h == 60);
    NVNsync s; h_nvnSyncInitialize(&s, NULL); NVNqueue q; memset(&q, 0, sizeof q);
    int before = h_nvnQueueWaitSync(&q, &s); h_nvnQueueFenceSync(&q, &s, NVN_SYNC_CONDITION_ALL_GPU_COMMANDS_COMPLETE, 0); int after = h_nvnQueueWaitSync(&q, &s);
    check("QueueWaitSync: false on a sync never fenced, true once fenced", !before && after);
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
} }
