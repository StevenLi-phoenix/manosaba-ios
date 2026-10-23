// gfx/nvn/mtl/frame_timing.inc, part of nvn_mtl.m (included there in order, one translation unit): The frame number, GPU time tags of command buffers, DM_NVN_GPUWATCH; boot instrumentation, per-frame GPU time and DM_NVN_UB are gfx/nvn/core/frame.c (plan item 264)
static void ren_report(uint64_t f); static void lay_report(uint64_t f); static void rc_report(uint64_t f);
void mtl_set_frame(uint64_t f) { atomic_store(&g_cur_frame, f); if (g_ub.n) ub_frame(f); us_report(f); ren_report(f); lay_report(f); rc_report(f); }
static void gpu_tag_f(id<MTLCommandBuffer> cb, uint64_t f) { gpu_tag_begin(f); [cb addCompletedHandler:^(id<MTLCommandBuffer> c) { gpu_tag_done(f, c.GPUStartTime, c.GPUEndTime); }]; }
void gpu_tag(id<MTLCommandBuffer> cb) { gpu_tag_f(cb, atomic_load(&g_cur_frame)); }
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
            int upd = g_pend.over; for (int k = 0; k < g_pend.n && !upd; k++) upd = l[i].addr < g_pend.hi[k] && l[i].addr + l[i].size > g_pend.lo[k];
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
