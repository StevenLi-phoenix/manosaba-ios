// gfx/nvn/mtl/capture.inc, part of nvn_mtl.m (included there in order, one translation unit): The submit call's view for the translation thread (plan item 61) and the snapshot lookups on it
// ---------- The submit call's view, for the translation thread (plan item 61; MacNativeGames' mtl_sub_capture_ubos with TOTK_ASYNC_REPLAY, MIT, written anew) ----------
// With DM_NVN_XLAT=1 a submission is replayed later, on the translation thread (hle_nvn.c), while the game goes on. What the console reads when the
// queue takes the submission is read at the submit call, on the game's thread (mtl_sub_capture): the handles of its deferred binds and the bytes of
// the uniform ranges it binds (Tears of the Kingdom rewrites both as soon as the call returns; MacNativeGames found the constant pools rebuilt by
// other threads), and the syncs of its FenceSync records get the submission's token. The replay installs the handles into the records
// (mtl_sub_install) and its snapshot takes the captured bytes instead of memory (mtl_sub_snapshot). A range that a GPU write of a submission not
// complete at the capture reaches is not taken: its bytes may predate that write, so it is read on the GPU's timeline as a range with a running
// write always was
typedef struct { p_bind_handles *rec; int n; uint64_t h[32]; } cap_bh;
struct mtl_cap { uint64_t done, token; int us, nbh, cbh, nr; cap_bh *bh; us_rng *r; uint64_t *off; uint8_t *data; };
static pthread_mutex_t g_cap_mu = PTHREAD_MUTEX_INITIALIZER;   // captures on several game threads take turns (their scratch below)
static us_rng *g_cap_rng, *g_cap_tmp; static int g_cap_rn, g_cap_rcap, g_cap_tcap; static us_rng g_cap_seen[4096]; static uint32_t g_cap_cnt[6][2048];
static mtl_cap *g_cap_cur;   // the capture of the submission being replayed (mtl_sub_install .. mtl_sub_commit)
static int cap_seen(uint64_t lo, uint64_t hi) {
    uint64_t h = ((lo >> 4) ^ (hi << 7)) * 0x9E3779B97F4A7C15ull;
    for (unsigned k = 0, i = (unsigned)(h >> 52); k < 4; k++, i = (i + 1) & 4095) {
        us_rng *e = &g_cap_seen[i];
        if (e->lo == lo && e->hi == hi) return 1;
        if (!e->lo) { e->lo = lo; e->hi = hi; return 0; }
    }
    return 0;
}
static void cap_walk(mtl_cap *c, const uint8_t *p, int depth) {
    if (depth > 32) return;
    for (uint32_t guard = 0; guard < 4000000 && p; guard++) {
        op_hdr h; memcpy(&h, p, sizeof h);
        if (h.size < 8 || (h.size & 7) || h.op == OP_END) return;
        if (h.op == OP_JUMP) { const uint8_t *t; memcpy(&t, p + 8, 8); p = t; continue; }
        if (h.op == OP_CALL) { uint64_t t; memcpy(&t, p + 8, 8); cap_walk(c, (const uint8_t *)(uintptr_t)t, depth + 1); }
        else if (h.op == OP_BIND_TEX || h.op == OP_BIND_SAMP || h.op == OP_BIND_IMG) { p_bind_handles *b = (p_bind_handles *)(p + 8);
#ifndef DM_TEST_XLAT_NO_CAPTURE   // negative control of t_xlat only: deferred handles read at the replay
            if (b->defer && b->src && b->n > 0 && b->n <= 32) {
                if (c->nbh == c->cbh) { c->cbh = c->cbh ? c->cbh * 2 : 64; c->bh = realloc(c->bh, (size_t)c->cbh * sizeof *c->bh); if (!c->bh) MDIE("submission capture"); }
                cap_bh *e = &c->bh[c->nbh++]; e->rec = b; e->n = b->n; memcpy(e->h, (const void *)(uintptr_t)b->src, 8 * (size_t)b->n); }
#endif
        }
#ifndef DM_TEST_XLAT_NO_TOKEN   // negative control of t_xlat only: FenceSync records fence their syncs at the replay only
        else if (h.op == OP_FENCE_SYNC) { sync_t *sy; memcpy(&sy, p + 8, 8); if (sy) sync_token_store(sy, c->token); }
#endif
        else if (c->us && h.op == OP_BIND_UBO) { const p_bind_buf *b = (const p_bind_buf *)(p + 8);
            if (b->addr && b->size && b->size <= (1u << 20) && !cap_seen(b->addr, b->addr + b->size)) {
                if (g_cap_rn == g_cap_rcap) { g_cap_rcap = g_cap_rcap ? g_cap_rcap * 2 : 8192; g_cap_rng = realloc(g_cap_rng, (size_t)g_cap_rcap * sizeof *g_cap_rng); if (!g_cap_rng) MDIE("submission capture"); }
                g_cap_rng[g_cap_rn++] = (us_rng){b->addr, b->addr + b->size}; } }
        p += h.size;
    }
}
mtl_cap *mtl_sub_capture(const uint64_t *h, int n, uint64_t token) {   // the game's thread, in nvnQueueSubmitCommands
    mtl_cap *c = calloc(1, sizeof *c); if (!c) MDIE("submission capture");
    c->token = token; c->us = us_on();
#ifdef DM_TEST_XLAT_NO_CAPTURE
    c->us = 0;
#endif
    c->done = mtl_done_fence();   // before the bytes are read: a write completing meanwhile only makes them newer, and its range is left out anyway
    pthread_mutex_lock(&g_cap_mu);
    g_cap_rn = 0; memset(g_cap_seen, 0, sizeof g_cap_seen);
    for (int i = 0; i < n; i++) cap_walk(c, (const uint8_t *)(uintptr_t)h[i], 0);
    if (c->us && g_cap_rn) {
        us_rng_sort_x(g_cap_rng, g_cap_rn, &g_cap_tmp, &g_cap_tcap, g_cap_cnt);
        us_rng *u = g_cap_rng; int k = 0;   // merged as mtl_sub_snapshot merges
        for (int i = 1; i < g_cap_rn; i++) {
            void *mb; uint64_t mo, hi = u[i].hi > u[k].hi ? u[i].hi : u[k].hi;
            if (hi <= u[k].hi) continue;
            if (u[i].lo <= u[k].hi + 256 && hi - u[k].lo <= (1u << 20) && mtl_addr(u[k].lo, hi - u[k].lo, &mb, &mo)) u[k].hi = hi;
            else { us_rng r = u[i]; if (r.lo < u[k].hi) r.lo = u[k].hi; u[++k] = r; }
        }
        int nr = k + 1; uint64_t total = 0;
        c->r = malloc((size_t)nr * sizeof *c->r); c->off = malloc((size_t)nr * sizeof *c->off); if (!c->r || !c->off) MDIE("submission capture");
        for (int i = 0; i < nr; i++) { void *mb; uint64_t mo; c->r[c->nr] = u[i]; c->off[c->nr] = mtl_addr(u[i].lo, u[i].hi - u[i].lo, &mb, &mo) ? total : UINT64_MAX;   // not in one pool: read live at the replay
                                       if (c->off[c->nr] != UINT64_MAX) total += u[i].hi - u[i].lo; c->nr++; }
        c->data = malloc(total ? total : 1); if (!c->data) MDIE("submission capture");
        static dispatch_once_t hook; dispatch_once(&hook, ^{ dm_fault_hook_register(us_fault); });
        volatile int i = 0;   // one guard for the run (us_copy_range)
        for (;;) {
            atomic_fetch_add_explicit(&g_us_jumps, 1, memory_order_relaxed);
            if (!sigsetjmp(t_usg.jb, 1)) {
                for (; i < c->nr; i++) {
                    if (c->off[i] == UINT64_MAX) continue;
                    t_usg.lo = c->r[i].lo; t_usg.hi = c->r[i].hi; t_usg.armed = 1;
                    memcpy(c->data + c->off[i], (const void *)(uintptr_t)c->r[i].lo, (size_t)(c->r[i].hi - c->r[i].lo)); t_usg.armed = 0;
                }
                break;
            }
            c->off[i] = UINT64_MAX; atomic_fetch_add(&g_us_faults, 1); i++;   // not readable: read live at the replay
        }
    }
    pthread_mutex_unlock(&g_cap_mu);
    return c;
}
void mtl_cap_free(mtl_cap *c) { if (!c) return; free(c->bh); free(c->r); free(c->off); free(c->data); free(c); }
void mtl_sub_install(mtl_cap *c) {   // the translation thread, after mtl_sub_begin: the deferred handles of the submit call
    g_cap_cur = c;
    if (!c) return;
    for (int i = 0; i < c->nbh; i++) { memcpy(c->bh[i].rec->h, c->bh[i].h, 8 * (size_t)c->bh[i].n); c->bh[i].rec->defer = g_bh_gen; }
}
// Tests (t_xlat): the 8 bytes at an address in the next snapshot (seen 1), or no copy holds it (0)
static uint64_t g_test_us_at, g_test_us_val; static int g_test_us_hit = -1;
void mtl_test_us_watch(uint64_t a) { g_test_us_at = a; g_test_us_hit = -1; }
int mtl_test_us_seen(uint64_t *v) { *v = g_test_us_val; return g_test_us_hit; }
static const us_ent *us_find(uint64_t a, uint64_t n);
static void us_test_note(void) {
    if (!g_test_us_at) return;
    const us_ent *u = us_find(g_test_us_at, 8);
    if (u) memcpy(&g_test_us_val, u->cpu + (g_test_us_at - u->lo), 8);
    g_test_us_hit = u != NULL;
}
static void us_from_cap(const mtl_cap *c) {   // mtl_sub_snapshot with a capture: its bytes, less what a GPU write not complete at the capture reaches
    if (!us_on() || !c->nr) return;
    uint64_t t0 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    g_us_subs++;
    uint64_t done = mtl_done_fence(), keep = c->done < done ? c->done : done, gw_max = 0; int ng = 0;
#ifdef DM_TEST_XLAT_CAP_STALE   // negative control of t_xlat only: only the writes still running now count, as without a capture
    keep = done;
#endif
    for (int i = 0; i < g_us_gwn; i++) if (g_us_gw[i].fence > keep) { g_us_gw[ng++] = g_us_gw[i]; if (g_us_gw[i].hi - g_us_gw[i].lo > gw_max) gw_max = g_us_gw[i].hi - g_us_gw[i].lo; }
    g_us_gwn = ng;
    if (ng > 1) qsort(g_us_gw, (size_t)ng, sizeof *g_us_gw, us_gw_cmp);
    g_us_ranges += (uint64_t)c->nr;
    if (g_us_cap < c->nr) { g_us_cap = c->nr + 256; g_us = realloc(g_us, (size_t)g_us_cap * sizeof *g_us); }
    for (int i = 0; i < c->nr; i++) {
        void *mb; uint64_t mo, sz = c->r[i].hi - c->r[i].lo;
        if (c->off[i] == UINT64_MAX || !mtl_addr(c->r[i].lo, sz, &mb, &mo)) continue;
        if (ng) {
            uint64_t from = c->r[i].lo > gw_max ? c->r[i].lo - gw_max : 0; int a = 0, b = ng;
            while (a < b) { int m = (a + b) / 2; if (g_us_gw[m].lo < from) a = m + 1; else b = m; }
            int hit = 0; for (int j = a; j < ng && g_us_gw[j].lo < c->r[i].hi; j++) if (g_us_gw[j].hi > c->r[i].lo) { hit = 1; break; }
            if (hit) { g_us_skip_gpu++; continue; }
        }
        ARENA_OUT id<MTLBuffer> ab; uint64_t pad = c->r[i].lo & 255; size_t off = arena_alloc((size_t)(sz + pad), &ab) + (size_t)pad;
        us_ent *e = &g_us[g_us_n++];
        e->lo = c->r[i].lo; e->hi = c->r[i].hi; e->cpu = (uint8_t *)ab.contents + off; e->gpu = ab.gpuAddress + off; e->buf = (__bridge void *)ab; e->dropped = 0;
        memcpy(e->cpu, c->data + c->off[i], (size_t)sz);
        g_us_bytes += sz;
    }
    g_us_copies += (uint64_t)g_us_n;
    g_us_ns += clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - t0;
}
void mtl_sub_snapshot(const uint64_t *h, int n) {   // hle_nvn.c nvnQueueSubmitCommands, after mtl_sub_begin and before the replay
    g_us_n = 0;
    if (g_cap_cur) { us_from_cap(g_cap_cur); us_test_note(); return; }   // the translation thread's replay: the submit call's bytes and handles (above)
    if (!us_on() && g_bh_seen) for (int i = 0; i < n; i++) us_collect((const uint8_t *)(uintptr_t)h[i], 0, 0);
    if (!us_on() || !n) return;
    uint64_t t0 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    g_us_subs++;
    uint64_t done = mtl_done_fence(), gw_max = 0; int ng = 0;   // GPU writes of submissions not completed yet, sorted by start
    for (int i = 0; i < g_us_gwn; i++) if (g_us_gw[i].fence > done) { g_us_gw[ng++] = g_us_gw[i]; if (g_us_gw[i].hi - g_us_gw[i].lo > gw_max) gw_max = g_us_gw[i].hi - g_us_gw[i].lo; }
    g_us_gwn = ng;
    if (ng > 1) qsort(g_us_gw, (size_t)ng, sizeof *g_us_gw, us_gw_cmp);
    g_us_rn = 0; memset(g_us_seen, 0, sizeof g_us_seen);
    for (int i = 0; i < n; i++) us_collect((const uint8_t *)(uintptr_t)h[i], 0, 1);
    if (!g_us_rn) return;
    g_us_ranges += (uint64_t)g_us_rn;
    us_rng_sort(g_us_rng, g_us_rn);
    us_rng *u = g_us_rng; int k = 0;   // merge in place; the copies stay disjoint: a range overlapping a finished copy keeps only its part beyond it
    for (int i = 1; i < g_us_rn; i++) {
        void *mb; uint64_t mo, hi = u[i].hi > u[k].hi ? u[i].hi : u[k].hi;
        if (hi <= u[k].hi) continue;
        if (u[i].lo <= u[k].hi + 256 && hi - u[k].lo <= (1u << 20) && mtl_addr(u[k].lo, hi - u[k].lo, &mb, &mo)) u[k].hi = hi;
        else { us_rng r = u[i]; if (r.lo < u[k].hi) r.lo = u[k].hi; u[++k] = r; }
    }
    int nr = k + 1;
    if (g_us_cap < nr) { g_us_cap = nr + 256; g_us = realloc(g_us, (size_t)g_us_cap * sizeof *g_us); }
    for (int i = 0; i < nr; i++) {
        void *mb; uint64_t mo, sz = u[i].hi - u[i].lo;
        if (!mtl_addr(u[i].lo, sz, &mb, &mo)) continue;   // not in one pool: read live memory
        if (ng) {   // a GPU write still running reaches it: read on the GPU timeline
            uint64_t from = u[i].lo > gw_max ? u[i].lo - gw_max : 0; int a = 0, b = ng;
            while (a < b) { int m = (a + b) / 2; if (g_us_gw[m].lo < from) a = m + 1; else b = m; }
            int hit = 0; for (int j = a; j < ng && g_us_gw[j].lo < u[i].hi; j++) if (g_us_gw[j].hi > u[i].lo) { hit = 1; break; }
            if (hit) { g_us_skip_gpu++; continue; }
        }
        ARENA_OUT id<MTLBuffer> ab; uint64_t pad = u[i].lo & 255; size_t off = arena_alloc((size_t)(sz + pad), &ab) + (size_t)pad;   // same offset modulo 256 as the original
        us_ent *c = &g_us[g_us_n++];
        c->lo = u[i].lo; c->hi = u[i].hi; c->cpu = (uint8_t *)ab.contents + off; c->gpu = ab.gpuAddress + off; c->buf = (__bridge void *)ab; c->dropped = 0;
        g_us_bytes += sz;
    }
    uint64_t uf0 = atomic_load(&g_us_faults);
    us_copy_all();
    if (atomic_load(&g_us_faults) != uf0) { static int once; if (once++ < 8) MLOG("[red] uniform snapshot: %llu source range(s) not readable (unmapped after the pool was registered): those copies dropped, their draws read memory", (unsigned long long)(atomic_load(&g_us_faults) - uf0)); }
    g_us_copies += (uint64_t)g_us_n;
    g_us_ns += clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - t0;
}
static int g_us_last;   // the copy the last lookup found, tried first (plan item 225: consecutive draws bind the same blocks; ~13,000 lookups a frame)
static const us_ent *us_find(uint64_t a, uint64_t n) {   // the live copy holding all of [a, a + n), or NULL
    if (!g_us_n || !a) return NULL;
    if (!n) n = 1;
    if (g_us_last < g_us_n) { const us_ent *c = &g_us[g_us_last];   // the copies are disjoint: one holding the range is the only one
#ifndef DM_TEST_US_LAST_UNCHECKED   // negative control of t_usnap only: the last copy found taken whatever the range
        if (c->lo <= a && a + n <= c->hi) return c->dropped ? NULL : c;
#else
        return c->dropped ? NULL : c;
#endif
    }
    int lo = 0, hi = g_us_n;
    while (lo < hi) { int m = (lo + hi) / 2; if (g_us[m].hi <= a) lo = m + 1; else hi = m; }
    if (lo < g_us_n && g_us[lo].lo <= a && a + n <= g_us[lo].hi && !g_us[lo].dropped) { g_us_last = lo; return &g_us[lo]; }
    return NULL;
}
static void us_gw_note(uint64_t a, uint64_t n) {   // a GPU write of this submission reaches [a, a + n): the next submissions do not copy it until it completes
    int dup = 0;   // the same range written again and again (a storage buffer bound for many draws): noted once
    for (int j = g_us_gwn - 1; j >= 0 && j >= g_us_gwn - 8; j--) if (g_us_gw[j].lo == a && g_us_gw[j].hi == a + n && g_us_gw[j].fence == g_fence + 1) { dup = 1; break; }
    if (!dup) { if (g_us_gwn == g_us_gwcap) { g_us_gwcap = g_us_gwcap ? g_us_gwcap * 2 : 4096; g_us_gw = realloc(g_us_gw, (size_t)g_us_gwcap * sizeof *g_us_gw); }
                g_us_gw[g_us_gwn++] = (us_gw){a, a + n, g_fence + 1}; }
}
static void us_drop(uint64_t a, uint64_t n) {   // ... and its copies are dropped from then on: later readers see memory
    if (!n || !us_on()) return;
    us_gw_note(a, n);
    if (!g_us_n) return;
    int lo = 0, hi = g_us_n;
    while (lo < hi) { int m = (lo + hi) / 2; if (g_us[m].hi <= a) lo = m + 1; else hi = m; }
    int any = 0;
    for (int i = lo; i < g_us_n && g_us[i].lo < a + n; i++) if (!g_us[i].dropped) { g_us[i].dropped = 1; g_us_drops++; any = 1; }
    if (any) args_dirty_all();   // bindings encoded from here on read memory
}
static void us_report(uint64_t f) {   // every 300 frames
    static uint64_t p[7]; uint64_t c[7] = {g_us_subs, g_us_ranges, g_us_copies, g_us_bytes, g_us_drops, g_us_skip_gpu, g_us_ns};
    if (f % 300 || c[0] == p[0]) return;
    double ns = (double)(c[0] - p[0]);
    MLOG("uniform snapshots up to frame %llu: %llu submissions, per submission %.0f bindings in %.0f copies, %.1f KB, %.3f ms; %llu copies dropped by a GPU write of their submission, %llu ranges not copied (a GPU write of an earlier submission still running)",
         (unsigned long long)f, (unsigned long long)(c[0] - p[0]), (c[1] - p[1]) / ns, (c[2] - p[2]) / ns, (c[3] - p[3]) / ns / 1024, (c[6] - p[6]) / ns / 1e6,
         (unsigned long long)(c[4] - p[4]), (unsigned long long)(c[5] - p[5]));
    memcpy(p, c, sizeof p);
}
// Bytes [a, a + n) as the GPU will see them at this point of the replay: guest memory with the pending writes recorded before entry `upto` applied in
// order: an inline update's bytes, or what a GPU copy reads from its source as of that copy (MacNativeGames' pend_read, MIT). 0 when that is not known
// (a copy without a source, the table overflowed, copies nested too deep)
static int pend_read_x(uint64_t a, void *out, uint64_t n, int upto, int depth) {
    memcpy(out, (const void *)(uintptr_t)a, n);
    if (g_pend_over) return 0;
    for (int i = 0; i < upto; i++) {
        if (a >= g_pend_hi[i] || a + n <= g_pend_lo[i]) continue;
        uint64_t lo = MAX(a, g_pend_lo[i]), hi = MIN(a + n, g_pend_hi[i]);
        if (g_pend_data[i]) memcpy((uint8_t *)out + (lo - a), g_pend_data[i] + (lo - g_pend_lo[i]), hi - lo);
        else if (!g_pend_src[i] || depth >= 4 || !pend_read_x(g_pend_src[i] + (lo - g_pend_lo[i]), (uint8_t *)out + (lo - a), hi - lo, i, depth + 1)) return 0;
    }
    return 1;
}
static int g_nupd, g_upd_flushing;   // renamed inline uniform updates not yet in memory (see upd_log)
static void upd_apply(uint64_t lo, uint8_t *out, uint64_t n);
static int upd_any(void);   // the submission logged inline uniform updates (written to memory or not): a snapshot alone predates them
static int pend_read(uint64_t a, void *out, uint64_t n) {
    { const us_ent *u = us_find(a, n); if (u) { memcpy(out, u->cpu + (a - u->lo), n); if (upd_any()) upd_apply(a, out, n); return 1; } }   // not dropped: no GPU write of this submission reached it but renamed updates
    return pend_read_x(a, out, n, g_npend, 0);
}
typedef struct { uint64_t ser; uint16_t level, slice; } zskip_zero;
static zskip_zero g_zskip_zero[4096]; static int g_zskip_n, g_zskip_count;
static zskip_zero g_zskip_cand[8]; static uint8_t g_zskip_cand_ok[8];
static void zskip_clear(void);
static int tbdr_on(void);
enum { TBP_ZSKIP = 1, TBP_XV = 2, TBP_COVER = 4, TBP_STORE = 8, TBP_SDX = 16, TBP_DEFER = 32, TBP_FUSE = 64, TBP_HOIST = 128, TBP_DS = 256, TBP_RANGE = 512, TBP_CONT = 1024, TBP_RTHEAP = 2048 };
static int tbdr_part(int bit);   // DM_NVN_TBDR_OFF (plan item 62j)
static id g_cb;
static BENC g_blit;
static NSMutableArray<dispatch_block_t> *g_commit_blocks;
static uint64_t g_alloc_fence[ALLOC_MAX]; static uint64_t g_alloc_waits, g_alloc_grown;
int mtl_wait(uint64_t fence, uint64_t timeout_ns);
static void up_flush(void);
