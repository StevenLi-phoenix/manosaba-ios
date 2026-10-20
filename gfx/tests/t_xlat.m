// The translation thread (plan item 61, DM_NVN_XLAT=1), through the public NVN calls with the thread held where a case needs it:
//   a submission with a FenceSync record returns before its replay: the sync holds the submission's token, a wait of 0 times out (not FAILED: it
//   was fenced), and once the thread goes on the wait is satisfied and the sync holds a Metal fence; a QueueFenceSync taken meanwhile the same
//   a deferred bind whose handles the game writes over after the submit call: the replay binds the submit call's handles
//   a uniform range rewritten after the submit call: the replay's snapshot holds the submit call's bytes
//   a range that a buffer copy of an earlier submission writes, both submitted before that one ran: the later replay takes no copy of it (the bytes
//   captured predate the write; read on the GPU's timeline), even when the copy has completed by then
//   ReadTexels after a held submission that clears the texture: waits for the queue, reads the clear
// FenceSync records not fenced at the submit (-DDM_TEST_XLAT_NO_TOKEN): 2 fail; nothing captured, read at the replay (-DDM_TEST_XLAT_NO_CAPTURE): 2
// fail; the capture's bytes taken when the earlier write has completed (-DDM_TEST_XLAT_CAP_STALE): 1 fails; nothing waits for the queue
// (-DDM_TEST_XLAT_NO_DRAIN): 1 fails.
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
static pool_t g_x_pool; static uint8_t *g_x_mem; static NVNqueue g_x_q;
static NVNcommandBuffer g_x_cb;
static void xrec(ptrdiff_t off) { h_nvnCommandBufferInitialize(&g_x_cb, NULL); h_nvnCommandBufferAddCommandMemory(&g_x_cb, (const NVNmemoryPool *)&g_x_pool, off, 8192); h_nvnCommandBufferBeginRecording(&g_x_cb); }
static void submit(NVNcommandHandle h) { h_nvnQueueSubmitCommands(&g_x_q, 1, &h); }
static void idle(void) { for (int i = 0; i < 5000 && nvn_xlat_pending(); i++) usleep(1000); }   // the queue run (not x_drain: -DDM_TEST_XLAT_NO_DRAIN)
static void *release_later(void *a) { (void)a; usleep(100000); nvn_xlat_hold(0); return NULL; }

int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); setenv("DM_NVN_XLAT", "1", 1); mtl_init();
    if (posix_memalign((void **)&g_x_mem, getpagesize(), 1 << 18)) abort(); memset(g_x_mem, 0, 1 << 18);
    g_x_pool = (pool_t){.mem = g_x_mem, .size = 1 << 18}; mtl_pool_add(g_x_mem, 1 << 18);
    memset(&g_x_q, 0, sizeof g_x_q);
    const int fs = NVN_SHADER_STAGE_FRAGMENT, vs = NVN_SHADER_STAGE_VERTEX;

    // FenceSync record and QueueFenceSync with the thread held
    NVNsync s1, s2; h_nvnSyncInitialize(&s1, NULL); h_nvnSyncInitialize(&s2, NULL);
    xrec(0); h_nvnCommandBufferFenceSync(&g_x_cb, &s1, NVN_SYNC_CONDITION_ALL_GPU_COMMANDS_COMPLETE, 0); NVNcommandHandle hf = h_nvnCommandBufferEndRecording(&g_x_cb);
    nvn_xlat_hold(1); submit(hf); h_nvnQueueFenceSync(&g_x_q, &s2, NVN_SYNC_CONDITION_ALL_GPU_COMMANDS_COMPLETE, 0);
    uint64_t v1 = 0, v2 = 0; int f1 = sync_fence_load((const sync_t *)&s1, &v1), f2 = sync_fence_load((const sync_t *)&s2, &v2);
    int w1 = h_nvnSyncWait(&s1, 0), w2 = h_nvnSyncWait(&s2, 0);
    check("held: the submit returned (1 job queued), both syncs hold its token, waits of 0 time out",
          nvn_xlat_pending() == 1 && f1 && (v1 & SYNC_TOKEN) && f2 && (v2 & SYNC_TOKEN) && w1 == NVN_SYNC_WAIT_RESULT_TIMEOUT_EXPIRED && w2 == NVN_SYNC_WAIT_RESULT_TIMEOUT_EXPIRED);
    nvn_xlat_hold(0);
    int r1 = h_nvnSyncWait(&s1, 1000000000ull), r2 = h_nvnSyncWait(&s2, 1000000000ull); idle();
    sync_fence_load((const sync_t *)&s1, &v1);
    check("going on: both waits satisfied, the record's sync now a Metal fence",
          (r1 == NVN_SYNC_WAIT_RESULT_CONDITION_SATISFIED || r1 == NVN_SYNC_WAIT_RESULT_ALREADY_SIGNALED) && (r2 == NVN_SYNC_WAIT_RESULT_CONDITION_SATISFIED || r2 == NVN_SYNC_WAIT_RESULT_ALREADY_SIGNALED) && !(v1 & SYNC_TOKEN) && v1);

    // deferred handles written over after the submit call
    static uint64_t two[2] = {0x30000a, 0x30000b};
    xrec(8192); h_nvnCommandBufferBindTexturesDeferred(&g_x_cb, fs, 0, 2, (const NVNtextureHandle *)two); NVNcommandHandle hd = h_nvnCommandBufferEndRecording(&g_x_cb);
    state_reset(); nvn_xlat_hold(1); submit(hd); two[0] = 0x30000c; two[1] = 0x30000d; nvn_xlat_hold(0); idle();
    check("a deferred bind, its handles written over after the submit call: the replay binds the call's", S.tex[fs][0] == 0x30000a && S.tex[fs][1] == 0x30000b);

    // uniform bytes rewritten after the submit call
    uint64_t ua = (uint64_t)(uintptr_t)(g_x_mem + 0x10000), was = 0x1111111111111111ull, now = 0x2222222222222222ull, got = 0;
    memcpy(g_x_mem + 0x10000, &was, 8);
    xrec(16384); h_nvnCommandBufferBindUniformBuffer(&g_x_cb, vs, 0, ua, 256); NVNcommandHandle hu = h_nvnCommandBufferEndRecording(&g_x_cb);
    mtl_test_us_watch(ua); nvn_xlat_hold(1); submit(hu); memcpy(g_x_mem + 0x10000, &now, 8); nvn_xlat_hold(0); idle();
    int seen = mtl_test_us_seen(&got);
    check("a uniform range rewritten after the submit call: the replay's snapshot holds the call's bytes", seen == 1 && got == was);

    // an earlier submission's buffer copy writes the range; both submitted before it ran
    uint64_t src = (uint64_t)(uintptr_t)(g_x_mem + 0x20000); memcpy(g_x_mem + 0x20000, &now, 8); memcpy(g_x_mem + 0x10000, &was, 8);
    xrec(24576); h_nvnCommandBufferCopyBufferToBuffer(&g_x_cb, src, ua, 256, 0); NVNcommandHandle hc = h_nvnCommandBufferEndRecording(&g_x_cb);
    xrec(32768); h_nvnCommandBufferBindUniformBuffer(&g_x_cb, vs, 0, ua, 256); NVNcommandHandle hb = h_nvnCommandBufferEndRecording(&g_x_cb);
    mtl_test_us_watch(ua); nvn_xlat_hold(1); submit(hc); uint64_t kc = x_token(); submit(hb);
    nvn_xlat_step(1); uint64_t fc = 0; int tr = x_token_fence(kc, 1000000000ull, &fc); int gd = tr && mtl_wait(fc, 1000000000ull);   // the copy translated and complete on the GPU
    nvn_xlat_hold(0); idle(); seen = mtl_test_us_seen(&got);
    check("a range an earlier, then unrun submission's copy writes: no captured copy of it, even with the copy complete", tr && gd && seen == 0);

    // ReadTexels waits for the queue
    tex_t t = {.magic = MAGIC('T', 'E', 'X', 0), .serial = 2600001}; t.d = (nvn_texdesc){.target = NVN_TEXTURE_TARGET_2D, .format = NVN_FORMAT_RGBA8, .width = 4, .height = 4, .depth = 1, .levels = 1, .samples = 1};
    if (!tex_mtl(&t)) { printf("no texture\n"); return 2; }
    NVNcopyRegion rg = {.width = 4, .height = 4, .depth = 1}; float red[4] = {1, 0, 0, 1};
    xrec(40960); h_nvnCommandBufferClearTexture(&g_x_cb, (const NVNtexture *)&t, NULL, &rg, red, 15); NVNcommandHandle hx = h_nvnCommandBufferEndRecording(&g_x_cb);
    nvn_xlat_hold(1); submit(hx); pthread_t th; pthread_create(&th, NULL, release_later, NULL);
    uint8_t px[64] = {0}; uint64_t t0 = now_ns(); h_nvnTextureReadTexels((const NVNtexture *)&t, NULL, &rg, px); uint64_t dt = now_ns() - t0;
    pthread_join(th, NULL); idle();
    check("ReadTexels after a held clear: waits for the queue (>= 90 ms), reads red", px[0] == 255 && px[1] == 0 && px[3] == 255 && dt >= 90000000ull);

    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
} }
