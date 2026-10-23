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
        us_rng *u = g_cap_rng; int nr = us_merge(u, g_cap_rn); uint64_t total = 0;   // merged as mtl_sub_snapshot merges
        c->r = malloc((size_t)nr * sizeof *c->r); c->off = malloc((size_t)nr * sizeof *c->off); if (!c->r || !c->off) MDIE("submission capture");
        for (int i = 0; i < nr; i++) { void *mb; uint64_t mo; c->r[c->nr] = u[i]; c->off[c->nr] = mtl_addr(u[i].lo, u[i].hi - u[i].lo, &mb, &mo) ? total : UINT64_MAX;   // not in one pool: read live at the replay
                                       if (c->off[c->nr] != UINT64_MAX) total += u[i].hi - u[i].lo; c->nr++; }
        c->data = malloc(total ? total : 1); if (!c->data) MDIE("submission capture");
        us_cap_copy(c->nr, c->r, c->off, c->data);   // one guard for the run; a range not readable is read live at the replay
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
static void us_test_note(void) {
    if (!g_test_us_at) return;
    const us_ent *u = us_find(g_test_us_at, 8);
    if (u) memcpy(&g_test_us_val, u->cpu + (g_test_us_at - u->lo), 8);
    g_test_us_hit = u != NULL;
}
static void us_from_cap(const mtl_cap *c) {   // mtl_sub_snapshot with a capture: its bytes, less what a GPU write not complete at the capture reaches
    if (!us_on() || !c->nr) return;
    uint64_t t0 = core_now_ns();
    g_us.subs++;
    uint64_t done = mtl_done_fence(), keep = c->done < done ? c->done : done, gw_max;
#ifdef DM_TEST_XLAT_CAP_STALE   // negative control of t_xlat only: only the writes still running now count, as without a capture
    keep = done;
#endif
    int ng = us_gw_keep(keep, &gw_max);
    g_us.ranges += (uint64_t)c->nr;
    us_reserve(c->nr);
    for (int i = 0; i < c->nr; i++) {
        void *mb; uint64_t mo, sz = c->r[i].hi - c->r[i].lo;
        if (c->off[i] == UINT64_MAX || !mtl_addr(c->r[i].lo, sz, &mb, &mo)) continue;
        if (ng && us_gw_reaches(c->r[i].lo, c->r[i].hi, ng, gw_max)) { g_us.skip_gpu++; continue; }
        us_ent *e = us_add(c->r[i].lo, c->r[i].hi);
        memcpy(e->cpu, c->data + c->off[i], (size_t)sz);
    }
    g_us.copies += (uint64_t)g_us.n;
    g_us.ns += core_now_ns() - t0;
}
void mtl_sub_snapshot(const uint64_t *h, int n) {   // hle_nvn.c nvnQueueSubmitCommands, after mtl_sub_begin and before the replay
    g_us.n = 0;
    if (g_cap_cur) { us_from_cap(g_cap_cur); us_test_note(); return; }   // the translation thread's replay: the submit call's bytes and handles (above)
    us_snapshot(h, n);
}
static void us_drop(uint64_t a, uint64_t n) { if (us_drop_range(a, n, g_fence + 1)) args_dirty_all(); }   // a GPU write of this submission reaches [a, a + n): bindings encoded from here on read memory
static int g_nupd, g_upd_flushing;   // renamed inline uniform updates not yet in memory (see upd_log)
static void upd_apply(uint64_t lo, uint8_t *out, uint64_t n);
static int upd_any(void);   // the submission logged inline uniform updates (written to memory or not): a snapshot alone predates them
static int pend_read(uint64_t a, void *out, uint64_t n) {
    { const us_ent *u = us_find(a, n); if (u) { memcpy(out, u->cpu + (a - u->lo), n); if (upd_any()) upd_apply(a, out, n); return 1; } }   // not dropped: no GPU write of this submission reached it but renamed updates
    return pend_read_x(a, out, n, g_pend.n, 0);
}
typedef struct { uint64_t ser; uint16_t level, slice; } zskip_zero;
static zskip_zero g_zskip_zero[4096]; static int g_zskip_n, g_zskip_count;
static zskip_zero g_zskip_cand[8]; static uint8_t g_zskip_cand_ok[8];
static void zskip_clear(void);
static id g_cb;
static BENC g_blit;
static NSMutableArray<dispatch_block_t> *g_commit_blocks;
static uint64_t g_alloc_fence[ALLOC_MAX]; static uint64_t g_alloc_waits, g_alloc_grown;
int mtl_wait(uint64_t fence, uint64_t timeout_ns);
static void up_flush(void);
