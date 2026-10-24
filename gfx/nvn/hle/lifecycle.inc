// gfx/nvn/hle/lifecycle.inc, part of hle_nvn.c (included there in order, one translation unit): Lifecycle: suspend and resume (dm_gfx_suspend)
// ---------- Lifecycle: dm_gfx_suspend (called before iOS goes to the background; equivalent to Switch suspend) ----------
// on=1: subsequent submits / presents block until on=0; before returning, wait for in-progress submit / present calls to exit and for all in-flight GPU work to complete (iOS requires no GPU work in the background).
static pthread_mutex_t g_susp_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_susp_cv = PTHREAD_COND_INITIALIZER;
static int g_susp, g_inflight_api;
static void gfx_enter(void) { pthread_mutex_lock(&g_susp_mu); while (g_susp) pthread_cond_wait(&g_susp_cv, &g_susp_mu); g_inflight_api++; pthread_mutex_unlock(&g_susp_mu); }
static void gfx_leave(void) { pthread_mutex_lock(&g_susp_mu); g_inflight_api--; pthread_cond_broadcast(&g_susp_cv); pthread_mutex_unlock(&g_susp_mu); }
void dm_gfx_suspend(int on) {
    pthread_mutex_lock(&g_susp_mu);
    g_susp = on != 0;
    if (on) while (g_inflight_api) pthread_cond_wait(&g_susp_cv, &g_susp_mu);
    else pthread_cond_broadcast(&g_susp_cv);
    pthread_mutex_unlock(&g_susp_mu);
    if (on) { x_drain(); mtl_drain(); LOG("[nvn] graphics suspended: in-flight GPU work drained"); } else LOG("[nvn] graphics resumed");
}
// DM_TEST_WORK_MS=<ms> (a test of the pacing, plan item 53): the frame's first submission waits until <ms> after the previous present, as a game whose
// own work takes that long; Tears of the Kingdom's frame-rate controller counts it (a wait in the present would not be: it measures to before it)
static void sleep_until_ns(uint64_t dl);
static _Atomic uint64_t g_test_work_from;   // the last present's time; taken by the frame's first submission
static int test_work_ms(void) { static int ms = -1; if (ms < 0) { const char *e = getenv("DM_TEST_WORK_MS"); ms = e ? atoi(e) : 0; if (ms > 0) LOG("[nvn] DM_TEST_WORK_MS=%d: each frame's work made to take that long (a test)", ms); } return ms; }
// The replay of one submission into Metal: on the calling thread, or the translation thread's (below) with the submit call's capture
static uint64_t sub_exec(int n, const uint64_t *h, mtl_cap *cap) {
    gfx_mt_enter("SubmitCommands");
    uint64_t t0 = now_ns(), c0 = clock_gettime_nsec_np(CLOCK_THREAD_CPUTIME_ID), i0, y0, s0 = thr_sys_ns(); thr_counts(&i0, &y0);
    thr_lvl l0[THR_LVL_MAX]; int lv = thr_levels(l0);
    mtl_sub_begin();
    mtl_sub_install(cap);
    mtl_sub_index(h, n);
    mtl_sub_snapshot(h, n);
    for (int i = 0; i < n; i++) { nvn_exec_rest(h + i + 1, n - i - 1); nvn_exec((const uint8_t *)h[i]); }
    nvn_exec_rest(NULL, 0);
    uint64_t f = mtl_sub_commit();
    uint64_t dw = now_ns() - t0;
    atomic_fetch_add(&g_nvn_ns, dw); atomic_fetch_add(&g_sub_wall_ns, dw);
    uint64_t i1, y1; thr_counts(&i1, &y1); uint64_t dc = clock_gettime_nsec_np(CLOCK_THREAD_CPUTIME_ID) - c0, di = i1 - i0;
    atomic_fetch_add(&g_cum_sub_cyc, y1 - y0); atomic_fetch_add(&g_cum_sub_sys_ns, thr_sys_ns() - s0); atomic_fetch_add(&g_cum_sub_wall_ns, dw);
    atomic_fetch_add(&g_sub_cpu_ns, dc); atomic_fetch_add(&g_cum_sub_cpu_ns, dc);
    atomic_fetch_add(&g_sub_instr, di); atomic_fetch_add(&g_cum_sub_instr, di);
    { thr_lvl l1[THR_LVL_MAX]; if (lv && thr_levels(l1)) for (int k = 0; k < thr_nlvl(); k++) {
          atomic_fetch_add(&g_cum_lvl_cyc[k], l1[k].cyc - l0[k].cyc); atomic_fetch_add(&g_cum_lvl_mach[k], (l1[k].user + l1[k].sys) - (l0[k].user + l0[k].sys)); } }
    gfx_mt_leave();
    return f;
}
