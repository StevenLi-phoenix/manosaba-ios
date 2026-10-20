// The queue's contract before NVN translation moves to a thread of its own (plan item 61a; the cases of LASRecomp hjqcan c404f1d
// tools/test_render_queue.c, MIT, written anew for this replay: there they run against a render queue, here against the submit as it is now, so a
// translation thread has to keep them). Through the public NVN calls:
//   nested lists kept: a parent calls a child recorded in game memory (an event +1 and a fence) and a child that overflows into a host segment
//   (three events +10, two of them in the segment); both command buffer objects are finalized and used again for lists that add 1000 (never
//   submitted), the second also overflowing into a host segment, and the child's control memory is written over (the SDK copies control data at
//   CallCommands); the parent submitted twice adds 62
//   a fence in a nested list published when queued: before any submit a wait on it is FAILED; right after the first submit (completion held) it
//   holds that submission's fence and a wait times out; after the second, the second's
//   two waiters: one on a queue fence after the first submit, one on the nested fence of the second; with completion let go up to the first, the
//   first returns (0 or 1, as it may start late) while the second still waits and a 1 ms wait on its sync times out; let go, it returns
//   the four wait results: ALREADY_SIGNALED 0, CONDITION_SATISFIED 1 (a wait released by another thread), TIMEOUT_EXPIRED 2, FAILED 3
//   an old fence past a ring: after 4,097 later submissions (MacNativeGames' fence ring has 4,096 entries) the first queue fence is still
//   ALREADY_SIGNALED, and the same sync fenced again on a held submission holds the new fence and times out
// Also run with the translation thread (DM_NVN_XLAT=1): fences read through fence_of / last_fence, which resolve a submission's token
// Completion is held only after QueueFinish: the submit waits for one of the four occlusion-count buffers (nvn_mtl.m g_vis_ring), so five
// submissions not done stop the fifth. Built as t_nvn (make -C gfx/tests t_rqueue). The nested fence given the last submission's fence (-DDM_TEST_FENCE_SYNC_LAST): 5 fail.
#include "../nvn/hle_nvn.c"
const dm_game_profile dm_game = { .id = "test", .nvn_api_major = 55, .nvn_api_minor = 15, .nvn_c2 = {0x168, 128, 0x568, 128, 0x120, 8}, .nvn_depth_mode = 1 };
uint64_t g_t0_ns;
void dm_touch_screen(int w, int h) { (void)w; (void)h; }
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000ull + t.tv_nsec; }
void mtl_test_done_hold(int on);
uint64_t mtl_test_done_to(uint64_t f);

static int g_fail, g_n;
#define EXPECT(name, want, got) do { long long w_ = (long long)(want), g_ = (long long)(got); g_n++;                 \
    printf("%-96s expected %-6lld → got %-6lld %s\n", name, w_, g_, w_ == g_ ? "✓" : "✗"); if (w_ != g_) g_fail++; } while (0)
#define P(name) ((NVNT_##name)proc(#name))
static PFNNVNDEVICEGETPROCADDRESSPROC gpa; static NVNdevice dev;
static void *proc(const char *n) { void *p = (void *)gpa(&dev, n); if (!p) { printf("address lookup failed %s\n", n); exit(1); } return p; }

static NVNmemoryPool pool; static uint8_t poolmem[1 << 20] __attribute__((aligned(4096)));
static NVNevent ev;
static void begin(NVNcommandBuffer *c, ptrdiff_t off, size_t n) {   // a fresh command buffer object on a window of the pool (no memory callback)
    P(nvnCommandBufferInitialize)(c, &dev); P(nvnCommandBufferAddCommandMemory)(c, &pool, off, n); P(nvnCommandBufferBeginRecording)(c);
}
static void add(NVNcommandBuffer *c, uint32_t v) { P(nvnCommandBufferSignalEvent)(c, &ev, NVN_EVENT_SIGNAL_MODE_ADD, NVN_EVENT_SIGNAL_LOCATION_BOTTOM, 0, v); }
// A sync's Metal fence: with the translation thread (DM_NVN_XLAT=1, plan item 61) a token is the number of a queued submission, resolved once
// that submission has been translated; the newest submission's Metal fence likewise
static uint64_t fence_of(const NVNsync *s) {
    uint64_t f = 0; if (!sync_fence_load((const sync_t *)s, &f)) return 0;
    if (f & SYNC_TOKEN) { uint64_t m = 0; return x_token_fence(f & ~SYNC_TOKEN, 5000000000ull, &m) ? m : 0; }
    return f;
}
static uint64_t last_fence(void) { if (!xlat_on()) return mtl_last_fence(); uint64_t m = 0; return x_token_fence(x_token(), 5000000000ull, &m) ? m : 0; }
static int wait_held(uint64_t f) {   // until the GPU has completed up to f (completion held: the done fence stays)
    for (int i = 0; i < 5000; i++) { if (mtl_test_done_to(0) >= f) return 1; usleep(1000); }
    return 0;
}
typedef struct { const NVNsync *s; _Atomic int done; int result; } waiter_t;
static void *waiter(void *a) { waiter_t *w = a; w->result = P(nvnSyncWait)(w->s, 5000000000ull); atomic_store(&w->done, 1); return NULL; }
static void *release_later(void *a) { (void)a; usleep(150000); mtl_test_done_hold(0); return NULL; }

int main(void) {
    gpa = (PFNNVNDEVICEGETPROCADDRESSPROC)((PFNNVNBOOTSTRAPLOADERPROC)h_nvnBootstrapLoader)("nvnDeviceGetProcAddress");
    NVNdeviceBuilder db; P(nvnDeviceBuilderSetDefaults)(&db); P(nvnDeviceBuilderSetFlags)(&db, 0); P(nvnDeviceInitialize)(&dev, &db);
    NVNmemoryPoolBuilder pb; P(nvnMemoryPoolBuilderSetDevice)(&pb, &dev); P(nvnMemoryPoolBuilderSetDefaults)(&pb);
    P(nvnMemoryPoolBuilderSetStorage)(&pb, poolmem, sizeof poolmem); P(nvnMemoryPoolBuilderSetFlags)(&pb, 0x24); P(nvnMemoryPoolInitialize)(&pool, &pb);
    NVNeventBuilder eb; P(nvnEventBuilderSetDefaults)(&eb); P(nvnEventBuilderSetStorage)(&eb, &pool, 0x1000); P(nvnEventInitialize)(&ev, &eb);
    NVNqueueBuilder qb; NVNqueue q; static uint8_t qmem[1 << 19];
    P(nvnQueueBuilderSetDevice)(&qb, &dev); P(nvnQueueBuilderSetDefaults)(&qb); P(nvnQueueBuilderSetQueueMemory)(&qb, qmem, P(nvnQueueBuilderGetQueueMemorySize)(&qb));
    P(nvnQueueInitialize)(&q, &qb);
    NVNsync sA, s1, fresh; P(nvnSyncInitialize)(&sA, &dev); P(nvnSyncInitialize)(&s1, &dev); P(nvnSyncInitialize)(&fresh, &dev);

    // the lists: a child in game memory (+1, the nested fence), a child overflowing a 64-byte window into a host segment (+10 three times), a parent
    static uint8_t ctl[256];
    NVNcommandBuffer cg, ch, cp;
    begin(&cg, 0x10000, 4096); P(nvnCommandBufferAddControlMemory)(&cg, ctl, sizeof ctl);
    add(&cg, 1); P(nvnCommandBufferFenceSync)(&cg, &sA, NVN_SYNC_CONDITION_ALL_GPU_COMMANDS_COMPLETE, 0);
    NVNcommandHandle child_game = P(nvnCommandBufferEndRecording)(&cg);
    begin(&ch, 0x11000, 64); add(&ch, 10); add(&ch, 10); add(&ch, 10);
    NVNcommandHandle child_host = P(nvnCommandBufferEndRecording)(&ch);
    const uint8_t *jat = NULL; for (const uint8_t *p = (const uint8_t *)(uintptr_t)child_host;; ) { op_hdr x; memcpy(&x, p, sizeof x); if (x.op == OP_JUMP) { jat = p; break; } if (x.op == OP_END) break; p += x.size; }
    EXPECT("the second child overflows its 64-byte window into a host segment (a JUMP in game memory)", 1, jat && jat >= poolmem + 0x11000 && jat < poolmem + 0x11040);
    begin(&cp, 0x12000, 4096); NVNcommandHandle kids[2] = {child_game, child_host}; P(nvnCommandBufferCallCommands)(&cp, 2, kids);
    NVNcommandHandle parent = P(nvnCommandBufferEndRecording)(&cp);
    // recycled: the objects finalized and used again (lists adding 1000, never submitted; the second in a host segment too), the control memory over
    P(nvnCommandBufferFinalize)(&cg); P(nvnCommandBufferFinalize)(&ch); P(nvnCommandBufferFinalize)(&cp);
    memset(ctl, 0xff, sizeof ctl);
    begin(&cg, 0x13000, 4096); add(&cg, 1000); P(nvnCommandBufferEndRecording)(&cg);
    begin(&ch, 0x14000, 64); add(&ch, 1000); add(&ch, 1000); add(&ch, 1000); P(nvnCommandBufferEndRecording)(&ch);
    begin(&cp, 0x15000, 4096); add(&cp, 1000); P(nvnCommandBufferEndRecording)(&cp);
    begin(&cp, 0x16000, 4096); NVNcommandHandle empty = P(nvnCommandBufferEndRecording)(&cp);

    EXPECT("a fence in a nested list, before any submit: FAILED (3)", 3, P(nvnSyncWait)(&sA, 0));
    EXPECT("a sync never fenced: FAILED (3)", 3, P(nvnSyncWait)(&fresh, 0));
    mtl_test_done_hold(1);
    P(nvnQueueSubmitCommands)(&q, 1, &parent); uint64_t f1 = last_fence();
    P(nvnQueueFenceSync)(&q, &s1, NVN_SYNC_CONDITION_ALL_GPU_COMMANDS_COMPLETE, 0);
    EXPECT("right after the first submit the nested fence holds that submission's fence", 1, f1 && fence_of(&sA) == f1);
    EXPECT("... and a wait on it times out while the submission is held: TIMEOUT_EXPIRED (2)", 2, P(nvnSyncWait)(&sA, 0));
    P(nvnQueueSubmitCommands)(&q, 1, &parent); uint64_t f2 = last_fence();
    EXPECT("after the second submit, the second's", 1, f2 == f1 + 1 && fence_of(&sA) == f2);

    waiter_t wa = {&s1}, wb = {&sA}; pthread_t ta, tb;
    pthread_create(&ta, NULL, waiter, &wa); pthread_create(&tb, NULL, waiter, &wb);
    EXPECT("the GPU completes both submissions (completion still held)", 1, wait_held(f2));
    usleep(50000); mtl_test_done_to(f1); pthread_join(ta, NULL);
    EXPECT("two waiters: let go up to the first submission, its waiter returns satisfied (0 or 1)", 1, wa.result == 0 || wa.result == 1);
    usleep(100000);
    EXPECT("... while the waiter on the second's nested fence still waits", 0, atomic_load(&wb.done));
    EXPECT("... and a 1 ms wait on that sync: TIMEOUT_EXPIRED (2)", 2, P(nvnSyncWait)(&sA, 1000000));
    mtl_test_done_hold(0); pthread_join(tb, NULL);
    EXPECT("let go, the second waiter returns satisfied (0 or 1)", 1, wb.result == 0 || wb.result == 1);
    EXPECT("nested lists kept: the parent submitted twice ran both children twice (2 x (1 + 30)), none of the recycled lists", 62, P(nvnEventGetValue)(&ev));

    P(nvnQueueFinish)(&q); mtl_test_done_hold(1); P(nvnQueueSubmitCommands)(&q, 1, &empty); NVNsync s3; P(nvnSyncInitialize)(&s3, &dev);
    P(nvnQueueFenceSync)(&q, &s3, NVN_SYNC_CONDITION_ALL_GPU_COMMANDS_COMPLETE, 0);
    pthread_t tr; pthread_create(&tr, NULL, release_later, NULL);
    EXPECT("a wait released by another thread: CONDITION_SATISFIED (1)", 1, P(nvnSyncWait)(&s3, 5000000000ull));
    pthread_join(tr, NULL);
    EXPECT("a fence already completed: ALREADY_SIGNALED (0)", 0, P(nvnSyncWait)(&s1, 0));

    for (int i = 0; i < 4097; i++) P(nvnQueueSubmitCommands)(&q, 1, &empty);
    EXPECT("4,097 submissions later the first queue fence is still ALREADY_SIGNALED (0)", 0, P(nvnSyncWait)(&s1, 0));
    P(nvnQueueFinish)(&q); mtl_test_done_hold(1); P(nvnQueueSubmitCommands)(&q, 1, &empty);
    P(nvnQueueFenceSync)(&q, &s1, NVN_SYNC_CONDITION_ALL_GPU_COMMANDS_COMPLETE, 0);
    EXPECT("the same sync fenced again holds the newest fence", 1, fence_of(&s1) == last_fence() && last_fence() == f2 + 4099);
    EXPECT("... and times out while that submission is held: TIMEOUT_EXPIRED (2)", 2, P(nvnSyncWait)(&s1, 0));
    mtl_test_done_hold(0);
    int last = P(nvnSyncWait)(&s1, 5000000000ull);
    EXPECT("... let go: satisfied (0 or 1)", 1, last == 0 || last == 1);
    printf("== %d checks, %d failed ==\n", g_n, g_fail);
    return g_fail != 0;
}
