// gfx/nvn/core/store_index.c: the store lookahead index, see store_index.h
#include "store_index.h"
#include "prog.h"
#include "rt.h"
#include <stdlib.h>
#include <string.h>

sdx_index g_sdx;
static sdx_ev *g_sdx_tmp; static uint32_t g_sdx_cap;
// Candidate textures (those that have been attachments) -> buckets: open addressing; a texture that has not yet been an attachment when an event occurs is irrelevant to any render pass that ended before then, so it is not recorded
static uint64_t g_sdx_hk[SDX_HB]; static uint16_t g_sdx_hb[SDX_HB]; static uint32_t g_sdx_bcnt[SDX_HB + 1];
int sdx_bucket(uint64_t ser, int add) {
    if (!ser) return -1;
    for (uint32_t i = (uint32_t)(ser * 0x9E3779B97F4A7C15ull >> 53), n = 0; n < SDX_HB; i = (i + 1) & (SDX_HB - 1), n++) {
        if (g_sdx_hk[i] == ser) return g_sdx_hb[i];
        if (!g_sdx_hk[i]) { if (!add) return -1; if (g_sdx.nb >= SDX_HB / 2) { g_sdx.ok = 0; return -1; } g_sdx_hk[i] = ser; g_sdx_hb[i] = (uint16_t)g_sdx.nb; return (int)g_sdx.nb++; }
    }
    return -1;
}
static uint32_t g_sdx_opcap, g_rtx_cap, g_drx_cap;
static uint64_t g_pp_slot[6][160]; static const prog_t *g_pp_prog; static const p_set_rt *g_pp_rt; static uint32_t g_pp_rtseq; static int g_pp_ren_maybe_off;   // render enable not known to be on (see sdx_walk)
static p_scissor g_pp_sc; static int g_pp_has_sc;   // scissor at that point of the walk: a clear under a partial scissor leaves the rest of the target (see clear_quad)
static void seq_push(uint32_t **v, uint32_t *n, uint32_t *cap, uint32_t x) { if (*n == *cap) { *cap = *cap ? *cap * 2 : 1024; *v = realloc(*v, *cap * sizeof **v); } (*v)[(*n)++] = x; }
static void sdx_ev_add(sdx_ev e) { if (sdx_bucket(e.ser, 0) < 0) return; if (g_sdx.n == g_sdx_cap) { g_sdx_cap = g_sdx_cap ? g_sdx_cap * 2 : 4096; g_sdx.ev = realloc(g_sdx.ev, g_sdx_cap * sizeof *g_sdx.ev); g_sdx_tmp = realloc(g_sdx_tmp, g_sdx_cap * sizeof *g_sdx.ev); } g_sdx.ev[g_sdx.n++] = e; }
static void sdx_lvsl(const view_t *v, int hv, uint16_t *lv, uint16_t *sl) { *lv = (uint16_t)(hv && v->set_levels ? v->base_level : 0); *sl = (uint16_t)(hv && v->set_layers ? v->min_layer : 0); }
static void sdx_walk(const uint8_t *p, int depth) {
    if (depth > 64) { g_sdx.ok = 0; return; }
    for (uint32_t guard = 0; guard < 4000000 && p && g_sdx.ok; guard++) {
        op_hdr h; memcpy(&h, p, sizeof h); const void *a = p + 8;
        if (g_sdx.nop == g_sdx_opcap) { g_sdx_opcap = g_sdx_opcap ? g_sdx_opcap * 2 : 16384; g_sdx.op = realloc(g_sdx.op, g_sdx_opcap * sizeof *g_sdx.op); }
        uint32_t seq = g_sdx.nop; g_sdx.op[g_sdx.nop++] = p;
        switch (h.op) {
            case OP_END: return;
            case OP_JUMP: { const uint8_t *t; memcpy(&t, a, 8); p = t; continue; }
            case OP_CALL: { uint64_t t; memcpy(&t, a, 8); sdx_walk((const uint8_t *)t, depth + 1); break; }
            case OP_SET_RT: { const p_set_rt *r = a; g_pp_rt = r; g_pp_rtseq = seq; seq_push(&g_sdx.rtx, &g_sdx.nrtx, &g_rtx_cap, seq);
                for (int k = 0; k < r->n && k < 8; k++) if (r->color[k]) sdx_bucket(r->color[k]->serial, 1);
                if (r->depth) sdx_bucket(r->depth->serial, 1);
                break; }
            case OP_RENDER_ENABLE: g_pp_ren_maybe_off = ((const p_render_enable *)a)->mode != 1; break;   // a clear while rendering may be off is not counted as a clear: storing is always correct
            case OP_SCISSOR: g_pp_sc = *(const p_scissor *)a; g_pp_has_sc = 1; break;
            case OP_CLEAR_COLOR: { const p_clear_color *c = a; const p_set_rt *r = g_pp_rt;
                if (!r || g_pp_ren_maybe_off || (c->mask & 0xf) != 0xf || c->index < 0 || c->index >= r->n || c->index >= 8 || !r->color[c->index]) break;
                if (!scissor_full_of(&g_pp_sc, g_pp_has_sc, r->color[c->index], &r->cview[c->index], r->has_cview[c->index])) break;
                uint16_t lv, sl; sdx_lvsl(&r->cview[c->index], r->has_cview[c->index], &lv, &sl); int last = -1;   // When the same subresource is attached at several indices, the last one counts
                for (int k = 0; k < r->n && k < 8; k++) { uint16_t l2, s2; if (!r->color[k] || r->color[k]->serial != r->color[c->index]->serial) continue; sdx_lvsl(&r->cview[k], r->has_cview[k], &l2, &s2); if (l2 == lv && s2 == sl) last = k; }
                if (last == c->index) sdx_ev_add((sdx_ev){seq, g_pp_rtseq, r->color[c->index]->serial, p, lv, sl, SDX_CLEAR, 0, 0, 0});
                break; }
            case OP_CLEAR_DS: { const p_clear_ds *c = a; const p_set_rt *r = g_pp_rt; if (!r || !r->depth || g_pp_ren_maybe_off || !scissor_full_of(&g_pp_sc, g_pp_has_sc, r->depth, &r->dview, r->has_dview)) break;
                uint16_t lv, sl; sdx_lvsl(&r->dview, r->has_dview, &lv, &sl);
                if (c->depth_mask) sdx_ev_add((sdx_ev){seq, g_pp_rtseq, r->depth->serial, p, lv, sl, SDX_CLEAR, 1, !r->has_dview, 0});
                if ((c->stencil_mask & 0xff) == 0xff) sdx_ev_add((sdx_ev){seq, g_pp_rtseq, r->depth->serial, p, lv, sl, SDX_CLEAR, 2, !r->has_dview, 0});
                break; }
            case OP_BIND_TEX: case OP_BIND_IMG: { const p_bind_handles *b = a; int base = h.op == OP_BIND_IMG ? 128 : 0, lim = h.op == OP_BIND_IMG ? 32 : 128;
                int in = b->stage < 6 && b->first >= 0 && b->first + b->n <= lim;
                nvn_be_bind_sers(b, in ? b->n : 0, in ? &g_pp_slot[b->stage][base + b->first] : NULL);
                break; }
            case OP_BIND_PROGRAM: g_pp_prog = prog_bind(g_pp_prog, ((const p_bind_program *)a)->prog, ((const p_bind_program *)a)->stages); break;
            case OP_DRAW: case OP_DRAW_IDX: case OP_DISPATCH: {
                if (h.op != OP_DISPATCH) seq_push(&g_sdx.drx, &g_sdx.ndrx, &g_drx_cap, seq);
                const nvn_refl *rf[6]; int stg[6];
                if (!g_pp_prog || !nvn_be_prog_refl(g_pp_prog, rf, stg)) { for (int st = 0; st < 6; st++) for (int k = 0; k < 160; k++) if (g_pp_slot[st][k]) sdx_ev_add((sdx_ev){seq, 0, g_pp_slot[st][k], p, 0, 0, SDX_READ, 0, 0, 1}); }
                else for (int st = 0; st < 6; st++) { if (!rf[st] || (h.op == OP_DISPATCH) != (st == 5)) continue;
                    for (int i = 0; i < rf[st]->nb; i++) { const nvn_rbind *b = &rf[st]->b[i]; int k = b->kind == NVN_RB_IMG ? 128 + b->reg : (b->kind == NVN_RB_TEX || b->kind == NVN_RB_TBUF) ? b->reg : -1;
                        if (k < 0 || k >= 160 || stg[st] < 0 || stg[st] >= 6) continue; uint64_t ser = g_pp_slot[stg[st]][k];
                        if (ser) sdx_ev_add((sdx_ev){seq, 0, ser, p, 0, 0, SDX_READ, 0, 0, 1}); } }
                const p_set_rt *r = g_pp_rt;
                if (h.op != OP_DISPATCH && r) {
                    for (int k = 0; k < r->n && k < 8; k++) if (r->color[k]) { uint16_t lv, sl; sdx_lvsl(&r->cview[k], r->has_cview[k], &lv, &sl); sdx_ev_add((sdx_ev){seq, g_pp_rtseq, r->color[k]->serial, p, lv, sl, SDX_ATT, 0, 0, 2}); }
                    if (r->depth) { uint16_t lv, sl; sdx_lvsl(&r->dview, r->has_dview, &lv, &sl); sdx_ev_add((sdx_ev){seq, g_pp_rtseq, r->depth->serial, p, lv, sl, SDX_ATT, 8, !r->has_dview, 2}); }
                }
                break; }
            case OP_COPY_T2T: { const p_copy_t2t *c = a; if (c->src) sdx_ev_add((sdx_ev){seq, 0, c->src->serial, p, 0, 0, SDX_NEED, 0, 0, 3}); if (c->dst) sdx_ev_add((sdx_ev){seq, 0, c->dst->serial, p, 0, 0, SDX_NEED, 0, 0, 3}); break; }
            case OP_COPY_T2B: case OP_COPY_B2T: { const p_copy_b2t *c = a; if (c->tex) sdx_ev_add((sdx_ev){seq, 0, c->tex->serial, p, 0, 0, SDX_NEED, 0, 0, 3}); break; }
            case OP_CLEAR_TEX: { const p_clear_tex *c = a; if (c->tex) sdx_ev_add((sdx_ev){seq, 0, c->tex->serial, p, 0, 0, SDX_NEED, 0, 0, 4}); break; }
            default: break;
        }
        p += h.size;
    }
}
int sdx_begin(int on) {
    g_sdx.n = g_sdx.nop = 0; g_sdx.nrtx = g_sdx.ndrx = 0; g_sdx.exec_seq = 0; g_sdx.ok = on;
    if (on) { memset(g_sdx_hk, 0, sizeof g_sdx_hk); g_sdx.nb = 0; }
    return g_sdx.ok;
}
void sdx_build(const sdx_start *s, const uint64_t *h, int n) {
    if (!g_sdx.ok) return;
    if (s->rt) { for (int k = 0; k < s->rt->n && k < 8; k++) if (s->rt->color[k]) sdx_bucket(s->rt->color[k]->serial, 1); if (s->rt->depth) sdx_bucket(s->rt->depth->serial, 1); }
    g_pp_prog = s->prog; g_pp_rt = NULL; g_pp_rtseq = SDX_NONE; g_pp_ren_maybe_off = s->render_off;
    g_pp_sc = s->sc; g_pp_has_sc = s->has_sc;
    memcpy(g_pp_slot, s->slot, sizeof g_pp_slot);
    for (int i = 0; i < n && g_sdx.ok; i++) sdx_walk((const uint8_t *)h[i], 0);
    if (!g_sdx.ok) return;
    // Counting sort into buckets: events are appended in sequence-number order, and within the same sequence number READ precedes ATT (the same draw is judged for reads first, then attachments), so after a stable scatter each bucket is ordered
    memset(g_sdx_bcnt, 0, (g_sdx.nb + 1) * sizeof *g_sdx_bcnt);
    for (uint32_t i = 0; i < g_sdx.n; i++) g_sdx_bcnt[sdx_bucket(g_sdx.ev[i].ser, 0)]++;
    for (uint32_t b = 0, acc = 0; b <= g_sdx.nb; b++) { g_sdx.bst[b] = acc; acc += b < g_sdx.nb ? g_sdx_bcnt[b] : 0; }
    for (uint32_t b = 0; b < g_sdx.nb; b++) g_sdx_bcnt[b] = g_sdx.bst[b];
    for (uint32_t i = 0; i < g_sdx.n; i++) g_sdx_tmp[g_sdx_bcnt[sdx_bucket(g_sdx.ev[i].ser, 0)]++] = g_sdx.ev[i];
    sdx_ev *t = g_sdx.ev; g_sdx.ev = g_sdx_tmp; g_sdx_tmp = t;
}
static int skipped(const uint8_t *p, const uint8_t *const *skip, int n) { for (int i = 0; i < n; i++) if (skip[i] == p) return 1; return 0; }
uint8_t sdx_first(uint64_t ser, uint32_t lv, uint32_t sl, int depth, uint32_t P, const uint8_t *const *skip, int nskip) {
    int b = sdx_bucket(ser, 0); if (b < 0) return 5 << 2;   // Not in the candidate set: no related event at all in this submit -> undecided after scanning to the end, needed
    uint32_t lo = g_sdx.bst[b], end = g_sdx.bst[b + 1], hi = end;   // First seq >= P in the bucket
    while (lo < hi) { uint32_t m = (lo + hi) / 2; if (g_sdx.ev[m].seq < P) lo = m + 1; else hi = m; }
    for (uint32_t j = lo; j < end; j++) { const sdx_ev *e = &g_sdx.ev[j];
        if (nskip && skipped(e->op, skip, nskip)) continue;
        if (e->kind == SDX_READ || e->kind == SDX_NEED) return (uint8_t)(e->why << 2);
#ifndef DM_TEST_SDX_ANY_RTSEQ   // negative control of t_core_store_index only: an attachment set before the render pass ended counts as set again after it
        if (e->rtseq == SDX_NONE || e->rtseq < P || (nskip && skipped(g_sdx.op[e->rtseq], skip, nskip))) continue;   // This target was set before the render pass ended: not re-set as an attachment after the render pass ended, so it does not count
#else
        if (e->rtseq == SDX_NONE) continue;
#endif
        int hit = depth ? (e->kind == SDX_ATT ? e->plane == 8 : e->plane == depth) && (e->anyls || (e->lv == lv && e->sl == sl))
                        : e->plane == 0 && e->lv == lv && e->sl == sl;
        if (!hit) continue;
        return e->kind == SDX_CLEAR ? 2 : (uint8_t)(2 << 2);
    }
    return 5 << 2;
}
