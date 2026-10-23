// gfx/nvn/mtl/passgpu.m: GPU time of each render pass and dispatch (DM_NVN_PASSGPU, plan item 49), a translation unit of the Metal backend of its own (plan item 264; what others use of it: mtl/mtl.h)
#include "mtl.h"
// ---------- DM_NVN_PASSGPU=<file>: GPU time of each render pass and dispatch (plan item 49) ----------
// After upstream 44a6770 (jhaizhou-ops/d2r-mobile, MIT), written for this replay. Metal 4 writes timestamps into an MTL4CounterHeap: after the
// render encoder opens (before the vertex stage of its first draw) and before it ends (after the fragment stage of its draws), and around each
// dispatch (precise: dispatches share the copy encoder); Metal 3 samples at stage boundaries through the pass descriptors. A heap per submission in
// a ring of 8, cleared before reuse; the submission's feedback resolves it. One row per pass: frame idx kind gpu_ms inc_ms w h fmt0 dfmt load
// draws t0_ns t1_ns pass (kind R render / C dispatch; load: L load / C clear per colour attachment, | then depth; pass: the number in
// DM_NVN_PASSDUMP's lines). Passes overlap on the GPU (a pass's vertex stage, the start Metal 3 samples, runs during the previous pass's
// fragments: a light pass after a heavy one spans 13 ms on Metal 3, 0.01 ms on Metal 4), so gpu_ms (start to end) does not add up; inc_ms =
// end − max(start, the latest end before it) does, to the time the GPU was busy. Every 300 frames a line gives the passes' GPU time a frame and
// the five kinds of pass (size, formats, loads) that took the most of it (a pass's index in the frame is no identity: Tears of the Kingdom's
// frames have 100-260 passes; summed by index they came to 13 ms a frame against 6.4 ms of increments).
typedef struct { uint64_t frame, pass; int idx, kind, w, h, draws, slot; uint32_t cfmt0, dfmt; char load[12]; } pg_pass;
#define PG_SLOTS 4096
#define PG_RING 8
static int g_pgt = -1; static FILE *g_pgt_f;
// Which submissions are timed (plan item 256): DM_NVN_PASSGPU_EVERY=<n> every nth only (shh632058099 9ecf169, MIT; the timestamps' own cost
// spread thinner), DM_NVN_PASSGPU_FROM=<frame> none before that frame and DM_NVN_PASSGPU_FRAMES=<n> none from n frames after it (GirlNet 9dc64f9,
// MIT: on the iPhone the first minute runs on burst power, 230). A submission left out takes no heap and writes no row
static uint64_t g_pgt_every = 1, g_pgt_frames; uint64_t g_pgt_from;
static inline int pg_sampled(uint64_t sub, uint64_t frame, uint64_t every, uint64_t from, uint64_t frames) {
#ifdef DM_TEST_PG_SAMPLE_ALL   // negative control of t_passgpu only: every submission timed, as before
    (void)sub; (void)frame; (void)every; (void)from; (void)frames; return 1;
#endif
    return (every <= 1 || sub % every == 0) && frame >= from && (!frames || frame < from + frames);
}
#if DM_NVN_M3
static id<MTLCounterSampleBuffer> g_pgt_buf[PG_RING];
#define PG_RSLOTS 4   // vertex start / end, fragment start / end
#else
static id<MTL4CounterHeap> g_pgt_buf[PG_RING]; static double g_pgt_ns_tick = 1;
#define PG_RSLOTS 2
#endif
static uint32_t g_pgt_ring_used[PG_RING];
static int g_pgt_b; static uint32_t g_pgt_used;
static pg_pass *g_pgt_pass; static int g_pgt_np, g_pgt_cap, g_pgt_open = -1;
static uint64_t g_pgt_frame = ~0ull; static int g_pgt_idx;
static pthread_mutex_t g_pgt_mu = PTHREAD_MUTEX_INITIALIZER;
typedef struct { int kind, w, h; uint32_t cfmt0, dfmt; char load[12]; double sum; uint64_t n; } pg_sig;   // one kind of pass in the window
static pg_sig g_pgt_sig[512]; static int g_pgt_nsig; static uint64_t g_pgt_rep_frame, g_pgt_win_last = ~0ull, g_pgt_win_frames, g_pgt_win_n; static double g_pgt_win_ms;
static void pg_window(const pg_pass *p, double inc) {   // (under g_pgt_mu) one row into the window; every 300 frames its line
    if (p->frame != g_pgt_win_last) { g_pgt_win_last = p->frame; g_pgt_win_frames++; }
    g_pgt_win_ms += inc; g_pgt_win_n++;
    int k = 0; while (k < g_pgt_nsig && !(g_pgt_sig[k].kind == p->kind && g_pgt_sig[k].w == p->w && g_pgt_sig[k].h == p->h && g_pgt_sig[k].cfmt0 == p->cfmt0 && g_pgt_sig[k].dfmt == p->dfmt && !strcmp(g_pgt_sig[k].load, p->load))) k++;
    if (k == g_pgt_nsig && k < 512) { g_pgt_sig[k] = (pg_sig){p->kind, p->w, p->h, p->cfmt0, p->dfmt, "", 0, 0}; memcpy(g_pgt_sig[k].load, p->load, sizeof p->load); g_pgt_nsig++; }
    if (k < g_pgt_nsig) { g_pgt_sig[k].sum += inc; g_pgt_sig[k].n++; }
    if (p->frame < g_pgt_rep_frame + 300 || g_pgt_win_frames < 2) return;
    double nf = (double)(g_pgt_win_frames - 1);   // (the frame now starting is not whole yet)
    int top[5] = {-1, -1, -1, -1, -1};
    for (int i = 0; i < g_pgt_nsig; i++) for (int j = 0; j < 5; j++) if (top[j] < 0 || g_pgt_sig[i].sum > g_pgt_sig[top[j]].sum) { for (int q = 4; q > j; q--) top[q] = top[q - 1]; top[j] = i; break; }
    char line[1024]; size_t o = 0;
    for (int j = 0; j < 5 && top[j] >= 0 && o < sizeof line; j++) { pg_sig *g = &g_pgt_sig[top[j]];
        o += (size_t)snprintf(line + o, sizeof line - o, "%s %c %dx%d fmt %u/%u %s %.2f ms (%.1f a frame)", j ? ";" : "", g->kind ? 'C' : 'R', g->w, g->h, g->cfmt0, g->dfmt,
                              g->kind ? "-" : g->load, g->sum / nf, g->n / nf); }
    MLOG("[passgpu] up to frame %llu: passes and dispatches %.2f ms of GPU time a frame (%.0f of them); most:%s", (unsigned long long)p->frame, g_pgt_win_ms / nf, g_pgt_win_n / nf, line);
    g_pgt_rep_frame = p->frame; g_pgt_nsig = 0; g_pgt_win_frames = 1; g_pgt_win_ms = 0; g_pgt_win_n = 0; g_pgt_win_last = p->frame;
}
static int pg_on(void) {
    if (g_pgt >= 0) return g_pgt;
    const char *e = getenv("DM_NVN_PASSGPU"); g_pgt = 0;
    if (!e || !*e) return 0;
#if DM_NVN_M3
    if (![g_dev supportsCounterSampling:MTLCounterSamplingPointAtStageBoundary]) { MLOG("[red] DM_NVN_PASSGPU: no counter sampling at stage boundaries on this GPU"); return 0; }
    id<MTLCounterSet> ts = nil; for (id<MTLCounterSet> c in g_dev.counterSets) if ([c.name isEqualToString:MTLCommonCounterSetTimestamp]) ts = c;
    if (!ts) { MLOG("[red] DM_NVN_PASSGPU: no timestamp counter set"); return 0; }
    MTLCounterSampleBufferDescriptor *d = [MTLCounterSampleBufferDescriptor new]; d.counterSet = ts; d.storageMode = MTLStorageModeShared; d.sampleCount = PG_SLOTS;
    for (int i = 0; i < PG_RING; i++) { NSError *er = nil; g_pgt_buf[i] = [g_dev newCounterSampleBufferWithDescriptor:d error:&er]; if (!g_pgt_buf[i]) { MLOG("[red] DM_NVN_PASSGPU: no counter sample buffer: %s", er.localizedDescription.UTF8String ?: "?"); return 0; } }
#else
    MTL4CounterHeapDescriptor *hd = [MTL4CounterHeapDescriptor new]; hd.type = MTL4CounterHeapTypeTimestamp; hd.count = PG_SLOTS;
    for (int i = 0; i < PG_RING; i++) { NSError *er = nil; g_pgt_buf[i] = [g_dev newCounterHeapWithDescriptor:hd error:&er]; if (!g_pgt_buf[i]) { MLOG("[red] DM_NVN_PASSGPU: no timestamp counter heap: %s", er.localizedDescription.UTF8String ?: "?"); return 0; } }
    { uint64_t fq = [g_dev queryTimestampFrequency]; g_pgt_ns_tick = fq ? 1e9 / (double)fq : 1; }
#endif
    { const char *v; if ((v = getenv("DM_NVN_PASSGPU_EVERY")) && atoll(v) > 1) g_pgt_every = (uint64_t)atoll(v);
      if ((v = getenv("DM_NVN_PASSGPU_FROM"))) g_pgt_from = (uint64_t)atoll(v); if ((v = getenv("DM_NVN_PASSGPU_FRAMES"))) g_pgt_frames = (uint64_t)atoll(v);
      if (g_pgt_every > 1 || g_pgt_from || g_pgt_frames) MLOG("DM_NVN_PASSGPU: every %llu submission(s), from frame %llu%s", (unsigned long long)g_pgt_every, (unsigned long long)g_pgt_from,
                                                                g_pgt_frames ? [NSString stringWithFormat:@" for %llu frames", (unsigned long long)g_pgt_frames].UTF8String : ""); }
    if (!(g_pgt_f = fopen(e, "w"))) { MLOG("[red] DM_NVN_PASSGPU: cannot open %s", e); return 0; }
    fprintf(g_pgt_f, "frame\tidx\tkind\tgpu_ms\tinc_ms\tw\th\tfmt0\tdfmt\tload\tdraws\tt0_ns\tt1_ns\tpass\n"); fflush(g_pgt_f);
    MLOG("DM_NVN_PASSGPU: the GPU time of each render pass and dispatch → %s", e);
    return g_pgt = 1;
}
void pg_sub_begin(uint64_t fence) {   // this submission takes heap fence % PG_RING, once the one that used it last has completed
    if (!pg_on()) return;
    if (!pg_sampled(fence, atomic_load(&g_cur_frame), g_pgt_every, g_pgt_from, g_pgt_frames)) { g_pgt_np = 0; g_pgt_open = -1; g_pgt_used = PG_SLOTS; return; }   // (pg_new: no slot left)
    if (fence > PG_RING) mtl_wait(fence - PG_RING, UINT64_MAX);
    g_pgt_b = (int)(fence % PG_RING); g_pgt_used = 0; g_pgt_np = 0; g_pgt_open = -1;
#if !DM_NVN_M3
    if (g_pgt_ring_used[g_pgt_b]) [g_pgt_buf[g_pgt_b] invalidateCounterRange:NSMakeRange(0, g_pgt_ring_used[g_pgt_b])];   // (a slot left unwritten reads 0, not an old time)
#endif
}
static pg_pass *pg_new(int kind, int nslot) {
    if (!pg_on() || g_pgt_used + (uint32_t)nslot > PG_SLOTS) return NULL;
    uint64_t f = atomic_load(&g_cur_frame); if (f != g_pgt_frame) { g_pgt_frame = f; g_pgt_idx = 0; }
    if (g_pgt_np == g_pgt_cap) { g_pgt_cap = g_pgt_cap ? g_pgt_cap * 2 : 128; g_pgt_pass = realloc(g_pgt_pass, (size_t)g_pgt_cap * sizeof *g_pgt_pass); }
    pg_pass *p = &g_pgt_pass[g_pgt_np++]; memset(p, 0, sizeof *p);
    p->frame = f; p->pass = g_stat_pass; p->idx = g_pgt_idx++; p->kind = kind; p->slot = (int)g_pgt_used; p->draws = -(int)g_stat_draw;
    g_pgt_used += (uint32_t)nslot;
    return p;
}
void pg_render(RPD *pd, NSUInteger nc) {   // before the render encoder opens: the pass's row (and on Metal 3 its sample points)
    if (!g_pgt) return;
    pg_pass *p = pg_new(0, PG_RSLOTS); if (!p) return;
    p->pass = g_stat_pass + 1;
#if DM_NVN_M3
    MTLRenderPassSampleBufferAttachmentDescriptor *a = pd.sampleBufferAttachments[0];
    a.sampleBuffer = g_pgt_buf[g_pgt_b];
    a.startOfVertexSampleIndex = (NSUInteger)p->slot; a.endOfVertexSampleIndex = (NSUInteger)p->slot + 1; a.startOfFragmentSampleIndex = (NSUInteger)p->slot + 2; a.endOfFragmentSampleIndex = (NSUInteger)p->slot + 3;
#ifdef DM_TEST_PG_NO_END
    a.endOfVertexSampleIndex = a.endOfFragmentSampleIndex = MTLCounterDontSample;
#endif
#endif
    id<MTLTexture> t0 = nil; int k = 0;
    for (NSUInteger i = 0; i < nc && i < 8; i++) { id<MTLTexture> t = pd.colorAttachments[i].texture; if (!t) continue;
        if (!t0) t0 = t; if (k < 9) p->load[k++] = pd.colorAttachments[i].loadAction == MTLLoadActionClear ? 'C' : 'L'; }
    id<MTLTexture> dt = pd.depthAttachment.texture;
    if (dt) { if (k < 10) { p->load[k++] = '|'; p->load[k++] = pd.depthAttachment.loadAction == MTLLoadActionClear ? 'C' : 'L'; } p->dfmt = (uint32_t)dt.pixelFormat; }
    if (!k) p->load[k++] = '-';
    NSUInteger pw, ph; pass_dims(pd, nc, &pw, &ph);
    p->w = (int)pw; p->h = (int)ph; p->cfmt0 = t0 ? (uint32_t)t0.pixelFormat : 0;
    g_pgt_open = g_pgt_np - 1;
}
void pg_render_begin(id e) {   // the encoder just opened: the pass's start
#if !DM_NVN_M3
    if (g_pgt_open >= 0 && [e conformsToProtocol:@protocol(MTL4RenderCommandEncoder)])
        [(id<MTL4RenderCommandEncoder>)e writeTimestampWithGranularity:MTL4TimestampGranularityRelaxed afterStage:MTLRenderStageVertex intoHeap:g_pgt_buf[g_pgt_b] atIndex:(NSUInteger)g_pgt_pass[g_pgt_open].slot];
#else
    (void)e;
#endif
}
void pg_render_end(id e) {   // before the encoder ends: the pass's end and its draws
    if (g_pgt_open < 0) return;
#if !DM_NVN_M3 && !defined(DM_TEST_PG_NO_END)   // (negative control of t_passgpu: no end written)
    if ([e conformsToProtocol:@protocol(MTL4RenderCommandEncoder)])
        [(id<MTL4RenderCommandEncoder>)e writeTimestampWithGranularity:MTL4TimestampGranularityRelaxed afterStage:MTLRenderStageFragment intoHeap:g_pgt_buf[g_pgt_b] atIndex:(NSUInteger)g_pgt_pass[g_pgt_open].slot + 1];
#else
    (void)e;
#endif
    g_pgt_pass[g_pgt_open].draws += (int)g_stat_draw; g_pgt_open = -1;
}
#if DM_NVN_M3
MTLComputePassDescriptor *pg_compute_desc(void) {   // a dispatch's own encoder, sampled at its start and end
    pg_pass *p = g_pgt ? pg_new(1, 2) : NULL; if (!p) return nil;
    p->draws = 0;
    MTLComputePassDescriptor *cd = [MTLComputePassDescriptor computePassDescriptor];
    cd.sampleBufferAttachments[0].sampleBuffer = g_pgt_buf[g_pgt_b];
    cd.sampleBufferAttachments[0].startOfEncoderSampleIndex = (NSUInteger)p->slot;
#ifndef DM_TEST_PG_NO_END
    cd.sampleBufferAttachments[0].endOfEncoderSampleIndex = (NSUInteger)p->slot + 1;
#else
    cd.sampleBufferAttachments[0].endOfEncoderSampleIndex = MTLCounterDontSample;
#endif
    return cd;
}
#else
int pg_dispatch_begin(CENC ce) {   // a dispatch in the shared encoder: precise timestamps around it
    pg_pass *p = g_pgt ? pg_new(1, 2) : NULL; if (!p) return -1;
    p->draws = 0;
    [ce writeTimestampWithGranularity:MTL4TimestampGranularityPrecise intoHeap:g_pgt_buf[g_pgt_b] atIndex:(NSUInteger)p->slot];
    return p->slot + 1;
}
void pg_dispatch_end(CENC ce, int slot) {
#ifndef DM_TEST_PG_NO_END
    if (slot >= 0) [ce writeTimestampWithGranularity:MTL4TimestampGranularityPrecise intoHeap:g_pgt_buf[g_pgt_b] atIndex:(NSUInteger)slot];
#else
    (void)ce; (void)slot;
#endif
}
#endif
void pg_commit(id fb) {   // the submission's rows go to its feedback (fb = FB_ON(o, g_cb)), which resolves the heap
    if (g_pgt <= 0 || !g_pgt_np) return;
    int n = g_pgt_np; pg_pass *ps = malloc((size_t)n * sizeof *ps); memcpy(ps, g_pgt_pass, (size_t)n * sizeof *ps);
    __typeof__(g_pgt_buf[0]) buf = g_pgt_buf[g_pgt_b]; uint32_t used = g_pgt_used; g_pgt_ring_used[g_pgt_b] = used;
#if !DM_NVN_M3
    double ns_tick = g_pgt_ns_tick;
#endif
    [fb FB_SEL:^(FB_ARG c) {
        (void)c;
        NSData *d = [buf resolveCounterRange:NSMakeRange(0, used)];
        uint64_t *t = d ? calloc(used ? used : 1, sizeof *t) : NULL;   // each sample point's time in ns, 0 = not written
#if DM_NVN_M3
        const MTLCounterResultTimestamp *r = d.bytes;
        for (uint32_t k = 0; t && k < used && (k + 1) * sizeof *r <= d.length; k++) t[k] = r[k].timestamp == MTLCounterErrorValue ? 0 : r[k].timestamp;
#else
        const MTL4TimestampHeapEntry *r = d.bytes;
        for (uint32_t k = 0; t && k < used && (k + 1) * sizeof *r <= d.length; k++) t[k] = (uint64_t)(r[k].timestamp * ns_tick);
#endif
        pthread_mutex_lock(&g_pgt_mu);
        for (int i = 0; t && i < n; i++) {
            pg_pass *p = &ps[i]; uint64_t a, b;
#if DM_NVN_M3
            if (p->kind == 0) { a = t[p->slot] ? t[p->slot] : t[p->slot + 2]; b = t[p->slot + 3] ? t[p->slot + 3] : t[p->slot + 1]; }
            else { a = t[p->slot]; b = t[p->slot + 1]; }
#else
            a = t[p->slot]; b = t[p->slot + 1];
#endif
            if (!a || !b || b < a) continue;
            static uint64_t prev_end;   // the latest end so far (feedback runs in submission order)
            double ms = (b - a) / 1e6, inc = b > prev_end ? (b - (a > prev_end ? a : prev_end)) / 1e6 : 0;
            if (b > prev_end) prev_end = b;
            fprintf(g_pgt_f, "%llu\t%d\t%c\t%.4f\t%.4f\t%d\t%d\t%u\t%u\t%s\t%d\t%llu\t%llu\t%llu\n", (unsigned long long)p->frame, p->idx, p->kind ? 'C' : 'R', ms, inc,
                    p->w, p->h, p->cfmt0, p->dfmt, p->kind ? "-" : p->load, p->draws, (unsigned long long)a, (unsigned long long)b, (unsigned long long)p->pass);
            pg_window(p, inc);
        }
        fflush(g_pgt_f);
        pthread_mutex_unlock(&g_pgt_mu);
        free(t); free(ps);
    }];
}
#pragma clang attribute pop
