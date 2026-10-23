// gfx/nvn/mtl/exec.inc, part of nvn_mtl.m (included there in order, one translation unit): The power-saving mode; the execution interface (gfx/nvn/nvn_backend.h: the replay loop is gfx/nvn/core/exec.c)
// ---------- Power-saving mode (30 frames per second) ----------
// DM_FPS=30: present at 30 Hz (the display link and the frame rate hint of the front end follow, so a ProMotion screen lowers its refresh rate).
// mtl_fps_set changes it while running (iOS Settings page): the display link follows on the next frame (link_sync)
static _Atomic int g_fps_target;
int mtl_fps_target(void) {
    int f = atomic_load(&g_fps_target);
    if (!f) { const char *e = getenv("DM_FPS"); int v = e && atoi(e) == 30 ? 30 : 60, z = 0; atomic_compare_exchange_strong(&g_fps_target, &z, v); f = atomic_load(&g_fps_target); }
    return f;
}
void mtl_fps_set(int fps) { int v = fps == 30 ? 30 : 60; if (atomic_exchange(&g_fps_target, v) != v) MLOG("frame rate target %d fps", v); }

// ---------- The execution interface (gfx/nvn/nvn_backend.h): what the replay (gfx/nvn/core/exec.c) hands the backend, command by command ----------
void nvn_be_reset(void) { g_pso_dirty = g_dss_dirty = 1; }   // (the state at the start: gfx/nvn/core/state.c st_reset)
void nvn_be_draw(const p_draw *dw, const p_draw_idx *di) { draw(dw, di); }
void nvn_be_dispatch(const p_dispatch *d) { dispatch(d); }
void nvn_be_copy_b2t(const p_copy_b2t *c) { copy_b2t(c); }
void nvn_be_copy_t2b(const p_copy_b2t *c) { copy_t2b(c); }
void nvn_be_copy_t2t(const p_copy_t2t *c) { copy_t2t(c); }
void nvn_be_clear_tex(const p_clear_tex *c) { clear_tex(c); }
uint64_t nvn_be_sub_fence(void) { return g_fence + 1; }   // this submission commits with fence g_fence + 1
void nvn_be_report_counter(const uint8_t *p, const void *a) { uint64_t t, ad; memcpy(&t, a, 8); memcpy(&ad, (const uint8_t *)a + 8, 8); g_cnt_report[t & 31]++;
    PD("Q report type=%llu cmd=%p dst=%#llx", (unsigned long long)t, p, (unsigned long long)ad);
    if (t == NVN_COUNTER_TYPE_SAMPLES_PASSED) { post((post_op){3, NULL, 0, 0, ad, (uint32_t)t, vis_snapshot(), g_sub_work}); g_vis_cur = -1;
        occ_note(ad, g_fence + 1); }   // this submission commits with fence g_fence + 1
    else {   // counter half now, in command order (0: TIMESTAMP / TIMESTAMP_TOP report only a timestamp, the other counters are not emulated: conditional
             // rendering at replay compares it); the timestamp half only when the GPU has run the submission. Written at replay as well, it told the game
             // the GPU had passed that point while the work was still queued: Tears of the Kingdom's frames alternated between two poses / object sets
             // (Zelda's cloak, the cave walls), and waiting for the GPU after every submission made it go away
        if ((t == NVN_COUNTER_TYPE_TIMESTAMP || t == NVN_COUNTER_TYPE_TIMESTAMP_TOP) && counts_on()) { g_cnt_ts++; if (++g_cnt_sub_ts == 2) g_cnt_ts_multi++;
            // The reports of one submission that has several, once, after frame 900: their guest heap offsets for DM_WATCHPOINT (who reads them)
            static uint64_t logged_sub; static int nlog; uint64_t f = atomic_load(&g_cur_frame);
            if (f >= 900 && g_cnt_sub_ts >= 2 && nlog < 12 && (!logged_sub || logged_sub == g_fence + 1)) {
                uint8_t *al, *st, *hp; size_t aln, stn, hpn; svc_guest_layout(&al, &aln, &st, &stn, &hp, &hpn);
                if (!logged_sub) MLOG("counts: the timestamp reports of submission %llu (frame %llu), the first one being %#llx (heap+%#llx)", (unsigned long long)(g_fence + 1),
                                      (unsigned long long)f, (unsigned long long)g_cnt_first_ts, (unsigned long long)(g_cnt_first_ts - (uint64_t)hp));
                logged_sub = g_fence + 1; nlog++;
                MLOG("counts:   report %d type %llu at %#llx (heap+%#llx)", g_cnt_sub_ts, (unsigned long long)t, (unsigned long long)ad, (unsigned long long)(ad - (uint64_t)hp));
                const char *w = getenv("DM_NVN_COUNTS_WATCH"); if (w && atoi(w) == g_cnt_sub_ts) { MLOG("counts: watching loads of report %d's timestamp", g_cnt_sub_ts); dm_watch_load(ad + 8, 8); } }
            if (g_cnt_sub_ts == 1) g_cnt_first_ts = ad; }
        if (ad) { uint64_t z = 0; memcpy((void *)(uintptr_t)ad, &z, sizeof z); }
        mtl_sub_counter(ad, (uint32_t)t); }
    return; }
void nvn_be_reset_counter(const uint8_t *p, const void *a) { uint64_t t; memcpy(&t, a, 8); g_cnt_reset[t & 31]++; PD("Q reset type=%llu cmd=%p", (unsigned long long)t, p);
    if (t == NVN_COUNTER_TYPE_SAMPLES_PASSED) {
        // Counting starts inside a pass that has no visibility buffer: the pass is split here, so the draws after the reset count in a pass
        // that has one (they had counted nothing: the queries read 0 and Tears of the Kingdom culled the objects behind them every other
        // frame - Zelda's cloak and the cave walls flickered)
        if (g_enc && !g_enc_has_vis) rp_end();
        g_vis_on = 1; g_vis_counting = 1; vis_clear(); }
    return; }
void nvn_be_set_rt(const uint8_t *p, const void *a) {
    const p_set_rt *r = a;
    if (defer_begin_q(p)) return;
    int pend = R.clear_d || R.clear_s; for (int i = 0; i < 8; i++) pend |= R.clear_c[i];
    if (g_enc && g_fz.active && g_fz.phase == 1 && fz_reuse_op(p) && !pend && g_enc_mode != 1 && R.has_rt && tbdr_part(TBP_FUSE) && rt_same(&g_fz.rt1, r)) {
        g_rt_merged = 1; if (!g_rt_merge_n++) MLOG("same-target continuation: first case pass %llu", g_stat_pass); PD("M %llu same-target continued pass", g_stat_pass); return; }
    // Do not continue a render pass opened only for a clear; one with fewer depth / stencil attachments can be continued (check the planes before drawing)
    if (g_enc && g_lay.active && !pend && lay_continue(r)) return;   // another layer of the layered pass
    if (g_enc && g_enc_mode != 1 && !pend && R.has_rt && tbdr_part(TBP_CONT) && rt_same(&R.rt, r)) { g_rt_merged = 1; if (!g_rt_merge_n++) MLOG("same-target continuation: first case pass %llu", g_stat_pass); PD("M %llu same-target continued pass", g_stat_pass); R.rt = *r; return; }
    rp_flush(); R.rt = *r; R.has_rt = 1; return;
}
void nvn_be_render_enable(const uint8_t *p, const void *a) { const p_render_enable *r = a;   // SetRenderEnable: off discards clears and draws until it is on again (queue state, kept across command buffers)
    g_render_off = r->mode == 0; g_rc_on_pending = 0;
    if (r->mode >= 2) {   // see occ_pending
        g_rc_n++;
        if (!r->counters || occ_pending(r->counters) || occ_pending(r->counters + 16)) { g_render_off = 0; g_rc_pending++; g_rc_on_pending = 1; RED_ONCE(74, "SetRenderEnableConditional on a samples-passed result still on the GPU: rendering stays enabled (first case)"); }
        else { uint64_t v0, v1; memcpy(&v0, (const void *)(uintptr_t)r->counters, 8); memcpy(&v1, (const void *)(uintptr_t)(r->counters + 16), 8);
            g_render_off = r->mode == 2 ? v0 != v1 : v0 == v1; } }
    if (r->mode >= 2 && r->counters && passdump()) { const uint64_t *cv = (const uint64_t *)(uintptr_t)r->counters;
        PD("e render_enable mode=%d counters=%#llx values %#llx %#llx | %#llx %#llx", r->mode, (unsigned long long)r->counters, (unsigned long long)cv[0], (unsigned long long)cv[1], (unsigned long long)cv[2], (unsigned long long)cv[3]); }
    else PD("e render_enable mode=%d counters=%#llx", r->mode, (unsigned long long)r->counters); return; }
void nvn_be_clear_color(const uint8_t *p, const void *a) {
    const p_clear_color *c = a;
    if (g_render_off) return;   // SetRenderEnable(false) discards clears too
    if (g_rc_on_pending) g_rc_pend_draws++;
    if (g_rt_merged || g_lay.active) rp_end();   // A clear arrived in a continued (or layered) render pass: end it here, and the clear is attached to the start of the next render pass as usual
    if (c->index < 0 || c->index >= R.rt.n || !R.rt.color[c->index]) { RED_ONCE(5, "color clear target %d invalid", c->index); return; }
    if (g_enc || c->mask != 0xf || !scissor_full(R.rt.color[c->index], &R.rt.cview[c->index], R.rt.has_cview[c->index])) { clear_quad(c); return; }
    R.clear_c[c->index] = 1; R.cc[c->index] = nvn_rgba_make(c->rgba[0], c->rgba[1], c->rgba[2], c->rgba[3]);
    return;
}
void nvn_be_clear_ds(const uint8_t *p, const void *a) {
    const p_clear_ds *c = a;
    if (g_render_off) return;
    if (g_rt_merged || g_lay.active) rp_end();
    if (!R.rt.depth || (!c->depth_mask && !(c->stencil_mask & 255))) return;
    // A load action clears the whole plane: inside a pass, under a partial scissor or with a partial stencil mask it is a triangle instead
    // (a scissored depth clear and a partial stencil mask used to clear the whole plane)
    if (g_enc || ((c->stencil_mask & 255) && (c->stencil_mask & 255) != 255) || !scissor_full(R.rt.depth, &R.rt.dview, R.rt.has_dview)) { clear_ds_quad(c); return; }
    if (c->depth_mask) { R.clear_d = 1; R.cd = c->depth; }
    if (c->stencil_mask) { R.clear_s = 1; R.cs = (uint32_t)c->stencil; }
    return;
}
void nvn_be_barrier(const uint8_t *p, const void *a) { int32_t bits; memcpy(&bits, a, 4); if (g_cnt_on > 0) g_cnt_bar_at = g_stat_draw; if (bits & NVN_BARRIER_INVALIDATE_TEXTURE_BIT) g_fbx_bar_at = g_stat_draw; return; }
void nvn_be_set_pool(const uint8_t *p, const void *a) {   // SetTexturePool / SetSamplerPool
    op_hdr h; memcpy(&h, p, sizeof h);
    if (h.op == OP_SET_TEX_POOL) { const dpool_t *d = ((const p_set_pool *)a)->pool; if (d) { if (g_tex_ns != d->base) g_reg_gen++; g_tex_ns = d->base; if (d->n) g_tpool_n = d->n; } return; }   // (plan item 64e)
    { const dpool_t *d = ((const p_set_pool *)a)->pool; if (d) { if (g_samp_ns != d->base) g_reg_gen++; g_samp_ns = d->base; } return; }
}
