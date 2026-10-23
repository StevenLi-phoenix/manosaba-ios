// gfx/nvn/mtl/frame_timing.inc, part of nvn_mtl.m (included there in order, one translation unit): Boot instrumentation, per-frame GPU time, same-process alternating measurement (DM_NVN_UB), GPU time tags, DM_NVN_GPUWATCH
// ---------- Boot instrumentation (DM_BOOTPROF=1): metallib loading, pipeline creation (both happen synchronously during replay on the submit thread), texture upload, first-frame present ----------
static uint64_t bp_now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000ull + t.tv_nsec; }
static uint64_t g_bp_t0;   // Time of process start (when this module is loaded)
__attribute__((constructor)) static void bp_t0(void) { g_bp_t0 = bp_now(); }
static int bootprof(void) { static int v = -1; if (v < 0) v = getenv("DM_BOOTPROF") != NULL; return v; }
static uint64_t g_bp_lib_n, g_bp_lib_ns, g_bp_pso_n, g_bp_pso_ns, g_bp_up_bytes, g_bp_first_present;
static uint64_t g_up_total_ns;   // Cumulative upload time (same source as the per-frame @upload_ms)
// ---------- Per-frame GPU time aggregation ----------
#define GRING 1024
static struct { _Atomic uint64_t frame, ns, t0, t1; _Atomic int pending; } g_gpu[GRING];   // t0 / t1: the frame's earliest GPUStart / latest GPUEnd (mach absolute time, ns)
// The frame's GPU time is the union of its command buffers' spans: Apple GPUs start a buffer while the one before it runs, so the plain sum (ns) counted
// the overlap twice (LASRecomp PR #197: summed 13-17 ms against a union of 6.5-7 ms). The spans of the last GIVR frames are kept (the time is read 8
// frames late); a full list is merged, and if no two spans overlap the two closest are joined (their gap is counted, the only overestimate)
#define GIVR 32
enum { GIV = 128 };
static struct { uint64_t frame; uint32_t n; uint64_t iv[GIV][2]; os_unfair_lock lk; } g_giv[GIVR];
static int giv_cmp(const void *a, const void *b) { const uint64_t *x = a, *y = b; return x[0] < y[0] ? -1 : x[0] > y[0]; }
static uint32_t giv_merge(uint64_t (*iv)[2], uint32_t n) {   // sort by start and join overlapping spans in place; returns the new count
    if (n < 2) return n;
    qsort(iv, n, sizeof *iv, giv_cmp);
    uint32_t m = 0;
    for (uint32_t i = 1; i < n; i++) { if (iv[i][0] <= iv[m][1]) { if (iv[i][1] > iv[m][1]) iv[m][1] = iv[i][1]; } else { m++; iv[m][0] = iv[i][0]; iv[m][1] = iv[i][1]; } }
    return m + 1;
}
static uint64_t giv_union_ns(uint64_t (*iv)[2], uint32_t *n) { *n = giv_merge(iv, *n); uint64_t s = 0; for (uint32_t i = 0; i < *n; i++) s += iv[i][1] - iv[i][0]; return s; }
static void giv_add(uint64_t f, uint64_t s0, uint64_t s1) {
    unsigned k = f % GIVR; os_unfair_lock_lock(&g_giv[k].lk);
    if (g_giv[k].frame != f) { g_giv[k].frame = f; g_giv[k].n = 0; }
    if (g_giv[k].n == GIV) g_giv[k].n = giv_merge(g_giv[k].iv, GIV);
    if (g_giv[k].n == GIV) {   // still full: join the two closest neighbours
        uint32_t bi = 0; uint64_t bg = UINT64_MAX;
        for (uint32_t i = 0; i + 1 < GIV; i++) { uint64_t gap = g_giv[k].iv[i + 1][0] - g_giv[k].iv[i][1]; if (gap < bg) { bg = gap; bi = i; } }
        g_giv[k].iv[bi][1] = g_giv[k].iv[bi + 1][1]; memmove(&g_giv[k].iv[bi + 1], &g_giv[k].iv[bi + 2], (GIV - bi - 2) * sizeof g_giv[k].iv[0]); g_giv[k].n--;
    }
    g_giv[k].iv[g_giv[k].n][0] = s0; g_giv[k].iv[g_giv[k].n][1] = s1; g_giv[k].n++;
    os_unfair_lock_unlock(&g_giv[k].lk);
}
static _Atomic uint64_t g_cur_frame;
// ---------- Same-process alternating measurement (DM_NVN_UB, off by default: without the environment variable the code below only adds one 'is it enabled' check) ----------
// On a hot real device the system keeps adjusting power / frequency, and segment comparison (segments A and B each run for a minute) cannot resolve differences of <10%; within the same run, toggle by phase in turn and take differences of adjacent phase pairs, so the drift cancels.
// DM_NVN_UB="name=item,item;name=item", one phase every DM_NVN_UB_N frames (default 180, minimum 60): even phases = baseline (nothing changed), odd phases take one category in turn. Each item of a category:
//   @set:<global variable name>=<value>   on entering the phase set this int global variable to the value, on leaving restore it (address found by symbol name; switches to be used on a real device must have a line added to the registration table below)
//   <fragment shader key prefix> or *   the phase drops all draws of these fragment shaders (the picture will be wrong; it only measures 'how much this kind of draw is worth'; a nearly idle category needs a round of its own)
// Readings: each phase discards its first 45 frames (transition + present lag); for the remaining frames, the mean of the actual present interval (same source as perf.txt) and the frame counts in the three buckets 16.7 / 25 / ≥33 ms, one log line "UB present".
static int g_ub_n = -1, g_ub_N = 180; static _Atomic int g_ub_cur = -1; static char g_ub_name[16][16], g_ub_keys[16][16][48]; static int g_ub_nk[16];
static _Atomic int g_ubp_on, g_ubp_c[4]; static _Atomic uint64_t g_ubp_sum_us;
void mtl_ub_present(double iv) {   // Called once per frame by the present thread: actual present interval in ms (−1 = dropped, 0 = no reading)
    if (!atomic_load(&g_ubp_on) || iv == 0) return;
    if (iv < 0) { atomic_fetch_add(&g_ubp_c[3], 1); return; }
    atomic_fetch_add(&g_ubp_c[iv <= 20.8 ? 0 : iv <= 29.2 ? 1 : 2], 1); atomic_fetch_add(&g_ubp_sum_us, (uint64_t)(iv * 1000));
}
// Registered runtime switches: name → address. Main-program symbols of an iOS release build are not in the dynamic symbol table and dlsym cannot find them (in the first real-device round all four categories were "global variable not found" and the whole round was wasted),
// so a switch to be measured on a real device must have a line added here; on Mac / in unit tests unregistered ones are still looked up by symbol name.
static int *ub_var(const char *nm) {
    extern int g_dm_present_link;
    static const struct { const char *n; int *p; } t[] = { {"g_dm_present_link", &g_dm_present_link} };
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) if (!strcmp(nm, t[i].n)) return t[i].p;
    return dlsym(RTLD_DEFAULT, nm);
}
static void ub_frame(uint64_t f) {
    if (g_ub_n < 0) { g_ub_n = 0; const char *e = getenv("DM_NVN_UB"), *n = getenv("DM_NVN_UB_N"); if (n && atoi(n) >= 60) g_ub_N = atoi(n);
        for (const char *q = e; q && *q && g_ub_n < 16; ) { const char *eq = strchr(q, '='), *end = strchr(q, ';'); if (!end) end = q + strlen(q); if (!eq || eq > end) break;
            int c = g_ub_n++; size_t l = (size_t)(eq - q); if (l > 15) l = 15; memcpy(g_ub_name[c], q, l);
            for (const char *k = eq + 1; k < end && g_ub_nk[c] < 16; ) { size_t kl = 0; while (k + kl < end && k[kl] != ',') kl++; size_t cl = kl > 47 ? 47 : kl; memcpy(g_ub_keys[c][g_ub_nk[c]++], k, cl); k += kl; if (k < end) k++; }
            q = *end ? end + 1 : end; }
        if (g_ub_n) MLOG("DM_NVN_UB: %d categories, %d frames per phase (alternating measurement in one process; phases that drop draws render incorrectly)", g_ub_n, g_ub_N); }
    if (!g_ub_n) return;
    uint64_t ph = f / (uint64_t)g_ub_N, r = f % (uint64_t)g_ub_N;
    if (r == 45) { for (int i = 0; i < 4; i++) atomic_store(&g_ubp_c[i], 0); atomic_store(&g_ubp_sum_us, 0); atomic_store(&g_ubp_on, 1); }
    if (r != 0) return;
    if (f && atomic_load(&g_ubp_on)) { atomic_store(&g_ubp_on, 0); uint64_t pp = ph - 1; int pc = (pp & 1) ? (int)((pp / 2) % (uint64_t)g_ub_n) : -1;
        int c0 = atomic_load(&g_ubp_c[0]), c1 = atomic_load(&g_ubp_c[1]), c2 = atomic_load(&g_ubp_c[2]), c3 = atomic_load(&g_ubp_c[3]), n_ = c0 + c1 + c2;
        MLOG("UB present %llu %s frames %d avg %.3f ms 2-tick %d 3-tick %d 4+tick %d dropped %d", (unsigned long long)pp, pc < 0 ? "base" : g_ub_name[pc], n_, n_ ? atomic_load(&g_ubp_sum_us) / 1000.0 / n_ : 0.0, c0, c1, c2, c3); }
    int nc = (ph & 1) ? (int)((ph / 2) % (uint64_t)g_ub_n) : -1;
    static int *sp[16]; static int sv[16], ns;   // Variables set by the previous phase: restore first
    for (int i = 0; i < ns; i++) *sp[i] = sv[i]; ns = 0;
    for (int i = 0; nc >= 0 && i < g_ub_nk[nc] && ns < 16; i++) { const char *k = g_ub_keys[nc][i]; if (strncmp(k, "@set:", 5)) continue;
        char nm[48]; const char *eq = strchr(k, '='); if (!eq) continue; size_t l = (size_t)(eq - (k + 5)); if (l >= sizeof nm) continue; memcpy(nm, k + 5, l); nm[l] = 0;
        int *a_ = ub_var(nm); static int bad, ok;
        if (!a_) { if (bad++ < 8) MLOG("[red] DM_NVN_UB: global variable not found: %s (on a real device it must first be registered in the ub_var table)", nm); continue; }
        sp[ns] = a_; sv[ns] = *a_; ns++; *a_ = atoi(eq + 1); if (ok++ < 8) MLOG("DM_NVN_UB: %s = %d (was %d)", nm, atoi(eq + 1), sv[ns - 1]); }
    atomic_store(&g_ub_cur, nc);
}
static int ub_skip(const char *key) {   // Whether this phase should drop draws of this fragment shader
    int uc = atomic_load(&g_ub_cur); if (uc < 0) return 0;
    for (int i = 0; i < g_ub_nk[uc]; i++) { const char *k = g_ub_keys[uc][i]; if (k[0] == '@') continue; if (k[0] == '*' || !strncmp(key, k, strlen(k))) return 1; }
    return 0;
}
static void us_report(uint64_t f); static void ren_report(uint64_t f); static void lay_report(uint64_t f); static void rc_report(uint64_t f);
void mtl_set_frame(uint64_t f) { atomic_store(&g_cur_frame, f); if (g_ub_n) ub_frame(f); us_report(f); ren_report(f); lay_report(f); rc_report(f); }
// GPU timing registration: gpu_tag_begin(frame) before commit, gpu_tag_done(frame, GPU start, GPU end) on completion (seconds on the CACurrentMediaTime base).
// Old-queue command buffers go through gpu_tag_f; the Metal 4 main submission backfills in the feedback of mtl_sub_commit (when switching to Metal 4 all the call sites were lost and the GPU column of the "timing" line became empty)
static void gpu_tag_begin(uint64_t f) {
    unsigned k = f % GRING;
    if (atomic_load(&g_gpu[k].frame) != f) { atomic_store(&g_gpu[k].frame, f); atomic_store(&g_gpu[k].ns, 0); atomic_store(&g_gpu[k].pending, 0); atomic_store(&g_gpu[k].t0, UINT64_MAX); atomic_store(&g_gpu[k].t1, 0); }
    atomic_fetch_add(&g_gpu[k].pending, 1);
}
static void gpu_tag_done(uint64_t f, double g0, double g1) {
    unsigned k = f % GRING;
    if (atomic_load(&g_gpu[k].frame) != f) return;
    double d = g1 - g0;
    if (d > 0) {
        atomic_fetch_add(&g_gpu[k].ns, (uint64_t)(d * 1e9));
        uint64_t s0 = (uint64_t)(g0 * 1e9), s1 = (uint64_t)(g1 * 1e9), v;
        giv_add(f, s0, s1);
        for (v = atomic_load(&g_gpu[k].t0); s0 < v && !atomic_compare_exchange_weak(&g_gpu[k].t0, &v, s0);) {}
        for (v = atomic_load(&g_gpu[k].t1); s1 > v && !atomic_compare_exchange_weak(&g_gpu[k].t1, &v, s1);) {}
    }
    atomic_fetch_sub(&g_gpu[k].pending, 1);
}
static void gpu_tag_f(id<MTLCommandBuffer> cb, uint64_t f) { gpu_tag_begin(f); [cb addCompletedHandler:^(id<MTLCommandBuffer> c) { gpu_tag_done(f, c.GPUStartTime, c.GPUEndTime); }]; }
void gpu_tag(id<MTLCommandBuffer> cb) { gpu_tag_f(cb, atomic_load(&g_cur_frame)); }
double mtl_gpu_ms(uint64_t f) {   // the union of the frame's command-buffer spans; -1 while one is still running or the frame is gone
    unsigned k = f % GRING;
    if (atomic_load(&g_gpu[k].frame) != f || atomic_load(&g_gpu[k].pending)) return -1;
    if (!atomic_load(&g_gpu[k].ns)) return 0;
    unsigned j = f % GIVR; os_unfair_lock_lock(&g_giv[j].lk);
    double ms = g_giv[j].frame == f ? giv_union_ns(g_giv[j].iv, &g_giv[j].n) / 1e6 : -1;
    os_unfair_lock_unlock(&g_giv[j].lk);
    return ms;
}
double mtl_gpu_sum_ms(uint64_t f) {   // the plain sum of the spans (what mtl_gpu_ms reported before 2026-10-19), for comparison with older numbers
    unsigned k = f % GRING;
    if (atomic_load(&g_gpu[k].frame) != f || atomic_load(&g_gpu[k].pending)) return -1;
    return atomic_load(&g_gpu[k].ns) / 1e6;
}
int mtl_gpu_span(uint64_t f, uint64_t *t0, uint64_t *t1) {
    unsigned k = f % GRING;
    if (atomic_load(&g_gpu[k].frame) != f || atomic_load(&g_gpu[k].pending)) return 0;
    *t0 = atomic_load(&g_gpu[k].t0); *t1 = atomic_load(&g_gpu[k].t1);
    return *t1 >= *t0 && *t1;
}
// DM_NVN_GPUWATCH=<first>-<last> (diagnostics): the uniform ranges a submission of those frames binds are copied at its commit and compared byte by
// byte when it completes: data the CPU changed while the GPU still had to read it (how Tears of the Kingdom's dive flicker was found; with uniform
// snapshots on, a changed range the GPU read from its copy is counted apart). Ranges written by the submission's own inline updates are left out,
// and a range also bound as a storage buffer is counted apart (a shader may have written it). The vertex and index ranges its draws read in place
// too (stage -1 / -2; after LASRecomp dc1d28f's LARECOMP_GPU_LAG_CHECK, MIT): what a GPU one frame behind would read changed (plan item 224)
typedef struct { uint64_t addr, size; int stage, index, ssbo, copied; } gw_rng;   // copied: a uniform snapshot of this submission held it when it was bound
static gw_rng *g_gw; static int g_gw_n, g_gw_cap; static uint64_t g_gw_hits, g_gw_checked;
static long g_gw_a = -2, g_gw_b;   // DM_NVN_GPUWATCH's frames (-1: off), read once
static int gw_on(void) { if (g_gw_a == -2) { const char *e = getenv("DM_NVN_GPUWATCH"); g_gw_a = e ? atol(e) : -1; g_gw_b = e && strchr(e, '-') ? atol(strchr(e, '-') + 1) : g_gw_a; }
    if (g_gw_a < 0) return 0;
    long f = (long)atomic_load(&g_cur_frame); return f >= g_gw_a && f <= g_gw_b; }
static void gw_note_(const p_bind_buf *b, int ssbo);
static inline __attribute__((always_inline)) void gw_note(const p_bind_buf *b, int ssbo) { if (g_gw_a != -1) gw_note_(b, ssbo); }   // (off: one compare a binding)
static void gw_note_(const p_bind_buf *b, int ssbo) {
    if (!b->addr || !b->size || !gw_on()) return;
    for (int i = g_gw_n - 1; i >= 0 && i >= g_gw_n - 64; i--) if (g_gw[i].addr == b->addr && g_gw[i].size == b->size) return;
    if (g_gw_n == g_gw_cap) { g_gw_cap = g_gw_cap ? g_gw_cap * 2 : 1024; g_gw = realloc(g_gw, (size_t)g_gw_cap * sizeof *g_gw); }
    g_gw[g_gw_n++] = (gw_rng){b->addr, b->size, b->stage, b->index, ssbo, !ssbo && us_find(b->addr, b->size)};
}
static void gw_commit(void) {   // before the commit blocks are copied: copy now, compare on completion
    if (!g_gw_n) return;
    int n = g_gw_n; gw_rng *l = malloc((size_t)n * sizeof *l); memcpy(l, g_gw, (size_t)n * sizeof *l); g_gw_n = 0;
    uint64_t frame = atomic_load(&g_cur_frame);
    {
        uint8_t **snap = calloc((size_t)n, sizeof *snap); int nsnap = 0;
        for (int i = 0; i < n; i++) {
            if (l[i].ssbo || l[i].size > (1u << 20)) continue;
            int upd = g_pend_over; for (int k = 0; k < g_npend && !upd; k++) upd = l[i].addr < g_pend_hi[k] && l[i].addr + l[i].size > g_pend_lo[k];
            if (upd) continue;
            snap[i] = malloc(l[i].size); memcpy(snap[i], (const void *)(uintptr_t)l[i].addr, l[i].size); nsnap++;
        }
        [g_commit_blocks addObject:^{
            int hit = 0, gpu_hit = 0, copied_hit = 0, vhit = 0, nv = 0;
            for (int i = 0; i < n; i++) nv += snap[i] && l[i].stage < 0;
            for (int i = 0; i < n; i++) { if (!snap[i]) continue; g_gw_checked++;
                if (memcmp(snap[i], (const void *)(uintptr_t)l[i].addr, l[i].size)) {
                    int gpu = 0; for (int j = 0; j < n && !gpu; j++) gpu = l[j].ssbo && l[i].addr < l[j].addr + l[j].size && l[i].addr + l[i].size > l[j].addr;
                    if (gpu) { gpu_hit++; continue; }   // also bound as a storage buffer in this submission: a shader may have written it
                    if (l[i].copied) { copied_hit++; continue; }   // the GPU read the snapshot taken at the submit call
                    if (l[i].stage < 0) vhit++; else hit++;
                    size_t off = 0; while (off < l[i].size && snap[i][off] == ((const uint8_t *)(uintptr_t)l[i].addr)[off]) off++;
                    if (g_gw_hits++ < 40) MLOG("[gpuwatch] frame %llu: %s st%d r%d [%#llx, +%llu) changed while this submission was on the GPU (first byte +%zu)", (unsigned long long)frame,
                                              l[i].stage == -1 ? "vertex" : l[i].stage == -2 ? "index" : "UBO", l[i].stage, l[i].index, (unsigned long long)l[i].addr, (unsigned long long)l[i].size, off); }
                free(snap[i]); }
            MLOG("[gpuwatch] frame %llu: %d of %d bound UBO ranges compared byte by byte, %d changed during the GPU work and were read from memory (+%d read from the submit-time copy, +%d also bound as storage buffers); vertex and index ranges %d, %d changed",
                 (unsigned long long)frame, nsnap - nv, n, hit, copied_hit, gpu_hit, nv, vhit);
            free(snap); free(l);
        }];
    }
}
