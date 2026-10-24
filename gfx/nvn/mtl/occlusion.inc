// gfx/nvn/mtl/occlusion.inc, part of nvn_mtl.m (included there in order, one translation unit): Occlusion queries (samples passed); the pending writes are gfx/nvn/core/pend.c (plan item 264)
// ---------- Occlusion queries (NVN_COUNTER_TYPE_SAMPLES_PASSED) ----------
// NVN semantics: samples passed is a cumulative counter on the queue - ResetCounter zeroes it, every draw afterwards adds to it, and ReportCounter writes out the cumulative value at that moment (+ timestamp) when the GPU completes.
// Metal: every render pass attaches a visibility result buffer; draws during counting use counting mode, with a new slot per pass (no reliance on accumulation across passes); a reset empties the 'slot table since the reset',
// and a report hands the slot table together with its buffer (retained) to the completion callback to sum and write back; after a report a new slot is used, so later draws are not counted into the reported value.
// (Previously always wrote 0: occlusion query = 'fully occluded', campfires / light sources got culled - the character select screen calls ResetCounter 21 times per frame)
// A count can span submissions (no reset between them): before a ring buffer is used again, the slots of the open count in it are added into
// g_vis_base, read once its submission has completed (the ring only hands out completed buffers); a report adds the base to its slots and a reset
// clears it. Without it both slot entries read the reused slot: 64 samples, then 32 in the next submission, reported 64 (lordran-native 3ef6915, MIT)
typedef struct { void *buf; uint32_t slot; } vis_slot;
typedef struct { uint64_t base; int n; vis_slot s[]; } vis_list;
#define VIS_SLOTS 8192
typedef struct { __strong id<MTLBuffer> buf; uint64_t fence; } vis_buf;
static vis_buf g_vis_ring[4]; static int g_vis_ring_n, g_vis_ring_i = -1;
static id<MTLBuffer> g_vis; static uint32_t g_vis_n;   // This submission's visibility buffer and number of slots used
static int vis_in_ring(id<MTLBuffer> b) { for (int i = 0; i < g_vis_ring_n; i++) if (g_vis_ring[i].buf == b) return 1; return 0; }
static int g_vis_on, g_vis_counting, g_vis_cur = -1; static uint64_t g_vis_cur_pass;
static vis_slot *g_vs; static int g_nvs, g_vscap;      // Slots since the last reset
static uint64_t g_vis_base;                              // Samples of slots since the last reset whose buffer was used again (see above)
static uint64_t g_vis_folded;                            // Slots added into g_vis_base (diagnostics, the unit test)
static uint64_t g_cnt_report[32], g_cnt_reset[32];       // Report / reset counts per counter type (diagnostics)
// DM_NVN_COUNTS=1 counters (see count_self_sample)
static int g_cnt_on = -1;
static uint64_t g_cnt_self, g_cnt_self_ov, g_cnt_self_dep, g_cnt_ts, g_cnt_ts_multi, g_cnt_cleartex, g_cnt_nodraw, g_cnt_pass_d0, g_cnt_bar_at, g_cnt_first_ts; static int g_cnt_sub_ts;
static uint64_t g_cnt_rp, g_cnt_rp_sub, g_cnt_rp_px, g_cnt_rp_out, g_cnt_rp_subc, g_cnt_rp_outc; static uint32_t g_cnt_ext_x, g_cnt_ext_y; static int g_cnt_ext_clr;   // item 46: the part of a pass its draws reach
__attribute__((weak)) void dm_watch_load(uint64_t addr, uint64_t len) { (void)addr; (void)len; }   // host/diag/diag.c (Mac)
__attribute__((weak)) void svc_guest_layout(uint8_t **alias, size_t *alias_n, uint8_t **stack, size_t *stack_n, uint8_t **heap, size_t *heap_n) {   // host/hle/hle_svc.c
    *alias = *stack = *heap = NULL; *alias_n = *stack_n = *heap_n = 0; }
static char g_cnt_why[200];   // the first overlap of the draw being counted
static int counts_on(void) { if (g_cnt_on < 0) g_cnt_on = getenv("DM_NVN_COUNTS") && strcmp(getenv("DM_NVN_COUNTS"), "0"); return g_cnt_on; }
void mtl_counts(uint64_t out[13]) { uint64_t v[13] = {g_cnt_self, g_cnt_self_ov, g_cnt_self_dep, g_cnt_ts, g_cnt_ts_multi, g_cnt_cleartex, g_cnt_nodraw, g_cnt_rp, g_cnt_rp_sub, g_cnt_rp_px, g_cnt_rp_out, g_cnt_rp_subc, g_cnt_rp_outc}; memcpy(out, v, sizeof v); }
static inline void cnt_extent(MTLScissorRect sc, double vx0, double vy0, double vx1, double vy1) {   // the right and bottom edges a draw can reach (scissor and viewport)
#ifdef DM_TEST_EXTENT_BROKEN   // negative control of t_extent only: the viewport left out
    vx0 = vx1 = (double)(sc.x + sc.width); vy0 = vy1 = (double)(sc.y + sc.height);
#endif
    double x1 = MIN((double)(sc.x + sc.width), MAX(vx0, vx1)), y1 = MIN((double)(sc.y + sc.height), MAX(vy0, vy1));
    if (x1 > g_cnt_ext_x) g_cnt_ext_x = (uint32_t)ceil(x1); if (y1 > g_cnt_ext_y) g_cnt_ext_y = (uint32_t)ceil(y1);
}
static void vis_clear(void) { for (int i = 0; i < g_nvs; i++) CFRelease(g_vs[i].buf); g_nvs = 0; g_vis_cur = -1; g_vis_base = 0; }
#ifndef DM_TEST_VIS_NO_FOLD   // negative control of t_visibility only: the slots stay listed and read the reused buffer
static void vis_recycle(id<MTLBuffer> b) {   // b's last submission has completed and b is about to be used again
    int keep = 0, n = 0;
    for (int i = 0; i < g_nvs; i++) {
        if (g_vs[i].buf == (__bridge void *)b) { g_vis_base += ((const uint64_t *)b.contents)[g_vs[i].slot]; CFRelease(g_vs[i].buf); n++; }
        else g_vs[keep++] = g_vs[i];
    }
    g_nvs = keep; g_vis_folded += (uint64_t)n;
    if (n) { static int once; if (!once++) MLOG("occlusion count across submissions: %d slots of a reused visibility buffer kept as %llu samples (logged once)", n, (unsigned long long)g_vis_base); }
}
#else
static void vis_recycle(id<MTLBuffer> b) { (void)b; }
#endif
static vis_list *vis_snapshot(void) {
    vis_list *l = malloc(sizeof *l + g_nvs * sizeof(vis_slot)); l->base = g_vis_base; l->n = g_nvs;
    for (int i = 0; i < g_nvs; i++) { l->s[i] = g_vs[i]; CFRetain(g_vs[i].buf); }
    return l;
}
static void pend_note(uint64_t dst, uint64_t n, const uint8_t *data, uint64_t src) { pend_add(dst, n, data, src, g_fence + 1); }   // pending writes: gfx/nvn/core/pend.c
