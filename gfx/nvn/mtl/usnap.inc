// gfx/nvn/mtl/usnap.inc, part of nvn_mtl.m (included there in order, one translation unit): Uniform snapshots: each submission's copies of the uniform ranges it binds
// ---------- Uniform snapshots (method from MacNativeGames' usnap, ly05010419/MacNativeGames nvn_mtl.m, MIT; written anew here) ----------
// A submission is replayed inside nvnQueueSubmitCommands for 10-17 ms and its GPU work starts after that; on the console the call returns at once
// and the GPU reads the constants soon after. Tears of the Kingdom, having handed the frame over, rebuilds per-object constant pools on other threads
// (MacNativeGames found main+0x87a604, called from main+0x874f50): in the dive from the Great Sky Island up to 1041 bound uniform ranges of one
// submission changed while it was on the GPU (DM_NVN_GPUWATCH, byte compare; vertex stage slot 4, 256 bytes: the objects' transforms), and those
// frames showed rock walls in front of the camera, a flat ground or no terrain. So when a submission starts, every uniform range it binds is copied
// into its argument arena (ranges up to 256 bytes apart in one memory pool merged, at most 1 MiB each, each copy at the same offset modulo 256), and
// bindings and replay-time reads use the copy. A GPU write within the submission that reaches a copied range (inline update, buffer copy, texture to
// buffer copy, a writable storage buffer) drops that copy from then on: later readers see memory, where the GPU applies the writes in order. A range
// that a GPU write of an earlier, still running submission reaches is not copied (its result is not in memory yet). DM_NVN_UBO_SNAPSHOT=0 reads live
// memory.
typedef struct { uint64_t lo, hi; uint8_t *cpu; uint64_t gpu; void *buf; int dropped; } us_ent;   // buf: the arena block holding the copy
static us_ent *g_us; static int g_us_n, g_us_cap;      // this submission's copies, sorted and disjoint
typedef struct { uint64_t lo, hi; } us_rng;
static us_rng *g_us_rng; static int g_us_rn, g_us_rcap;
typedef struct { uint64_t lo, hi, fence; } us_gw;      // GPU writes, with the fence of their submission
static us_gw *g_us_gw; static int g_us_gwn, g_us_gwcap;
static uint64_t g_us_subs, g_us_ranges, g_us_copies, g_us_bytes, g_us_drops, g_us_skip_gpu, g_us_ns;
static inline void args_dirty_all(void);
// arena_alloc's buffer comes back unretained (plan item 185): the arena holds it until the submission completes, and an autoreleasing out parameter
// cost a retain, an autorelease and a release per call, several per draw (t_fbx ar; -DDM_TEST_ARENA_AUTORELEASING as before)
#ifndef DM_TEST_ARENA_AUTORELEASING
#define ARENA_OUT __unsafe_unretained
#else
#define ARENA_OUT __autoreleasing
#endif
static size_t arena_alloc(size_t n, ARENA_OUT id<MTLBuffer> *buf);
uint64_t mtl_done_fence(void);
static int g_us_on = -1;   // -1: not read yet (tests set it directly)
static int us_on(void) {
    if (g_us_on < 0) { const char *e = getenv("DM_NVN_UBO_SNAPSHOT"); g_us_on = !(e && !strcmp(e, "0")); MLOG("uniform snapshots at submission start: %s (DM_NVN_UBO_SNAPSHOT)", g_us_on ? "on" : "off, live memory"); }
    return g_us_on;
}
// Ranges already collected in this snapshot (open addressing, four probes): the same uniform block bound for draw after draw is collected (and
// sorted) once. A range finding no free place goes through again, which the merge drops
static us_rng g_us_seen[4096];
static int us_seen(uint64_t lo, uint64_t hi) {
    uint64_t h = ((lo >> 4) ^ (hi << 7)) * 0x9E3779B97F4A7C15ull;
    for (unsigned k = 0, i = (unsigned)(h >> 52); k < 4; k++, i = (i + 1) & 4095) {
        us_rng *e = &g_us_seen[i];
        if (e->lo == lo && e->hi == hi) return 1;
        if (!e->lo) { e->lo = lo; e->hi = hi; return 0; }
    }
    return 0;
}
// A deferred binding record (hle_nvn.c BindTexturesDeferred) reads its handles from where the game keeps them (nxnative), once a submission (plan item
// 61b, after bayonetta3-mobile nvn55_snapshot.h, MIT: frozen at submit): the first read stamps the record with the submission's generation
// (mtl_sub_begin), and the walks at the submission's start make that read (mtl_sub_index's, else mtl_sub_snapshot's, which walks for this alone
// when uniform snapshots are off), so the replay's walks (lookahead, deferral, the draw) all see the handles of the submit even if the game writes
// them meanwhile; the next submission reads them again. Before, every walk read them anew
static int32_t g_bh_gen = 2;   // records start at defer = 1: never a generation
static int g_bh_seen;
void mtl_bh_deferred_seen(void) { g_bh_seen = 1; }
static inline void bh_fresh(const p_bind_handles *b) {
#ifndef DM_TEST_BH_EACH_READ   // negative control of t_bindfreeze only: read at every walk, as before
    if (b->defer == g_bh_gen) return;
#endif
    if (b->defer && b->src && b->n > 0 && b->n <= 32) { memcpy((void *)b->h, (const void *)(uintptr_t)b->src, 8 * (size_t)b->n); ((p_bind_handles *)b)->defer = g_bh_gen; }
}
static void us_collect(const uint8_t *p, int depth, int us) {
    if (depth > 32) return;
    for (uint32_t guard = 0; guard < 4000000 && p; guard++) {
        op_hdr h; memcpy(&h, p, sizeof h);
        if (h.size < 8 || (h.size & 7) || h.op == OP_END) return;
        if (h.op == OP_JUMP) { const uint8_t *t; memcpy(&t, p + 8, 8); p = t; continue; }
        if (h.op == OP_CALL) { uint64_t t; memcpy(&t, p + 8, 8); us_collect((const uint8_t *)(uintptr_t)t, depth + 1, us); }
        else if (h.op == OP_BIND_TEX || h.op == OP_BIND_SAMP || h.op == OP_BIND_IMG) bh_fresh((const p_bind_handles *)(p + 8));
        else if (us && h.op == OP_BIND_UBO) { const p_bind_buf *b = (const p_bind_buf *)(p + 8);
            if (b->addr && b->size && b->size <= (1u << 20) && !us_seen(b->addr, b->addr + b->size)) {
                if (g_us_rn == g_us_rcap) { g_us_rcap = g_us_rcap ? g_us_rcap * 2 : 8192; g_us_rng = realloc(g_us_rng, (size_t)g_us_rcap * sizeof *g_us_rng); }
                g_us_rng[g_us_rn++] = (us_rng){b->addr, b->addr + b->size}; } }
        p += h.size;
    }
}
static int us_rng_cmp(const void *x, const void *y) { const us_rng *a = x, *b = y; return (a->lo > b->lo) - (a->lo < b->lo); }
// The bindings by start: an LSD radix sort, 11 bits a pass, all six digit histograms from one read, and a pass whose digit is the same for every range
// skipped (the addresses share their high bits). qsort with a comparison call was 0.14-0.43 ms of the 0.3-0.9 ms a snapshot took in Tears of the
// Kingdom (4,000-11,000 bindings a submission)
static us_rng *g_us_tmp; static int g_us_tcap;
static void us_rng_sort_x(us_rng *a, int n, us_rng **tmp, int *tcap, uint32_t cnt[6][2048]) {   // the scratch is the caller's (the capture of plan item 61 sorts on the game's thread)
    if (n < 64) { qsort(a, (size_t)n, sizeof *a, us_rng_cmp); return; }
    if (*tcap < n) { *tcap = n * 2; *tmp = realloc(*tmp, (size_t)*tcap * sizeof **tmp); if (!*tmp) MDIE("uniform snapshot sort buffer"); }
    memset(cnt, 0, 6 * 2048 * sizeof(uint32_t));
    for (int i = 0; i < n; i++) { uint64_t k = a[i].lo; for (int d = 0; d < 6; d++) cnt[d][(k >> (11 * d)) & 2047]++; }
    us_rng *src = a, *dst = *tmp;
    for (int d = 0; d < 6; d++) {
        uint32_t *c = cnt[d]; if (c[(a[0].lo >> (11 * d)) & 2047] == (uint32_t)n) continue;   // one digit value for all: nothing to move
        for (uint32_t v = 0, acc = 0; v < 2048; v++) { uint32_t x = c[v]; c[v] = acc; acc += x; }
        for (int i = 0; i < n; i++) dst[c[(src[i].lo >> (11 * d)) & 2047]++] = src[i];
        us_rng *t = src; src = dst; dst = t;
    }
    if (src != a) memcpy(a, src, (size_t)n * sizeof *a);
}
static void us_rng_sort(us_rng *a, int n) { static uint32_t cnt[6][2048]; us_rng_sort_x(a, n, &g_us_tmp, &g_us_tcap, cnt); }
static int us_gw_cmp(const void *x, const void *y) { const us_gw *a = x, *b = y; return (a->lo > b->lo) - (a->lo < b->lo); }
// The copies themselves, done before the replay starts as before: above US_PAR_MIN bytes split by bytes over US_PAR parts on the system's worker threads
// (one on this thread), each part whole entries
enum { US_PAR = 4, US_PAR_MIN = 256 << 10 };
static int g_us_par = -1; static uint64_t g_us_par_n;   // DM_NVN_UBO_SNAPSHOT_PAR=0: one thread; g_us_par_n: snapshots copied in parts
// A source the game made inaccessible after its pool was registered used to end the process (SIGSEGV / SIGBUS, also on the worker threads that
// copy in parts): each copy runs under a per-thread fault guard and a faulting one is dropped, so its draws read memory as without snapshots
// (MacNativeGames guards usnap_copy_one the same way after a helper thread died in about one session of twelve; MIT, written anew here)
static __thread struct { sigjmp_buf jb; uint64_t lo, hi; int armed; } t_usg;
static _Atomic uint64_t g_us_faults, g_us_jumps;   // g_us_jumps: sigsetjmp calls of the copies (t_usnap)
static int us_fault(void *addr, int is_write, void *uc) {   // fault hook: only a fault inside the range this thread is copying
    (void)is_write; (void)uc; uint64_t a = (uint64_t)(uintptr_t)addr;
    if (!t_usg.armed || a < t_usg.lo || a >= t_usg.hi) return 0;
    t_usg.armed = 0; siglongjmp(t_usg.jb, 1);
}
// One sigsetjmp for the run of copies, not one a copy (plan item 225): saving the signal mask is two system calls on Darwin (sigprocmask and
// sigaltstack), and the copies of a submission are hundreds; 38 % of the game thread's submit call on the M5. A fault drops its copy and the loop goes
// on with the next behind a new sigsetjmp
static void us_copy_range(int a, int b) {
    static dispatch_once_t hook; dispatch_once(&hook, ^{ dm_fault_hook_register(us_fault); });
    volatile int i = a;
    for (;;) {
        atomic_fetch_add_explicit(&g_us_jumps, 1, memory_order_relaxed);
        if (!sigsetjmp(t_usg.jb, 1)) {
            for (; i < b; i++) {
#ifdef DM_TEST_US_GUARD_EACH   // negative control of t_usnap only: a sigsetjmp for every copy, as before
                atomic_fetch_add_explicit(&g_us_jumps, 1, memory_order_relaxed);
                if (sigsetjmp(t_usg.jb, 1)) { g_us[i].dropped = 1; atomic_fetch_add(&g_us_faults, 1); continue; }
#endif
                t_usg.lo = g_us[i].lo; t_usg.hi = g_us[i].hi; t_usg.armed = 1;
                memcpy(g_us[i].cpu, (const void *)(uintptr_t)g_us[i].lo, (size_t)(g_us[i].hi - g_us[i].lo)); t_usg.armed = 0;
            }
            return;
        }
        g_us[i].dropped = 1; atomic_fetch_add(&g_us_faults, 1); i++;   // (the fault hook disarmed the guard)
    }
}
static void us_copy_all(void) {
    if (g_us_par < 0) { const char *e = getenv("DM_NVN_UBO_SNAPSHOT_PAR"); g_us_par = !(e && !strcmp(e, "0")); }
    uint64_t total = 0; for (int i = 0; i < g_us_n; i++) total += g_us[i].hi - g_us[i].lo;
    if (!g_us_par || total < US_PAR_MIN || g_us_n < US_PAR) { us_copy_range(0, g_us_n); return; }
    int cut[US_PAR + 1] = {0}; uint64_t acc = 0; int k = 1;
    for (int i = 0; i < g_us_n && k < US_PAR; i++) { acc += g_us[i].hi - g_us[i].lo; if (acc >= total * (uint64_t)k / US_PAR) cut[k++] = i + 1; }
    while (k <= US_PAR) cut[k++] = g_us_n;
    const int *cp = cut; g_us_par_n++;   // dispatch_apply returns when every part is done
    dispatch_apply(US_PAR, dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^(size_t part) { us_copy_range(cp[part], cp[part + 1]); });
}
