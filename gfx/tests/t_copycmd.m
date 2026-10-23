// CopyCommands copies, CallCommands calls (plan item 64b; the semantics dr3-mobile read in the shipped SDK, its cases written anew). Recorded
// through the public calls, replayed as nvnQueueSubmitCommands does, judged by an event the lists add to:
//   a copy of a list (+1) whose memory is then recorded over with +100: the copy adds 1; a call of it adds 100 (it runs what is there)
//   a copy of a list that calls a +10 list, the +10 list then recorded over with +1000: 10 (nested calls inlined at the copy)
//   a destination whose memory callback hands out the source's own memory while copying: the source's commands, whole (collected first)
//   two lists in one copy: both, in order (an event written 5, then added 2: 7)
//   a list that calls itself: the copy refused, the destination as it was; submitted as is, the replay stops following at 64 calls deep
//   a source with a record of a bad size: refused
// Copy recorded as a call, as before (-DDM_TEST_COPY_AS_CALL): 4 fail.
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
static uint8_t *g_cc_mem; static pool_t g_cc_pool; static volatile uint32_t *g_cc_ev;
static void begin(NVNcommandBuffer *c, ptrdiff_t off, size_t len) { h_nvnCommandBufferInitialize(c, NULL); h_nvnCommandBufferAddCommandMemory(c, (const NVNmemoryPool *)&g_cc_pool, off, len); h_nvnCommandBufferBeginRecording(c); }
static void add(NVNcommandBuffer *c, int mode, uint32_t v) { ev_t e = {MAGIC('E', 'V', 'N', 'T'), g_cc_ev}; h_nvnCommandBufferSignalEvent(c, (const NVNevent *)&e, mode, NVN_EVENT_SIGNAL_LOCATION_BOTTOM, 0, v); }
static NVNcommandHandle list(ptrdiff_t off, uint32_t v) { NVNcommandBuffer c; begin(&c, off, 4096); add(&c, NVN_EVENT_SIGNAL_MODE_ADD, v); return h_nvnCommandBufferEndRecording(&c); }
static uint32_t run(NVNcommandHandle h) {   // the event after one submission of h, from 0
    *g_cc_ev = 0; uint64_t hs[1] = {(uint64_t)h};
    mtl_sub_begin(); mtl_sub_index(hs, 1); mtl_sub_snapshot(hs, 1); nvn_exec_rest(NULL, 0); nvn_exec((const uint8_t *)(uintptr_t)h); mtl_wait(mtl_sub_commit(), 5000000000ull);
    return *g_cc_ev;
}
static ptrdiff_t g_cc_give;
static void give(NVNcommandBuffer *c, NVNcommandBufferMemoryEvent e, size_t minsz, void *d) { (void)e; (void)minsz; (void)d; h_nvnCommandBufferAddCommandMemory(c, (const NVNmemoryPool *)&g_cc_pool, g_cc_give, 4096); }

int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); mtl_init(); state_reset();
    if (posix_memalign((void **)&g_cc_mem, getpagesize(), 1 << 20)) abort(); memset(g_cc_mem, 0, 1 << 20); g_cc_pool = (pool_t){.mem = g_cc_mem, .size = 1 << 20}; mtl_pool_add(g_cc_mem, 1 << 20);
    g_cc_ev = (volatile uint32_t *)(g_cc_mem + 0xff000);

    NVNcommandHandle s = list(0x1000, 1), cs; NVNcommandBuffer t;
    begin(&t, 0x2000, 4096); h_nvnCommandBufferCopyCommands(&t, 1, &s); NVNcommandHandle tc = h_nvnCommandBufferEndRecording(&t);
    begin(&t, 0x3000, 4096); h_nvnCommandBufferCallCommands(&t, 1, &s); NVNcommandHandle tl = h_nvnCommandBufferEndRecording(&t);
    cs = list(0x1000, 100);   // the source's memory recorded over
    check("a copy, its source then recorded over (+100): adds 1; a call of it adds 100", cs == s && run(tc) == 1 && run(tl) == 100);

    NVNcommandHandle inner = list(0x4000, 10); NVNcommandBuffer o; begin(&o, 0x5000, 4096); h_nvnCommandBufferCallCommands(&o, 1, &inner); NVNcommandHandle outer = h_nvnCommandBufferEndRecording(&o);
    begin(&t, 0x6000, 4096); h_nvnCommandBufferCopyCommands(&t, 1, &outer); NVNcommandHandle t2 = h_nvnCommandBufferEndRecording(&t);
    list(0x4000, 1000);
    check("a copy of a list calling +10, that list then recorded over with +1000: 10 (inlined)", run(t2) == 10);

    NVNcommandBuffer src; begin(&src, 0x8000, 4096); for (int i = 0; i < 40; i++) add(&src, NVN_EVENT_SIGNAL_MODE_ADD, 1); NVNcommandHandle big = h_nvnCommandBufferEndRecording(&src);
    begin(&t, 0x9000, 64); g_cc_give = 0x8000; h_nvnCommandBufferSetMemoryCallback(&t, give); h_nvnCommandBufferCopyCommands(&t, 1, &big); NVNcommandHandle t3 = h_nvnCommandBufferEndRecording(&t);
    check("the destination's callback hands out the source's memory while copying: all 40 of the source's +1", run(t3) == 40);

    NVNcommandHandle w5; { NVNcommandBuffer c; begin(&c, 0xa000, 4096); add(&c, NVN_EVENT_SIGNAL_MODE_WRITE, 5); w5 = h_nvnCommandBufferEndRecording(&c); }
    NVNcommandHandle a2 = list(0xb000, 2), two[2] = {w5, a2};
    begin(&t, 0xc000, 4096); h_nvnCommandBufferCopyCommands(&t, 2, two); NVNcommandHandle t4 = h_nvnCommandBufferEndRecording(&t);
    check("two lists in one copy: both, in order (written 5, then +2)", run(t4) == 7);

    NVNcommandBuffer x; begin(&x, 0xd000, 4096); NVNcommandHandle self = (NVNcommandHandle)(uintptr_t)(g_cc_mem + 0xd000);
    add(&x, NVN_EVENT_SIGNAL_MODE_ADD, 1); h_nvnCommandBufferCallCommands(&x, 1, &self); NVNcommandHandle xs = h_nvnCommandBufferEndRecording(&x);
    begin(&t, 0xe000, 4096); size_t before = ((cmd_t *)&t)->used; h_nvnCommandBufferCopyCommands(&t, 1, &xs); size_t after = ((cmd_t *)&t)->used;
    check("a list that calls itself: the copy refused, the destination as it was", xs == self && before == after);
    uint32_t got = run(xs);
    check("... submitted as is: the replay stops following at 64 calls deep (65 adds)", got == 65);

    NVNcommandBuffer b; begin(&b, 0xf000, 4096); add(&b, NVN_EVENT_SIGNAL_MODE_ADD, 1); NVNcommandHandle bad = h_nvnCommandBufferEndRecording(&b);
    ((op_hdr *)(uintptr_t)(bad + 8))->size = 12;   // the record after BEGIN: a size that is not a multiple of 8
    begin(&t, 0x10000, 4096); before = ((cmd_t *)&t)->used; h_nvnCommandBufferCopyCommands(&t, 1, &bad);
    check("a source with a record of a bad size: refused", ((cmd_t *)&t)->used == before);
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
} }
