// gfx/nvn/mtl/live.inc, part of nvn_mtl.m (included there in order, one translation unit): The live texture at a storage address, texture snapshots and releases
// ---------- The live texture at a storage address (MacNativeGames' live_put / nvn_tex_live_at, MIT; written anew here) ----------
// A game may finalize a texture and initialize a new one at the same storage every frame without registering its texture ID again (Tears of the
// Kingdom's transient G-buffer targets); on the console the descriptor names the memory, so the ID reads the new texture. Here the ID kept the
// finalized texture: its old content for FIN_GRACE submissions, then a blank one created again (56 "created again" red lines in play). tex_by_id
// now follows a registration whose texture was finalized to the texture living at its storage, when that one has the same shape (t_live).
// g_live: storage address → copy of the texture last initialized there and not finalized; g_dead: finalized serials not released yet; g_fin_gen
// counts finalizations (tex_by_id looks again only after one)
static nvn_hmap g_live = {.klen = 8}, g_dead = {.klen = 8}; static pthread_mutex_t g_live_mu = PTHREAD_MUTEX_INITIALIZER; static _Atomic uint64_t g_fin_gen, g_live_follows;
static uint64_t g_alias_n;   // textures initialized over the storage of one not finalized (two live textures sharing storage: MacNativeGames' stor_*)
void mtl_tex_live(const tex_t *t) {   // hle_nvn.c nvnTextureInitialize
    if (!t->mem || !t->serial) return;
    uint64_t k = (uint64_t)(uintptr_t)t->mem; tex_t *c = malloc(sizeof *c); if (!c) MDIE("live texture"); *c = *t;
    pthread_mutex_lock(&g_live_mu);
    tex_t *old = nvn_hm_put(&g_live, &k, c);
    if (old && old->serial != t->serial && !nvn_hm_get(&g_dead, &old->serial)) {
        uint64_t n = ++g_alias_n;
        if (n <= 8 || !(n % 1000)) MLOG("texture %llu (%ux%ux%u format %#x flags %#x) initialized over the storage of live texture %llu (%ux%ux%u format %#x flags %#x): two textures share storage (%llu so far)",
            (unsigned long long)t->serial, t->d.width, t->d.height, t->d.depth, t->d.format, t->d.flags, (unsigned long long)old->serial, old->d.width, old->d.height, old->d.depth, old->d.format, old->d.flags, (unsigned long long)n);
    }
    pthread_mutex_unlock(&g_live_mu); free(old);
}
static void live_finalized(const tex_t *t) {   // nvnTextureFinalize (mtl_tex_release)
    pthread_mutex_lock(&g_live_mu);
    uint64_t s = t->serial; nvn_hm_put(&g_dead, &s, (void *)1);
    if (t->mem) { uint64_t k = (uint64_t)(uintptr_t)t->mem; tex_t *cur = nvn_hm_get(&g_live, &k); if (cur && cur->serial == t->serial) free(nvn_hm_del(&g_live, &k)); }
    pthread_mutex_unlock(&g_live_mu);
    atomic_fetch_add(&g_fin_gen, 1);
}
static void live_released(uint64_t serial) { pthread_mutex_lock(&g_live_mu); nvn_hm_del(&g_dead, &serial); pthread_mutex_unlock(&g_live_mu); }   // fin_drain
static int live_follow(const tex_t *reg, int released, tex_t *out) {   // the texture to read for a registration of reg, when reg was finalized and replaced
    if (!reg->mem) return 0;
    uint64_t k = (uint64_t)(uintptr_t)reg->mem, s = reg->serial; int ok = 0;
    pthread_mutex_lock(&g_live_mu);
    if (released || nvn_hm_get(&g_dead, &s)) {
        const tex_t *cur = nvn_hm_get(&g_live, &k);
        ok = cur && cur->serial != reg->serial && cur->size == reg->size && cur->d.format == reg->d.format && cur->d.width == reg->d.width && cur->d.height == reg->d.height &&
             cur->d.depth == reg->d.depth && cur->d.target == reg->d.target && cur->d.levels == reg->d.levels && cur->d.samples == reg->d.samples && cur->d.flags == reg->d.flags;
        if (ok) *out = *cur;
    }
    pthread_mutex_unlock(&g_live_mu);
    return ok;
}
static void treg_drop_views(const uint64_t *serials, int n);
// Host copies of texture objects for recorded commands, one per serial (hle_nvn.c records them). A command (render targets, copies, clears) records
// which texture it uses; NVN puts what a command needs into command memory when it is recorded, so the game may finalize or reuse its NVNtexture
// before the command is submitted. Recording the game's pointer let replay read whatever was there by then (a garbage format at a scene load, a
// driver crash in a copy encoder). Commands point at this copy instead; it is freed SNAP_KEEP submissions after the texture's release (fin_drain),
// long after any command recorded before the finalize has been submitted
enum { SNAP_BUCKETS = 4096, SNAP_KEEP = 600 };   // SNAP_KEEP: about 10 s at 60 fps
typedef struct snap { struct snap *next; uint64_t due; tex_t t; } snap_t;
static snap_t *g_snap[SNAP_BUCKETS], *g_snap_old, **g_snap_old_tail = &g_snap_old; static pthread_mutex_t g_snap_mu = PTHREAD_MUTEX_INITIALIZER; static uint64_t g_snap_live;   // g_snap_old: released copies in due order (FIFO)
const tex_t *mtl_tex_snap(const tex_t *t) {
    if (!t || !t->serial) return t;   // not a texture this layer initialized: kept as is (replay reports it)
    pthread_mutex_lock(&g_snap_mu);
    snap_t **b = &g_snap[t->serial % SNAP_BUCKETS], *e = *b;
    while (e && e->t.serial != t->serial) e = e->next;
    if (!e && (e = malloc(sizeof *e))) { e->t = *t; e->due = 0; e->next = *b; *b = e; g_snap_live++; }
    pthread_mutex_unlock(&g_snap_mu);
    return e ? &e->t : t;
}
static void tex_snap_retire(uint64_t serial, uint64_t due) {   // the texture was released: its copy is freed at submission index due
    pthread_mutex_lock(&g_snap_mu);
    for (snap_t **b = &g_snap[serial % SNAP_BUCKETS]; *b; b = &(*b)->next)
        if ((*b)->t.serial == serial) { snap_t *e = *b; *b = e->next; e->due = due; e->next = NULL; *g_snap_old_tail = e; g_snap_old_tail = &e->next; break; }   // dues only grow: appended in order
    pthread_mutex_unlock(&g_snap_mu);
}
static void tex_snap_reap(uint64_t now) {
    pthread_mutex_lock(&g_snap_mu);
    // From the head until the first copy not due (the list was walked whole every submission: 0.7 ms with 18,000 copies in Tears of the Kingdom)
    while (g_snap_old && g_snap_old->due <= now) { snap_t *e = g_snap_old; g_snap_old = e->next; free(e); g_snap_live--; }
    if (!g_snap_old) g_snap_old_tail = &g_snap_old;
    pthread_mutex_unlock(&g_snap_mu);
}
uint64_t mtl_tex_snap_count(void) { pthread_mutex_lock(&g_snap_mu); uint64_t n = g_snap_live; pthread_mutex_unlock(&g_snap_mu); return n; }
static void blk_drop(uint64_t serial);   // a finalized compressed render target's auxiliary texture
static void fin_drain(void) {   // submit thread, at the start of a submission
    uint64_t due[64]; int n = 0;
    tex_snap_reap(g_sub_serial); stor_sweep();
    pthread_mutex_lock(&g_fin_mu);
    for (int i = 0; i < g_nfin;) {
        if (!g_fin[i].due) g_fin[i].due = g_sub_serial + FIN_GRACE;
        if (g_fin[i].due <= g_sub_serial && n < 64) { due[n++] = g_fin[i].serial; g_fin[i] = g_fin[--g_nfin]; } else i++;
    }
    pthread_mutex_unlock(&g_fin_mu);
    // Each serial's steps in the order they always ran; the views of all of them in one walk of the registrations (one walk per serial cost 0.28 ms
    // a submission in Tears of the Kingdom: 30 releases × 33,000 registrations)
    for (int i = 0; i < n; i++) serial_del(due[i]);
    treg_drop_views(due, n);
    for (int i = 0; i < n; i++) { blk_drop(due[i]); tex_snap_retire(due[i], g_sub_serial + SNAP_KEEP); live_released(due[i]); wb_released(due[i]); }
}
void mtl_tex_release(tex_t *t) { if (t->serial) { live_finalized(t); fin_queue(t->serial); tex_forget_serial(t->serial); } }

