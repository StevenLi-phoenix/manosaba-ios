// gfx/nvn/mtl/exec.inc, part of nvn_mtl.m (included there in order, one translation unit): A submission's context for the failure line (cctx_snap); the power-saving mode; the execution interface (gfx/nvn/nvn_backend.h: the replay loop is gfx/nvn/core/exec.c)
// Context description of one submit (mtl_sub_commit takes one per submit and prints it into the failure line when the command buffer errors): for the GPU memory fault on the first frame entering Travincal, the failure line had only the sequence number and error code, and it was impossible to tell what was in that submit
static const char *cctx_snap(void) {
    static char b[320]; static uint64_t p_pass, p_up, p_reup, p_ser; uint64_t up = 0, reup = 0; mtl_pkg_stats(&up, &reup);
    pthread_mutex_lock(&g_deferred_mu); unsigned long nd = (unsigned long)g_deferred.count; pthread_mutex_unlock(&g_deferred_mu);
    snprintf(b, sizeof b, "frame %llu; since the last submit: passes +%llu, first uploads +%llu, re-uploads +%llu, serial table changes +%llu; deferred-release queue %lu",
             (unsigned long long)atomic_load(&g_cur_frame), (unsigned long long)(g_stat_pass - p_pass), (unsigned long long)(up - p_up), (unsigned long long)(reup - p_reup),
             (unsigned long long)(g_ser_gen - p_ser), nd);
    p_pass = g_stat_pass; p_up = up; p_reup = reup; p_ser = g_ser_gen; return b;
}
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
int nvn_be_pass_mode(void) { return g_enc ? g_enc_mode : -1; }
void nvn_be_pass_flush(void) { rp_flush(); }
void nvn_be_pass_end(void) { rp_end(); }
int nvn_be_layered(void) { return g_lay.active; }
int nvn_be_layer_continue(const p_set_rt *r) { return lay_continue(r); }
void nvn_be_clear_quad(const p_clear_color *c) { clear_quad(c); }
void nvn_be_clear_ds_quad(const p_clear_ds *c) { clear_ds_quad(c); }
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
void nvn_be_barrier(const uint8_t *p, const void *a) { int32_t bits; memcpy(&bits, a, 4); if (g_cnt_on > 0) g_cnt_bar_at = g_stat_draw; if (bits & NVN_BARRIER_INVALIDATE_TEXTURE_BIT) g_fbx_bar_at = g_stat_draw; return; }
void nvn_be_set_pool(const uint8_t *p, const void *a) {   // SetTexturePool / SetSamplerPool
    op_hdr h; memcpy(&h, p, sizeof h);
    if (h.op == OP_SET_TEX_POOL) { const dpool_t *d = ((const p_set_pool *)a)->pool; if (d) { if (g_tex_ns != d->base) g_reg_gen++; g_tex_ns = d->base; if (d->n) g_tpool_n = d->n; } return; }   // (plan item 64e)
    { const dpool_t *d = ((const p_set_pool *)a)->pool; if (d) { if (g_samp_ns != d->base) g_reg_gen++; g_samp_ns = d->base; } return; }
}
