// gfx/nvn/mtl/exec.inc, part of nvn_mtl.m (included there in order, one translation unit): The power-saving mode and the replay loop (mtl_exec)
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

void mtl_exec(const uint8_t *p) {
    static int inited; if (!inited) { inited = 1; state_reset(); }
    static int dump_all = -1; if (dump_all < 0) dump_all = getenv("DM_NVN_PASSDUMP_ALL") != NULL;
    int trace = dump_all && passdump();   // Normal mode does not check the frame range per operation; the full command position is only used for the designated diagnostic frames
    for (;;) {
        op_hdr h; memcpy(&h, p, sizeof h);
        g_exec_cur = p;   // Scan start point of the store lookahead: the operation that ends the render pass (inclusive)
        if (p == g_defer_pend) { for (int gi = 0; gi < 4; gi++) if (g_defer_slots[gi].active && g_defer_slots[gi].p_end_op == p) g_defer_slots[gi].in_p = 0; g_defer_pend = NULL; }
        sdx_seq(p);
        if (trace) PD("o cmd=%p pass=%llu op=%u size=%u", p, g_stat_pass, (unsigned)h.op, (unsigned)h.size);
        if (g_hoist_n && hoist_take(p)) { if (g_fz.active && p == g_fz.rt2_op) { g_fz.phase = 2; g_rt_merged = 0; PD("F %llu merged pass enters the later pass's draws", g_stat_pass); } p += h.size; continue; }   // A pure-clear render pass already executed early / the start of a later pass already merged in   // Entering the later pass: the same-target continuation of the earlier pass ends here; the later pass reading the earlier pass's attachment is proven to be same-pixel by fz_p2_draw, and is no longer split by the continuation rule
        const void *a = p + 8;
        switch (h.op) {   // work for the counter timestamps' position (nvn_ts.h)
            case OP_DRAW: case OP_DRAW_IDX: case OP_DISPATCH: case OP_COPY_B2B: case OP_COPY_B2T: case OP_COPY_T2B: case OP_COPY_T2T:
            case OP_CLEAR_COLOR: case OP_CLEAR_DS: case OP_CLEAR_TEX: g_sub_work++; break;
            default: break;
        }
        switch (h.op) {
            case OP_END: g_exec_cur = NULL;
                if (g_hoist_n && !g_ret_n && !g_nrest) {
                    if (g_fz.active) fz_unskip();
                    if (g_hoist_n) { MLOG("[red] pure-clear pass hoist: %d hoisted ops not reached at the end of the submit, clearing (should not happen)", g_hoist_n); g_hoist_n = 0; }
                }
                return;   // An end-of-pass triggered outside a list (present / CPU readback etc.) must not scan expired command memory: a null start point means store everything
            case OP_BEGIN: break;
            case OP_JUMP: { const uint8_t *t; memcpy(&t, a, 8); p = t; continue; }
            case OP_CALL: { uint64_t t; memcpy(&t, a, 8);
                if (g_ret_n >= 64) { RED_ONCE(106, "a CALL 64 lists deep (a call cycle?): not followed (first case)"); break; }   // (plan item 64b; it recursed until the stack ran out)
                if (g_ret_n < 16) g_ret[g_ret_n] = p + h.size; g_ret_n++;   // Return stack for lookahead: after a sub-list ends, continue looking at the caller
                mtl_exec((const uint8_t *)t); g_ret_n--; break; }
            case OP_SIGNAL_EVENT: { volatile uint32_t *val; uint32_t m, v; memcpy(&val, a, 8); memcpy(&m, (const uint8_t *)a + 8, 4); memcpy(&v, (const uint8_t *)a + 12, 4);
                PD("Y signal_event cmd=%p dst=%p mode=%u value=%u", p, val, m, v); mtl_sub_event(val, m, v); break; }
            case OP_FENCE_SYNC: { sync_t *sy; memcpy(&sy, a, 8);   // this submission commits with fence g_fence + 1
                if (g_cap_cur) {   // the translation thread: the capture fenced it to this submission's token; a later submission's token stays (a newer FenceSync)
                    uint64_t want = g_cap_cur->token | SYNC_TOKEN;
                    __atomic_compare_exchange_n(&sy->fence, &want, g_fence + 1, 0, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
                    break; }
#ifndef DM_TEST_FENCE_SYNC_LAST   // negative control of t_rqueue only: the last submission's fence, as a fence read before its own is published
                sync_fence_store(sy, g_fence + 1);
#else
                sync_fence_store(sy, g_fence);
#endif
                break; }
            case OP_REPORT_COUNTER: { uint64_t t, ad; memcpy(&t, a, 8); memcpy(&ad, (const uint8_t *)a + 8, 8); g_cnt_report[t & 31]++;
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
                break; }
            case OP_RESET_COUNTER: { uint64_t t; memcpy(&t, a, 8); g_cnt_reset[t & 31]++; PD("Q reset type=%llu cmd=%p", (unsigned long long)t, p);
                if (t == NVN_COUNTER_TYPE_SAMPLES_PASSED) {
                    // Counting starts inside a pass that has no visibility buffer: the pass is split here, so the draws after the reset count in a pass
                    // that has one (they had counted nothing: the queries read 0 and Tears of the Kingdom culled the objects behind them every other
                    // frame - Zelda's cloak and the cave walls flickered)
                    if (g_enc && !g_enc_has_vis) rp_end();
                    g_vis_on = 1; g_vis_counting = 1; vis_clear(); }
                break; }
            case OP_COPY_B2B: { uint64_t s_, d, n; memcpy(&s_, a, 8); memcpy(&d, (const uint8_t *)a + 8, 8); memcpy(&n, (const uint8_t *)a + 16, 8);
                PD("Y copy_b2b cmd=%p src=%#llx dst=%#llx bytes=%llu", p, (unsigned long long)s_, (unsigned long long)d, (unsigned long long)n); mtl_sub_copy(s_, d, n); break; }
            case OP_UPDATE_UB: { uint64_t d, n; memcpy(&d, a, 8); memcpy(&n, (const uint8_t *)a + 8, 8);
                const uint8_t *data = (const uint8_t *)a + 16;
                if (passdump()) { char fv[400] = ""; size_t fo = 0; for (uint64_t q = 0; q + 4 <= n && q < 64; q += 4) { float f; memcpy(&f, data + q, 4); fo += snprintf(fv + fo, sizeof fv - fo, " %g", f); }
                    PD("Y update_ub cmd=%p dst=%#llx bytes=%llu data%s", p, (unsigned long long)d, (unsigned long long)n, fv); }
                mtl_sub_update(data, d, n);
                break; }
            case OP_SET_RT: {
                const p_set_rt *r = a;
                if (defer_begin_q(p)) break;
                int pend = R.clear_d || R.clear_s; for (int i = 0; i < 8; i++) pend |= R.clear_c[i];
                if (g_enc && g_fz.active && g_fz.phase == 1 && fz_reuse_op(p) && !pend && g_enc_mode != 1 && R.has_rt && tbdr_part(TBP_FUSE) && rt_same(&g_fz.rt1, r)) {
                    g_rt_merged = 1; if (!g_rt_merge_n++) MLOG("same-target continuation: first case pass %llu", g_stat_pass); PD("M %llu same-target continued pass", g_stat_pass); break; }
                // Do not continue a render pass opened only for a clear; one with fewer depth / stencil attachments can be continued (check the planes before drawing)
                if (g_enc && g_lay.active && !pend && lay_continue(r)) break;   // another layer of the layered pass
                if (g_enc && g_enc_mode != 1 && !pend && R.has_rt && tbdr_part(TBP_CONT) && rt_same(&R.rt, r)) { g_rt_merged = 1; if (!g_rt_merge_n++) MLOG("same-target continuation: first case pass %llu", g_stat_pass); PD("M %llu same-target continued pass", g_stat_pass); R.rt = *r; break; }
                rp_flush(); R.rt = *r; R.has_rt = 1; break;
            }
            case OP_RENDER_ENABLE: { const p_render_enable *r = a;   // SetRenderEnable: off discards clears and draws until it is on again (queue state, kept across command buffers)
                g_render_off = r->mode == 0; g_rc_on_pending = 0;
                if (r->mode >= 2) {   // see occ_pending
                    g_rc_n++;
                    if (!r->counters || occ_pending(r->counters) || occ_pending(r->counters + 16)) { g_render_off = 0; g_rc_pending++; g_rc_on_pending = 1; RED_ONCE(74, "SetRenderEnableConditional on a samples-passed result still on the GPU: rendering stays enabled (first case)"); }
                    else { uint64_t v0, v1; memcpy(&v0, (const void *)(uintptr_t)r->counters, 8); memcpy(&v1, (const void *)(uintptr_t)(r->counters + 16), 8);
                        g_render_off = r->mode == 2 ? v0 != v1 : v0 == v1; } }
                if (r->mode >= 2 && r->counters && passdump()) { const uint64_t *cv = (const uint64_t *)(uintptr_t)r->counters;
                    PD("e render_enable mode=%d counters=%#llx values %#llx %#llx | %#llx %#llx", r->mode, (unsigned long long)r->counters, (unsigned long long)cv[0], (unsigned long long)cv[1], (unsigned long long)cv[2], (unsigned long long)cv[3]); }
                else PD("e render_enable mode=%d counters=%#llx", r->mode, (unsigned long long)r->counters); break; }
            case OP_CLEAR_COLOR: {
                const p_clear_color *c = a;
                if (g_render_off) break;   // SetRenderEnable(false) discards clears too
                if (g_rc_on_pending) g_rc_pend_draws++;
                if (g_rt_merged || g_lay.active) rp_end();   // A clear arrived in a continued (or layered) render pass: end it here, and the clear is attached to the start of the next render pass as usual
                if (c->index < 0 || c->index >= R.rt.n || !R.rt.color[c->index]) { RED_ONCE(5, "color clear target %d invalid", c->index); break; }
                if (g_enc || c->mask != 0xf || !scissor_full(R.rt.color[c->index], &R.rt.cview[c->index], R.rt.has_cview[c->index])) { clear_quad(c); break; }
                R.clear_c[c->index] = 1; R.cc[c->index] = nvn_rgba_make(c->rgba[0], c->rgba[1], c->rgba[2], c->rgba[3]);
                break;
            }
            case OP_CLEAR_DS: {
                const p_clear_ds *c = a;
                if (g_render_off) break;
                if (g_rt_merged || g_lay.active) rp_end();
                if (!R.rt.depth || (!c->depth_mask && !(c->stencil_mask & 255))) break;
                // A load action clears the whole plane: inside a pass, under a partial scissor or with a partial stencil mask it is a triangle instead
                // (a scissored depth clear and a partial stencil mask used to clear the whole plane)
                if (g_enc || ((c->stencil_mask & 255) && (c->stencil_mask & 255) != 255) || !scissor_full(R.rt.depth, &R.rt.dview, R.rt.has_dview)) { clear_ds_quad(c); break; }
                if (c->depth_mask) { R.clear_d = 1; R.cd = c->depth; }
                if (c->stencil_mask) { R.clear_s = 1; R.cs = (uint32_t)c->stencil; }
                break;
            }
            case OP_DRAW: g_exec_next = p + h.size; draw(a, NULL); g_exec_next = NULL; break;   // Lookahead start point
            case OP_DRAW_IDX: g_exec_next = p + h.size; draw(NULL, a); g_exec_next = NULL; break;
            // Do not mark dirty if the state did not change (the game often re-sends the same program / state block / vertex format before each draw; the dirty flag only decides whether to re-fetch the pipeline / depth-stencil object, and equal values mean the same object)
            case OP_SCISSOR: case OP_BIND_PROGRAM: case OP_BIND_STATE: case OP_BIND_VATTR: case OP_BIND_VSTREAM: case OP_BIND_VBUF:
            case OP_BIND_UBO: case OP_BIND_SSBO: case OP_BIND_TEX: case OP_BIND_SAMP: case OP_BIND_IMG: case OP_STENCIL:
            case OP_POLY_OFFSET: case OP_VIEWPORTS: case OP_DEPTH_RANGE: case OP_BLEND_COLOR: case OP_COPY_STRIDE: case OP_COPY_IMAGE_STRIDE: case OP_DEPTH_CLAMP: case OP_PRIM_RESTART: case OP_DEPTH_BOUNDS: case OP_RASTER_DISCARD:
                state_op(h.op, a); break;
            case OP_COPY_B2T: copy_b2t(a); break;
            case OP_COPY_T2B: copy_t2b(a); break;
            case OP_COPY_T2T: copy_t2t(a); break;
            case OP_CLEAR_TEX: clear_tex(a); break;
            case OP_DISPATCH: dispatch(a); break;
            case OP_BARRIER: { int32_t bits; memcpy(&bits, a, 4); if (g_cnt_on > 0) g_cnt_bar_at = g_stat_draw; if (bits & NVN_BARRIER_INVALIDATE_TEXTURE_BIT) g_fbx_bar_at = g_stat_draw; break; }
            case OP_SET_TEX_POOL: { const dpool_t *d = ((const p_set_pool *)a)->pool; if (d) { if (g_tex_ns != d->base) g_reg_gen++; g_tex_ns = d->base; if (d->n) g_tpool_n = d->n; } break; }   // (plan item 64e)
            case OP_SET_SAMP_POOL: { const dpool_t *d = ((const p_set_pool *)a)->pool; if (d) { if (g_samp_ns != d->base) g_reg_gen++; g_samp_ns = d->base; } break; }
            default: break;
        }
        p += h.size;
    }
}

