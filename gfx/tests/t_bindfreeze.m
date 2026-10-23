// Handle bindings of more than 32 and deferred bindings frozen at submit (plan item 61b, after bayonetta3-mobile's nvn55_snapshot.h, MIT, written
// anew). Recorded through the public NVN calls, replayed as nvnQueueSubmitCommands does (mtl_sub_begin, index, snapshot, the lists, commit):
//   40 separate textures from slot 4 and 70 separate samplers from slot 0 reach every slot (records of 32 and the rest), and a bind past the 128
//   slots or 33 images is refused (nothing bound)
//   a deferred bind of two handles in a child the parent calls twice: the handles written over after the submission's start (as another game thread
//   would) do not reach this submission, whose state holds the handles of the submit; the next submission takes the new ones
//   a deferred bind of 40: both of its records read their own part
// Run with uniform snapshots on and off (DM_NVN_UBO_SNAPSHOT=0: then mtl_sub_snapshot walks for the deferred handles alone).
// More than 32 dropped (-DDM_TEST_BIND_NO_SPLIT): 4 fail; deferred handles read at every walk (-DDM_TEST_BH_EACH_READ): 2 fail.
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
static int fail, count;
#define CHECK(c, ...) do { count++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)

static pool_t g_bf_pool; static NVNcommandBuffer g_bf_cb;
static void bf_rec(ptrdiff_t off) { h_nvnCommandBufferInitialize(&g_bf_cb, NULL); h_nvnCommandBufferAddCommandMemory(&g_bf_cb, (const NVNmemoryPool *)&g_bf_pool, off, 8192); h_nvnCommandBufferBeginRecording(&g_bf_cb); }
// One submission of handles h as nvnQueueSubmitCommands makes it; between its start and the replay, after (if not NULL) runs as another thread would
static void submit(NVNcommandHandle h, void (*after)(void)) {
    uint64_t hs[1] = {(uint64_t)h};
    mtl_sub_begin(); mtl_sub_index(hs, 1); mtl_sub_snapshot(hs, 1);
    if (after) after();
    nvn_exec_rest(NULL, 0); nvn_exec((const uint8_t *)(uintptr_t)hs[0]);
    mtl_wait(mtl_sub_commit(), 5000000000ull);
}
static int same(const uint64_t *a, const uint64_t *b, int n) { return !memcmp(a, b, 8 * (size_t)n); }
static uint64_t g_two[2], g_forty[40];
static void write_two(void) { g_two[0] = 0x30000c; g_two[1] = 0x30000d; }
static void write_forty(void) { g_forty[3] = 0x40ff03; g_forty[35] = 0x40ff23; }

int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); mtl_init();
    void *mem = NULL; if (posix_memalign(&mem, getpagesize(), 1 << 17)) abort(); memset(mem, 0, 1 << 17);
    g_bf_pool = (pool_t){.mem = mem, .size = 1 << 17}; mtl_pool_add(mem, 1 << 17);
    const int fs = NVN_SHADER_STAGE_FRAGMENT;

    uint64_t t40[40], s70[70], i33[33], far[40];
    for (int i = 0; i < 40; i++) { t40[i] = 0x100000 + i; far[i] = 0x500000 + i; }
    for (int i = 0; i < 70; i++) s70[i] = 0x200000 + i;
    for (int i = 0; i < 33; i++) i33[i] = 0x600000 + i;
    bf_rec(0);
    h_nvnCommandBufferBindSeparateTextures(&g_bf_cb, fs, 4, 40, (const NVNseparateTextureHandle *)t40);
    h_nvnCommandBufferBindSeparateSamplers(&g_bf_cb, fs, 0, 70, (const NVNseparateSamplerHandle *)s70);
    h_nvnCommandBufferBindSeparateTextures(&g_bf_cb, fs, 100, 40, (const NVNseparateTextureHandle *)far);
    h_nvnCommandBufferBindImages(&g_bf_cb, fs, 0, 33, (const NVNimageHandle *)i33);
    NVNcommandHandle big = h_nvnCommandBufferEndRecording(&g_bf_cb);
    state_reset(); submit(big, NULL);
    CHECK(same(&S.tex[fs][4], t40, 40) && !S.tex[fs][3] && !S.tex[fs][44], "40 separate textures from slot 4: every slot, none beside");
    CHECK(same(S.samp[fs], s70, 70) && !S.samp[fs][70], "70 separate samplers from slot 0: every slot");
    int none = 1; for (int i = 100; i < 128; i++) none &= !S.tex[fs][i]; for (int i = 0; i < 32; i++) none &= !S.img[fs][i];
    CHECK(none, "past the 128 texture slots, and 33 images: refused, nothing bound");

    g_two[0] = 0x30000a; g_two[1] = 0x30000b;
    bf_rec(16384); h_nvnCommandBufferBindTexturesDeferred(&g_bf_cb, fs, 0, 2, (const NVNtextureHandle *)g_two); NVNcommandHandle child = h_nvnCommandBufferEndRecording(&g_bf_cb);
    bf_rec(32768); NVNcommandHandle kids[2] = {child, child}; h_nvnCommandBufferCallCommands(&g_bf_cb, 2, kids); NVNcommandHandle parent = h_nvnCommandBufferEndRecording(&g_bf_cb);
    state_reset(); submit(parent, write_two);
    CHECK(S.tex[fs][0] == 0x30000a && S.tex[fs][1] == 0x30000b, "a deferred bind called twice, its handles written over after the submit: the submit's handles (%#llx %#llx)",
          (unsigned long long)S.tex[fs][0], (unsigned long long)S.tex[fs][1]);
    state_reset(); submit(parent, NULL);
    CHECK(S.tex[fs][0] == 0x30000c && S.tex[fs][1] == 0x30000d, "the next submission takes the new handles");

    for (int i = 0; i < 40; i++) g_forty[i] = 0x400000 + i;
    uint64_t want[40]; memcpy(want, g_forty, sizeof want);
    bf_rec(49152); h_nvnCommandBufferBindTexturesDeferred(&g_bf_cb, fs, 0, 40, (const NVNtextureHandle *)g_forty); NVNcommandHandle d40 = h_nvnCommandBufferEndRecording(&g_bf_cb);
    state_reset(); submit(d40, write_forty);
    CHECK(same(S.tex[fs], want, 40), "a deferred bind of 40, written over after the submit: both records the submit's handles");
    state_reset(); submit(d40, NULL);
    CHECK(S.tex[fs][3] == 0x40ff03 && S.tex[fs][35] == 0x40ff23 && S.tex[fs][34] == 0x400022, "... the next submission: each record its own part, new");

    mtl_pool_del(mem); free(mem);
    printf("== %d checks, %d failed ==\n", count, fail); return fail != 0;
} }
