// gfx/nvn/mtl/uniform_rename.inc, part of nvn_mtl.m (included there in order, one translation unit): Inline uniform updates without ending the render pass
// ---------- Inline uniform updates without ending the render pass (renaming; MacNativeGames' upd_log / upd_shadow, MIT; written anew here) ----------
// Tears of the Kingdom updates small uniform ranges between draws about 200 times a frame (nvnCommandBufferUpdateUniformBuffer). Each was a GPU copy,
// and a copy ends the render pass: a copy encoder in between and the pass reopened with every attachment loaded again (MacNativeGames counted ~80 copy
// encoders and ~23 split passes a frame). Now an update keeps its bytes in the update ring and is logged (upd_log). A draw binding a uniform range that
// logged updates overlap gets a private copy of that range (upd_shadow): its contents as of the submit call (the snapshot) with the logged updates
// applied in order, so draws before an update read the old bytes and draws after it the new ones. The logged updates reach memory, merged, at the end
// of the submission (mtl_sub_commit), before any copy or dispatch (blit(), dispatch) and before a draw that reads an updated range other than as a
// uniform buffer, or binds one without a snapshot (upd_guard_draw, before its render pass is opened). DM_NVN_UB_RENAME=0: a GPU copy per update
typedef struct { uint64_t lo, hi, seq; const uint8_t *data; } upd_ent;
// A log kept sorted by start, with the largest end up to each entry: overlaps are found by a binary search and a walk back over real overlaps (every
// uniform binding of a draw is looked up, and most overlap nothing). g_upd: updates not in memory yet (written by upd_flush); g_uh: every update of
// the submission, written or not (a private copy is the snapshot with all of them: the snapshot stays valid after a write, which only adds them)
typedef struct { upd_ent *e; uint64_t *maxhi; int n, cap; uint64_t lo, hi, ver; } upd_list;   // ver: changed by every add and clear (ul_seq's cache)
static upd_list g_upd = {.lo = UINT64_MAX}, g_uh = {.lo = UINT64_MAX};
static int *g_upd_idx, g_upd_icap; static uint64_t g_upd_seq, g_ren_pass = UINT64_MAX;
static uint64_t g_ren_updates, g_ren_shadows, g_ren_reuse, g_ren_flushes, g_ren_guard, g_ren_noshot, g_ren_unknown;
static uint64_t g_ul_calls, g_ul_walk, g_ul_applied, g_ul_moves;   // ul_apply: calls past the range check, log entries walked, entries applied, sort moves
static uint64_t g_ren_incr;   // private copies made from the range's previous copy of the submission and the updates after it (plan item 225)
static int ub_rename_on(void) {
    static int v = -1;
    if (v < 0) { const char *e = getenv("DM_NVN_UB_RENAME"); v = !(e && !strcmp(e, "0")); if (!v) MLOG("DM_NVN_UB_RENAME=0: inline uniform updates copied on the GPU timeline (each ends the render pass)"); }
    return v;
}
static void upd_dirty_stages(uint64_t lo, uint64_t hi) {   // the argument blocks of stages binding an updated range are encoded again (they point at the old copy)
    for (int st = 0; st < 6; st++) for (int k = 0; k < 16; k++) { const p_bind_buf *u = &S.ubo[st][k]; if (u->addr && u->addr < hi && u->addr + (u->size ? u->size : 1) > lo) { g_args_dirty[st] = 1; break; } }
}
static void ul_add(upd_list *l, upd_ent v) {
    if (l->n == l->cap) { l->cap = l->cap ? l->cap * 2 : 256; l->e = realloc(l->e, (size_t)l->cap * sizeof *l->e); l->maxhi = realloc(l->maxhi, (size_t)l->cap * sizeof *l->maxhi);
                          if (!l->e || !l->maxhi) MDIE("uniform update log (%d entries)", l->cap); }
    int a = 0, b = l->n; while (a < b) { int m = (a + b) / 2; if (l->e[m].lo <= v.lo) a = m + 1; else b = m; }
    memmove(l->e + a + 1, l->e + a, (size_t)(l->n - a) * sizeof *l->e);
    l->e[a] = v; l->n++;
    for (int i = a; i < l->n; i++) { uint64_t pv = i ? l->maxhi[i - 1] : 0; l->maxhi[i] = MAX(l->e[i].hi, pv); }
    l->lo = MIN(l->lo, v.lo); l->hi = MAX(l->hi, v.hi); l->ver++;
}
static void ul_clear(upd_list *l) { l->n = 0; l->lo = UINT64_MAX; l->hi = 0; l->ver++; }
// The newest entry overlapping [lo, hi), 0 if none. Every uniform binding of a draw asks, about 13,000 times a frame in Tears of the Kingdom with ~470
// updates between: answers are kept per range until the list changes (plan item 225)
static struct { const upd_list *l; uint64_t ver, lo, hi, seq; } g_ul_seqc[256];
static uint64_t ul_seq(const upd_list *l, uint64_t lo, uint64_t hi) {
    if (!l->n || hi <= l->lo || lo >= l->hi) return 0;
    unsigned k = (unsigned)(((lo >> 4) ^ (hi >> 9) ^ ((uintptr_t)l >> 5)) & 255);
#ifndef DM_TEST_UL_SEQ_STALE   // negative control of t_ubren only: an answer kept after the list changed
    if (g_ul_seqc[k].l == l && g_ul_seqc[k].ver == l->ver && g_ul_seqc[k].lo == lo && g_ul_seqc[k].hi == hi) return g_ul_seqc[k].seq;
#else
    if (g_ul_seqc[k].l == l && g_ul_seqc[k].lo == lo && g_ul_seqc[k].hi == hi) return g_ul_seqc[k].seq;
#endif
    int a = 0, b = l->n; while (a < b) { int m = (a + b) / 2; if (l->e[m].lo < hi) a = m + 1; else b = m; }   // [0, a) start below hi
    uint64_t last = 0;
    for (int i = a - 1; i >= 0 && l->maxhi[i] > lo; i--) if (l->e[i].hi > lo && l->e[i].seq > last) last = l->e[i].seq;
    g_ul_seqc[k].l = l; g_ul_seqc[k].ver = l->ver; g_ul_seqc[k].lo = lo; g_ul_seqc[k].hi = hi; g_ul_seqc[k].seq = last;
    return last;
}
static void ul_apply(const upd_list *l, uint64_t lo, uint8_t *out, uint64_t n, uint64_t after) {   // the entries newer than after over out (= [lo, lo + n) before them), oldest first
    if (!l->n || lo + n <= l->lo || lo >= l->hi) return;
    uint64_t hi = lo + n; int a = 0, b = l->n, k = 0; while (a < b) { int m = (a + b) / 2; if (l->e[m].lo < hi) a = m + 1; else b = m; }
    if (g_upd_icap < l->n) { g_upd_icap = l->n * 2; g_upd_idx = realloc(g_upd_idx, (size_t)g_upd_icap * sizeof *g_upd_idx); if (!g_upd_idx) MDIE("uniform update log index"); }
    int i0 = a - 1;
    for (int i = a - 1; i >= 0 && l->maxhi[i] > lo; i--) { i0 = i; if (l->e[i].hi > lo && l->e[i].seq > after) g_upd_idx[k++] = i; }
    g_ul_calls++; g_ul_walk += (uint64_t)(a - i0); g_ul_applied += (uint64_t)k;
    // The walk went from the newest start down, and updates of one start sit in the log oldest first: reversed, the list is runs already in order,
    // which the insertion sort below passes in about k steps. Tears of the Kingdom's frame applies ~90 updates of a few ranges per private copy, ~360
    // copies a frame: sorted from the walk's order that was ~4,000 moves a copy, 18 % of the translation thread on the M5 (plan item 225)
#ifndef DM_TEST_UL_NO_REVERSE   // negative control of t_nvn only: sorted from the walk's order (the result is the same, the time is not)
    for (int i = 0, j = k - 1; i < j; i++, j--) { int t = g_upd_idx[i]; g_upd_idx[i] = g_upd_idx[j]; g_upd_idx[j] = t; }
#endif
#ifndef DM_TEST_UB_NEWEST_FIRST   // negative control of t_nvn 5a2 only: the newest applied first, an older one over it
    for (int i = 1; i < k; i++) { int x = g_upd_idx[i], j = i - 1; while (j >= 0 && l->e[g_upd_idx[j]].seq > l->e[x].seq) { g_upd_idx[j + 1] = g_upd_idx[j]; j--; g_ul_moves++; } g_upd_idx[j + 1] = x; }
#else
    for (int i = 1; i < k; i++) { int x = g_upd_idx[i], j = i - 1; while (j >= 0 && l->e[g_upd_idx[j]].seq < l->e[x].seq) { g_upd_idx[j + 1] = g_upd_idx[j]; j--; g_ul_moves++; } g_upd_idx[j + 1] = x; }
#endif
    for (int i = 0; i < k; i++) { const upd_ent *e = &l->e[g_upd_idx[i]]; uint64_t s0 = MAX(lo, e->lo), e0 = MIN(hi, e->hi); memcpy(out + (s0 - lo), e->data + (s0 - e->lo), e0 - s0); }
}
static void upd_log(uint64_t lo, uint64_t hi, const uint8_t *data) {
    upd_ent v = {lo, hi, ++g_upd_seq, data};
    ul_add(&g_upd, v); ul_add(&g_uh, v); g_nupd = g_upd.n;
    g_ren_updates++; if (g_enc) g_ren_pass = g_pass.serial;
    upd_dirty_stages(lo, hi);
}
static void upd_sub_end(void) { ul_clear(&g_uh); }   // mtl_sub_commit, after upd_flush
// Not g_nupd: once upd_flush has written the updates (a copy, a dispatch or upd_guard_draw before the draw), g_nupd is 0 but the snapshot still holds
// the bytes of the submit call, without them. Gating the private copies on g_nupd gave a draw bound after such a write the snapshot itself: Tears of
// the Kingdom's cave meshes then decoded their vertices with the constants another mesh had at the submit call and were drawn where that mesh is,
// and a cave wall came and went with the culling of the mesh drawn in its place (plan item 62k)
static int upd_any(void) {
#ifndef DM_TEST_UB_FLUSHED_SNAPSHOT   // negative control of t_nvn 5a3 only: private copies only while updates are unwritten, as before
    return g_uh.n > 0;
#else
    return g_nupd > 0;
#endif
}
static uint64_t upd_overlap_seq(uint64_t lo, uint64_t hi) { return ul_seq(&g_upd, lo, hi); }   // updates not in memory yet
static void upd_apply(uint64_t lo, uint8_t *out, uint64_t n) { ul_apply(&g_uh, lo, out, n, 0); }   // over a snapshot: every update of the submission
// Private copies of updated uniform ranges, found through a direct-mapped index by (range, newest overlapping update): later draws binding the same
// range with no update in between share one. A range updated again in the same submission gets its new copy from its previous one with only the
// updates after it (plan item 225): Tears of the Kingdom updates a few ranges ~470 times a frame and makes ~360 copies, each of which applied ~90
// updates from the snapshot (the previous copy is the snapshot with every update up to its own, and the snapshot and the log of the submission
// only grow). An entry of an earlier submission is never the base: its snapshot is another
typedef struct { uint64_t lo, size, seq, gpu, sub; void *buf; uint8_t *cpu; } ub_shadow;   // cpu: the copy's bytes when it was made over the snapshot, else NULL
enum { UB_SHADOW_N = 1024 };
static ub_shadow g_shadow[UB_SHADOW_N];
static uint64_t upd_shadow(uint64_t lo, uint64_t size, void **buf) {   // GPU address of [lo, lo + size) with the submission's updates, 0 to read as before (none overlaps, or memory holds them)
    uint64_t seq = size ? ul_seq(&g_uh, lo, lo + size) : 0;
    if (!seq) return 0;
    const us_ent *sc = us_find(lo, size);
    int pending = ul_seq(&g_upd, lo, lo + size) != 0;
    if (!sc && !pending) return 0;   // no snapshot (a GPU write of this submission reached it) and its updates written: memory on the GPU timeline is right
    uint64_t hh = (lo >> 4) ^ (lo >> 19) ^ (size * 0x9E3779B97F4A7C15ull); ub_shadow *e = &g_shadow[(hh ^ (hh >> 32)) & (UB_SHADOW_N - 1)];
    if (e->lo == lo && e->size == size && e->seq == seq && e->gpu) { *buf = e->buf; g_ren_reuse++; return e->gpu; }   // seq only grows: an entry of an earlier submission never matches
    ARENA_OUT id<MTLBuffer> ab; uint64_t pad = lo & 255; size_t off = arena_alloc((size_t)(size + pad), &ab) + (size_t)pad;   // same offset modulo 256 as the original (as the snapshots)
    uint8_t *cpu = (uint8_t *)ab.contents + off;
    if (sc) {
#ifndef DM_TEST_UBSH_ANY_SUB   // negative control of t_ubren only: a copy of an earlier submission taken as the base
        int same = e->sub == g_sub_serial;
#else
        int same = 1;
#endif
        if (e->lo == lo && e->size == size && same && e->cpu && e->seq < seq) { memcpy(cpu, e->cpu, (size_t)size); ul_apply(&g_uh, lo, cpu, size, e->seq); g_ren_incr++; }
        else { memcpy(cpu, sc->cpu + (lo - sc->lo), (size_t)size); upd_apply(lo, cpu, size); }
    } else {   // no snapshot and updates not written (upd_guard_draw could not write them first): memory with the pending writes, the updates among them
        g_ren_noshot++;
        if (!pend_read_x(lo, cpu, size, g_npend, 0)) { g_ren_unknown++; RED_ONCE(90, "renamed uniform update: range %#llx+%llu has no snapshot and a GPU write of unknown content (first case): its copy may be stale", (unsigned long long)lo, (unsigned long long)size); }
    }
    *e = (ub_shadow){lo, size, seq, ab.gpuAddress + off, g_sub_serial, (__bridge void *)ab, sc ? cpu : NULL}; *buf = e->buf; g_ren_shadows++;
    return e->gpu;
}
static void upd_flush(void) {   // the updates not in memory yet into memory on the GPU timeline (merged; ends a render pass if one is open)
    if (!g_upd.n || g_upd_flushing) return;
    g_upd_flushing = 1;
    upd_list *l = &g_upd;
    for (int i = 0; i < l->n;) {
        uint64_t lo = l->e[i].lo, hi = l->e[i].hi; int j = i + 1;
        for (; j < l->n && l->e[j].lo <= hi; j++) hi = MAX(hi, l->e[j].hi);   // overlapping or touching: covered whole by updates, one copy
        void *db; uint64_t dof;
        if (mtl_addr(lo, hi - lo, &db, &dof)) {
            ARENA_OUT id<MTLBuffer> sb; size_t so = arena_alloc((size_t)(hi - lo), &sb);
            ul_apply(l, lo, (uint8_t *)sb.contents + so, hi - lo, 0);
            enc_b_copy_buffer(blit(), sb, so, (__bridge id<MTLBuffer>)db, dof, hi - lo);
        } else {   // touching updates in two pools: each on its own, oldest first (blit() orders them)
            int k0 = 0; if (g_upd_icap < j - i) { g_upd_icap = (j - i) * 2; g_upd_idx = realloc(g_upd_idx, (size_t)g_upd_icap * sizeof *g_upd_idx); }
            for (int q = i; q < j; q++) g_upd_idx[k0++] = q;
            for (int q = 1; q < k0; q++) { int x = g_upd_idx[q], r = q - 1; while (r >= 0 && l->e[g_upd_idx[r]].seq > l->e[x].seq) { g_upd_idx[r + 1] = g_upd_idx[r]; r--; } g_upd_idx[r + 1] = x; }
            for (int q = 0; q < k0; q++) { const upd_ent *e = &l->e[g_upd_idx[q]]; void *eb; uint64_t eo; __unsafe_unretained id<MTLBuffer> rb; size_t ro;
                if (!mtl_addr(e->lo, e->hi - e->lo, &eb, &eo)) MDIE("renamed uniform update %#llx+%#llx is no longer in a memory pool", (unsigned long long)e->lo, (unsigned long long)(e->hi - e->lo));
                ring_put(e->data, e->hi - e->lo, &rb, &ro); enc_b_copy_buffer(blit(), rb, ro, (__bridge id<MTLBuffer>)eb, eo, e->hi - e->lo); }
        }
        if (us_on()) us_gw_note(lo, hi - lo, g_fence + 1);   // a GPU write for the next submissions; the snapshot stays (private copies add every update to it)
        i = j;
    }
    ul_clear(l); g_nupd = 0; g_ren_flushes++;
    g_upd_flushing = 0;
}
static uint64_t g_ren_g[5];   // what made upd_guard_draw write the updates: index, vertex, storage buffer, buffer texture, uniform range without a snapshot
static void ren_report(uint64_t f) {   // every 300 frames
    static uint64_t p[16]; uint64_t c[16] = {g_ren_updates, g_ren_shadows, g_ren_reuse, g_ren_flushes, g_ren_guard, g_ren_noshot, g_ren_unknown, g_ren_g[0], g_ren_g[1], g_ren_g[2], g_ren_g[3], g_ren_g[4],
                                             g_ul_calls, g_ul_walk, g_ul_applied, g_ren_incr};
    if (f % 300 || c[0] == p[0]) return;
    double uc = c[12] > p[12] ? (double)(c[12] - p[12]) : 1;
    MLOG("renamed uniform updates up to frame %llu: %llu updates, %llu private copies (%llu shared), written to memory %llu times (%llu before a draw reading them otherwise: index %llu, vertex %llu, storage %llu, buffer texture %llu, no snapshot %llu); %llu copies without a snapshot, %llu of unknown content; "
         "%llu applications of the log, %.1f entries walked and %.1f applied each; %llu copies from the range's previous copy (plan item 225)",
         (unsigned long long)f, (unsigned long long)(c[0] - p[0]), (unsigned long long)(c[1] - p[1]), (unsigned long long)(c[2] - p[2]), (unsigned long long)(c[3] - p[3]), (unsigned long long)(c[4] - p[4]),
         (unsigned long long)(c[7] - p[7]), (unsigned long long)(c[8] - p[8]), (unsigned long long)(c[9] - p[9]), (unsigned long long)(c[10] - p[10]), (unsigned long long)(c[11] - p[11]),
         (unsigned long long)(c[5] - p[5]), (unsigned long long)(c[6] - p[6]), (unsigned long long)(c[12] - p[12]), (c[13] - p[13]) / uc, (c[14] - p[14]) / uc, (unsigned long long)(c[15] - p[15]));
    memcpy(p, c, sizeof p);
}
