// gfx/nvn/mtl/occlusion.inc, part of nvn_mtl.m (included there in order, one translation unit): Occlusion queries (samples passed) and the report writes still pending
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
__attribute__((weak)) void dm_watch_load(uint64_t addr, uint64_t len) { (void)addr; (void)len; }   // host/diag.c (Mac)
__attribute__((weak)) void svc_guest_layout(uint8_t **alias, size_t *alias_n, uint8_t **stack, size_t *stack_n, uint8_t **heap, size_t *heap_n) {   // host/hle_svc.c
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
// Target ranges of buffer writes (copies / inline uniform updates) encoded but not yet executed on the GPU, in this submission and in earlier ones
// still running: reading these addresses on the CPU side at encode time yields old values. Entries stay until their submission completes (pend_begin):
// the acquire sync no longer holds the CPU until the previous frame has run, and Tears of the Kingdom updates ring-buffer uniform blocks inline one
// submission before the draws that read storage buffer pointers out of them (MacNativeGames' pend_begin, 16384 entries; MIT)
// g_pend_data: an inline update's bytes (its copy in the update ring, alive until its submission completes); NULL for a GPU copy, whose source is
// g_pend_src (0: not known); g_pend_fence: the fence of the entry's submission; g_pend_over_fence: a submission whose writes did not fit
#define PEND_MAX 16384
static uint64_t g_pend_lo[PEND_MAX], g_pend_hi[PEND_MAX], g_pend_src[PEND_MAX], g_pend_fence[PEND_MAX], g_pend_over_fence; static const uint8_t *g_pend_data[PEND_MAX]; static int g_npend, g_pend_over;
static int pend_hits(uint64_t a, uint64_t n) { if (g_pend_over) return 1; for (int i = 0; i < g_npend; i++) if (a < g_pend_hi[i] && a + n > g_pend_lo[i]) return 1; return 0; }
static void pend_note(uint64_t dst, uint64_t n, const uint8_t *data, uint64_t src) {
    if (g_npend < PEND_MAX) { g_pend_lo[g_npend] = dst; g_pend_hi[g_npend] = dst + n; g_pend_data[g_npend] = data; g_pend_src[g_npend] = src; g_pend_fence[g_npend] = g_fence + 1; g_npend++; }
    else { g_pend_over = 1; g_pend_over_fence = g_fence + 1; }
}
static void pend_begin(uint64_t done) {   // submission start: the writes of submissions that have completed are in memory now
    int n = 0;
    for (int i = 0; i < g_npend; i++) if (g_pend_fence[i] > done) {
        g_pend_lo[n] = g_pend_lo[i]; g_pend_hi[n] = g_pend_hi[i]; g_pend_data[n] = g_pend_data[i]; g_pend_src[n] = g_pend_src[i]; g_pend_fence[n] = g_pend_fence[i]; n++; }
    g_npend = n; g_pend_over = g_pend_over_fence > done;
}
