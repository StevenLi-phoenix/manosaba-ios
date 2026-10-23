// gfx/nvn/mtl/draw.inc, part of nvn_mtl.m (included there in order, one translation unit): Draws, dispatches, copies, texel reads and writes, clears
static void draw(const p_draw *dw, const p_draw_idx *di) {
    if (g_render_off) return;   // SetRenderEnable(false)
    if (g_rc_on_pending) g_rc_pend_draws++;
    if (!S.prog) { RED_ONCE(18, "no program bound before draw"); return; }
#ifndef DM_TEST_DBOUNDS_EMPTY_DRAWN   // negative control of gfx/tests t_dbounds only: drawn with the bounds as they are
    if (S.db_en && (S.db_lo > S.db_hi || S.db_lo > 1 || S.db_hi < 0)) return;   // depth bounds that admit no stored depth (plan item 64c): Metal takes only 0 <= lo <= hi <= 1
#endif
    __unsafe_unretained NvProg *pr = prog_get(S.prog);
#ifndef DM_TEST_DISCARD_DRAWN   // negative control of gfx/tests t_nvn64d only: drawn, as before
    if (S.raster_discard) {   // rasterizer discard (plan item 64d): nothing reaches the targets; a vertex stage that may write memory is reported, its writes lost
        __unsafe_unretained NvShader *vs = pr ? pr->sh[0] : nil;
        for (int i = 0; vs && i < vs->rf.nb; i++) if (vs->rf.b[i].kind == NVN_RB_IMG || vs->rf.b[i].kind == NVN_RB_SBUFP || vs->rf.b[i].kind == NVN_RB_SBUFU) {
            RED_ONCE(108, "rasterizer discard with a vertex shader that may write memory (%.16s): left out, its writes lost (first case)", vs->key); break; }
        return;
    }
#endif
    if (g_nupd && !g_defer_replaying) upd_guard_draw(pr, di);   // before its render pass is opened
    if (g_ruler_truth == 0) {   // Ruler negative control (DM_NVN_RULER_NEG=<fragment key>): in the mode under test drop this shader's draws, and the ruler must report non-equivalence
        static const char *neg; static int ng = -1; if (ng < 0) { neg = getenv("DM_NVN_RULER_NEG"); ng = neg && *neg; }
        if (ng && pr) for (int st = 0; st < 5; st++) { __unsafe_unretained NvShader *sh = pr->sh[st]; if (sh && sh->stage == 1 && !strncmp(sh->key, neg, strlen(neg))) { static int once; if (!once++) MLOG("DM_NVN_RULER_NEG: measured mode drops the draws of shader %s", sh->key); return; } }
    }
    if (zskip_can_draw(pr)) {   // Zero-source skip: the output is always 0, writing 0 over 0, so this draw changes no result (see zskip_can_draw)
        g_zskip_count++; { static int once; if (!once++) MLOG("zero-source skip: first case pass %llu", g_stat_pass + 1); }
        PD("N %llu zero-source skip", g_stat_pass + 1); return; }
    g_wide_gpu = 0;
    if (di && di->itype == 0 && di->count > 0 && !g_defer_replaying && pend_hits(di->ibuf, (uint64_t)di->count))   // see wide_u8_gpu; such a draw is not deferred
        g_wide_gpu = wide_u8_gpu(di, S.restart_set && S.restart_en && S.restart_idx == 0xff);
    if (!g_defer_replaying && !g_wide_gpu) {
        if (g_defer.active && g_defer.in_p && !g_defer.at_q) { if (defer_take(dw, di)) return; }
    }
    // Draws that cannot draw anything (both faces culled, empty scissor) return before the render pass is opened: otherwise the render pass is already open and on TBDR the whole attachment is loaded and stored for nothing
    // (Mac character select page: 3 render passes are opened only because of such draws, all loaded, zero draws)
    // Diagnostic switch for measuring an upper bound DM_NVN_SKIP_FS=<fragment key>[,<fragment key>...] (none by default): drops all draws of these fragment shaders (the picture will be wrong; only used in real-device ABA to measure "how much GPU time this class of draws is worth in total")
    { static char sk[16][20]; static int nsk = -1;
      if (nsk < 0) { nsk = 0; for (const char *q = getenv("DM_NVN_SKIP_FS"); q && *q && nsk < 16; q = strchr(q, ',') ? strchr(q, ',') + 1 : q + strlen(q)) { size_t n = strcspn(q, ","); if (n > 16) n = 16; memcpy(sk[nsk], q, n); sk[nsk++][n] = 0; }
                     if (nsk) MLOG("DM_NVN_SKIP_FS: dropping the draws of %d fragment shaders (diagnostic, rendering is wrong)", nsk); }
      if (g_ub_n > 0 && pr->sh[1] && !g_defer_replaying && ub_skip(pr->sh[1]->key)) return;   // In-process alternating measurement: this phase drops this class
      if (nsk && pr->sh[1] && !g_defer_replaying) for (int i = 0; i < nsk; i++) if (!strncmp(pr->sh[1]->key, sk[i], strlen(sk[i]))) return; }
    // Similar diagnostic DM_NVN_SKIP_SMALL=<width>: drop all draws whose target width (by the attached level) is <= this value -- measures how much "dropping tiny render passes in bulk" is worth under the parallel pipeline (fixed per-pass overhead)
    { static int small = -1; if (small < 0) { const char *e = getenv("DM_NVN_SKIP_SMALL"); small = e ? atoi(e) : 0; if (small) MLOG("DM_NVN_SKIP_SMALL: dropping draws whose target width is ≤ %d (diagnostic, rendering is wrong)", small); }
      if (small > 0 && R.has_rt && !g_defer_replaying) { int k = R.rt.n && R.rt.color[0] ? 0 : R.rt.depth ? 8 : -1;
        if (k >= 0) { uint64_t s_; int lv, sl; rt_sub(&R.rt, k, &s_, &lv, &sl); const tex_t *t = k == 8 ? R.rt.depth : R.rt.color[0]; if ((int)MAX(t->d.width >> lv, 1u) <= small) return; } } }
    if (S.poly.cull == NVN_FACE_FRONT_AND_BACK) {   // points and lines have no face (nvn_obj.h): drawn
        if (nvn_prim_has_face(dw ? dw->prim : di->prim)) return;
        static int once; if (!once++) MLOG("cull FRONT_AND_BACK on primitive %d (no face): drawn (first case, pass %llu)", dw ? dw->prim : di->prim, g_stat_pass); }
    if (g_up_fs[0][0] && !g_defer_replaying && fxp_dock(pr)) return;   // MetalFX wrote the canvas in this draw's place (plan item 63)
    if (g_fxp.sub) fxp_sub_end();
    if (g_up_fs[1][0] && !g_defer_replaying) fxp_scene(pr);   // MetalFX upscaled the scene this draw samples (plan item 63)
    // The continued render pass may have been opened with fewer depth / stencil attachments (open modes 2 / 3 / 4): if the plane this draw needs is not attached, split before it (on reopen, attach per the new lookahead)
    if ((g_rt_merged || (g_enc && g_ren_pass == g_pass.serial)) && g_enc_mode != 0 && (((S.ds.dt || S.ds.dw) && !g_pass.hd) || (S.ds.st && !g_pass.hs))) {   // (a pass that went on past a renamed update: its lookahead did too, this is a safety net)
        static int once2; if (!once2++) MLOG("same-target continuation: a draw in the continued pass needs a depth / stencil plane that is not attached, splitting here (first case pass %llu)", g_stat_pass);
        PD("M %llu a draw in the continued pass needs an unattached depth / stencil plane, splitting here", g_stat_pass); rp_end(); }
    if (g_rt_range_active && (!rt_range_pr_ro(pr) || !S.ds.dt || S.ds.dw || S.ds.st)) rp_end();   // The external write visibility at the original encoder boundary is not removed
    if (g_lay.active && (!lay_prim_ok(dw ? dw->prim : di->prim) || lay_reads_target(pr))) { g_lay.splits++; g_lay.refuse = 1; rp_end(); }   // a draw a layered pass cannot hold: the pass ends, the next opens plain
    if (g_rt_merged && draw_samples_rt(pr)) { static int once; if (!once++) MLOG("same-target continuation: a draw in the continued pass samples an attachment, splitting here (first case pass %llu)", g_stat_pass);
        PD("M %llu a draw in the continued pass samples an attachment, splitting here", g_stat_pass); rp_end(); }
    static id<MTLFunction> fz_fn;   // Specialized fragment function for this draw of the later pass in a merged render pass (nil = use the original version)
    if (g_fz.active && g_enc && g_fz.phase == 2 && !fz_p2_draw(pr, dw, di, g_fz.lmap2, &fz_fn)) {   // Cannot be proven: end the merged render pass (the earlier pass's result is stored as usual) and reopen with the later pass's original targets
        static int once3; if (!once3++) MLOG("readable tile pass merge: a draw in the later pass is not provable, falling back to separate passes (first case pass %llu)", g_stat_pass);
        g_fz_fallback++; rp_end(); }
    if (!g_defer_replaying) fbx_prepare(pr, dw, di); else { g_fbx_self = 0; if (g_fbx_nact) { g_fbx_nact = 0; args_dirty_all(); } }   // a replayed deferred draw: not copied for, and it keeps the draw that replays it's picks
    if (R.has_sc && !g_enc) {
        NSUInteger tw0 = 0, th0 = 0; rt_dims(&tw0, &th0);
        long x0 = R.sc.x < 0 ? 0 : R.sc.x, y0 = R.sc.y < 0 ? 0 : R.sc.y, x1 = (long)R.sc.x + R.sc.w, y1 = (long)R.sc.y + R.sc.h;
        if (x1 > (long)tw0) x1 = tw0; if (y1 > (long)th0) y1 = th0;
        if (x1 <= x0 || y1 <= y0) return;
    }
    int defer_q_draw = !g_enc && !g_defer_replaying && g_defer_natq > 0;   // The draw that opens Q's render pass: after the render pass is opened as usual through hoist_try / fuse_try, replay P first and then draw this one
    if (!g_enc && tbdr_part(TBP_HOIST)) hoist_try();   // First hoist and execute the pure-clear render passes sandwiched between this render pass and its continuation (before rp_begin_ex, not occupying this render pass)
    // The deferral decision comes after hoist_try: hoisted pure-clear render passes are skipped in the lookahead, and only then can P's same-target continuation segment be recognized
    if (!g_enc && tbdr_part(TBP_DEFER) && !defer_has_in_p() && !g_defer_replaying && !g_fz.active && !g_fz_checking && g_exec_next) { defer_try(dw, di); if (g_defer_cur->active && g_defer_cur->in_p) return; }
    if (!g_enc && tbdr_part(TBP_FUSE) && !g_fz_checking) fuse_try();   // Readable tile render pass merge is on by default; when the structure / same-pixel proof does not hold it still falls back to a normal render pass, and it is not re-entered during the CHECK redraw
    int need = g_enc || !tbdr_part(TBP_DS) ? 3 : pass_ds_need(), mode = defer_q_draw ? 0 : need == 0 ? 2 : need == 2 ? 3 : need == 1 ? 4 : 0;   // 2 attach neither, 3 attach stencil only, 4 attach depth only
    int skip_d = mode != 0;
    if (!g_enc) { g_fd_dw = dw; g_fd_di = di; }
    id<MTLRenderCommandEncoder> en = rp_begin_ex(mode);
    g_fd_dw = NULL; g_fd_di = NULL;
    if (!en) return;
    if (defer_q_draw) defer_replay();
    fbx_activate();
    if (skip_d) { g_stat_ds_omit++; if (mode == 3) g_stat_s_only++; else if (mode == 4) g_stat_d_only++; }
    // The pipeline depends only on (program, render pass targets, blend / color / write mask / multisample state, vertex layout): if these did not change, reuse the previous one
    static id<MTLRenderPipelineState> cur_ps; static NvProg *cur_pr; static uint64_t cur_ps_pass;
    if (g_fz.active && !g_defer_replaying) { g_pso_lmap = g_fz.phase == 1 ? g_fz.lmap1 : g_fz.lmap2; g_pso_fsfn = g_fz.phase == 1 ? nil : fz_fn; g_pso_fz = g_fz.phase == 1 ? (pso_fz_desc){0} : g_fz_desc; g_pso_dirty = 1; }
    else if (g_fbx_fn && !g_defer_replaying) { g_pso_lmap = g_fbx_lmap; g_pso_fsfn = g_fbx_fn; g_pso_fz = g_fbx_desc; g_pso_dirty = 1; }   // the readable-tile version fbx_fetch picked (identity mapping)   // Readable tile merge: fetch the pipeline by the physical -> logical mapping and the specialization
    if (g_pso_dirty || pr != cur_pr || cur_ps_pass != g_pass.serial) { cur_ps = pso_get(pr, nil); cur_pr = pr; cur_ps_pass = g_pass.serial; g_pso_dirty = !cur_ps; }
    if (g_pso_lmap) { g_pso_lmap = NULL; g_pso_fsfn = nil; memset(&g_pso_fz, 0, sizeof g_pso_fz); g_pso_dirty = 1; }   // The next draw fetches again according to its own situation
    __unsafe_unretained id<MTLRenderPipelineState> ps = cur_ps;
    if (!ps) return;
    if (ps != g_enc_pso) { enc_r_pso(en, ps); g_enc_pso = ps; }
    static id<MTLDepthStencilState> cur_ds; static uint64_t cur_ds_pass;
    if (g_dss_dirty || cur_ds_pass != g_pass.serial) { cur_ds = dss_get(g_pass.hd, g_pass.hs); cur_ds_pass = g_pass.serial; g_dss_dirty = 0; }
    if (cur_ds != g_enc_dss) { enc_r_dss(en, cur_ds); g_enc_dss = cur_ds; }
    enc_r_stencil_ref(en, S.st_ref[0], S.st_ref[1]);
    if (g_lay.active && g_lay.amp != g_lay.cur - g_lay.first) { g_lay.amp = g_lay.cur - g_lay.first; enc_r_layer(en, (uint32_t)g_lay.amp); }   // this draw's layer in the layered pass
    // Viewport / scissor (clamped within the target size)
    NSUInteger tw = g_pass.tw, th = g_pass.th;
    if (S.has_vp) enc_r_viewport(en, (MTLViewport){S.vp[0][0], S.vp[0][1], S.vp[0][2], S.vp[0][3], S.dr[0], S.dr[1]});
    else enc_r_viewport(en, (MTLViewport){0, 0, tw, th, 0, 1});
    MTLScissorRect sc = {0, 0, tw, th};
    if (R.has_sc) {
        long x0 = R.sc.x < 0 ? 0 : R.sc.x, y0 = R.sc.y <= 0 ? 0 : R.sc.y, x1 = (long)R.sc.x + R.sc.w, y1 = (long)R.sc.y + R.sc.h;
        if (x1 > (long)tw) x1 = tw; if (y1 > (long)th) y1 = th;
        if (x1 <= x0 || y1 <= y0) return;
        sc = (MTLScissorRect){(NSUInteger)x0, (NSUInteger)y0, (NSUInteger)(x1 - x0), (NSUInteger)(y1 - y0)};
    }
    enc_r_scissor(en, sc);
    if (g_cnt_on > 0) { if (S.has_vp) cnt_extent(sc, S.vp[0][0], S.vp[0][1], S.vp[0][0] + S.vp[0][2], S.vp[0][1] + S.vp[0][3]); else cnt_extent(sc, 0, 0, tw, th); }
    if (S.poly.cull == NVN_FACE_FRONT_AND_BACK && nvn_prim_has_face(dw ? dw->prim : di->prim)) return;
    { static int nocull = -1; if (nocull < 0) { nocull = getenv("DM_NVN_NOCULL") != NULL; if (nocull) MLOG("DM_NVN_NOCULL: face culling off (diagnostic, rendering is wrong)"); }   // diagnostic: does a missing draw come back without culling?
      enc_r_cull(en, nocull ? MTLCullModeNone : S.poly.cull == NVN_FACE_FRONT ? MTLCullModeFront : S.poly.cull == NVN_FACE_BACK ? MTLCullModeBack : MTLCullModeNone); }
    enc_r_winding(en, S.poly.front == NVN_FRONT_FACE_CCW ? MTLWindingCounterClockwise : MTLWindingClockwise);
    enc_r_fill(en, S.poly.mode == NVN_POLYGON_MODE_LINE ? MTLTriangleFillModeLines : MTLTriangleFillModeFill);
    if (S.poly.offset_en & 4) enc_r_depth_bias(en, S.po.units, S.po.factor, S.po.clamp); else enc_r_depth_bias(en, 0, 0, 0);
    if (S.db_en) enc_r_depth_bounds(en, S.db_lo > 0 ? S.db_lo : 0, S.db_hi < 1 ? S.db_hi : 1); else enc_r_depth_bounds(en, 0, 1);   // (bounds that admit nothing: draw() returned)
    enc_r_depth_clip(en, !S.depth_clamp);
    enc_r_blend_color(en, S.blend_color[0], S.blend_color[1], S.blend_color[2], S.blend_color[3]);
    // A stream bound to the same address on this encoder is not looked up and set again (plan item 225): every draw set every stream, ~2 a draw
    // and half the translation thread's address lookups in Tears of the Kingdom. Pool changes move addresses: a new generation binds all again
    { uint64_t pg = atomic_load_explicit(&g_pool_gen, memory_order_acquire); es_mine(en); if (g_es.vb_gen != pg) { memset(g_es.vb, 0, sizeof g_es.vb); g_es.vb_gen = pg; } }
    for (int s_ = 0; s_ < 16; s_++) {
        if (!S.vbuf[s_].address) continue;
        void *mb; uint64_t mo;
        uint64_t gpu;
#ifndef DM_TEST_VB_ELIDE_ANY   // negative control of t_vbelide only: a stream bound on this encoder is not set again whatever its address
        int same = g_es.vb[s_] == S.vbuf[s_].address;
#else
        int same = g_es.vb[s_] != 0;
#endif
        if (same) { g_vb_skips++; use_rec("buf", S.vbuf[s_].address, S.vbuf[s_].size, 0, NULL); gw_note(&(p_bind_buf){-1, s_, S.vbuf[s_].address, S.vbuf[s_].size}, 0); continue; }
        if (mtl_gpu_addr(S.vbuf[s_].address, 1, &mb, &mo, &gpu)) { enc_r_vbuf(en, 16 + s_, gpu); g_vb_sets++; use_rec("buf", S.vbuf[s_].address, S.vbuf[s_].size, 0, NULL);
            g_es.vb[s_] = s_ == 14 ? 0 : S.vbuf[s_].address; if (s_ == 14) g_es.vb_zero = 0;   // (index 30: set again for every draw, then the zero buffer over it)
            gw_note(&(p_bind_buf){-1, s_, S.vbuf[s_].address, S.vbuf[s_].size}, 0); }
    }
    if (di) gw_note(&(p_bind_buf){-2, 0, di->ibuf, (uint64_t)di->count * (di->itype == 0 ? 1 : di->itype == 1 ? 2 : 4)}, 0);
    static id<MTLBuffer> zero; if (!zero) { zero = [g_dev newBufferWithLength:64 options:MTLResourceStorageModeShared]; res_add(zero); }
    static uint64_t zero_gpu; if (!zero_gpu) zero_gpu = zero.gpuAddress;
    if (!g_es.vb_zero) { enc_r_vbuf(en, 30, zero_gpu); g_es.vb_zero = 1; }
    if (pr->sh[0]) encode_args(en, pr->sh[0], 0, 1);
    if (pr->sh[1]) encode_args(en, pr->sh[1], 1, 0);
    // Parameter binding captures the actual resources under the registry lock; if the version changed between the eligibility check and the binding, split and retry before any GPU draw.
    if (g_rt_range_active && (g_pass.reg_gen != g_reg_gen || g_pass.ser_gen != g_ser_gen || g_pass.range_dirty != nvn_dirty_now())) { rp_end(); draw(dw, di); return; }
    int prim = dw ? dw->prim : di->prim;
    MTLPrimitiveType mp;
    switch (prim) {
        case 0: mp = MTLPrimitiveTypePoint; break; case 1: mp = MTLPrimitiveTypeLine; break; case 3: mp = MTLPrimitiveTypeLineStrip; break;
        case 4: case 7: mp = MTLPrimitiveTypeTriangle; break; case 5: mp = MTLPrimitiveTypeTriangleStrip; break;
        default: RED_ONCE(19, "primitive type %d not implemented", prim); return;
    }
    if (g_vis_counting) {   // Occlusion query counting: a new slot for this render pass (also switch to a new slot after a report / reset)
        if (!g_enc_has_vis) RED_ONCE(34, "the pass in which occlusion query counting started has no visibility buffer attached (the first reset happened mid-pass), samples of that pass are not counted");
        else {
            if (g_vis_cur < 0 || g_vis_cur_pass != g_pass.serial) {
                if (g_vis_n >= VIS_SLOTS) RED_ONCE(35, "visibility slots exhausted");
                else { g_vis_cur = (int)g_vis_n++; g_vis_cur_pass = g_pass.serial;
                    if (g_nvs == g_vscap) { g_vscap = g_vscap ? g_vscap * 2 : 64; g_vs = realloc(g_vs, g_vscap * sizeof *g_vs); }
                    g_vs[g_nvs++] = (vis_slot){(void *)CFBridgingRetain(g_vis), (uint32_t)g_vis_cur}; }
            }
            if (g_vis_cur >= 0 && g_enc_vis != g_vis_cur + 1) { enc_r_visibility(en, MTLVisibilityResultModeCounting, (NSUInteger)g_vis_cur * 8); g_enc_vis = g_vis_cur + 1; }
        }
    }
    if (g_cnt_on > 0 || (g_cnt_on < 0 && counts_on())) count_self_sample(pr);   // after the splits above: the pass this draw lands in
    g_stat_draw++;
    if (enc_label_on() && !g_el_draws++ && pr->sh[1]) { memcpy(g_el_key, pr->sh[1]->key, 8); g_el_key[8] = 0; }
    if (passdump()) {   // Characteristics of a render pass's first draw (to judge whether the load can be omitted: full screen, no blending, writes all channels, does not depend on depth); with DM_NVN_PASSDUMP_ALL every draw is recorded
        static uint64_t last_pass = ~0ull; static int all = -1; if (all < 0) all = getenv("DM_NVN_PASSDUMP_ALL") != NULL;
        if (all) PD("d %llu bv=%d bi=%d first=%d vs=%s fs=%s vc=%d inst=%d blend=%#x b0=%#llx dt=%d dw=%d df=%d cull=%d st=%d sf=%d,%d sop=%d,%d,%d/%d,%d,%d smask=%#x,%#x/%#x,%#x sref=%#x,%#x ib=%#llx it=%d cmask=%#x", g_stat_pass, di ? di->base_vertex : 0, di ? di->base_instance : 0, dw ? dw->first : 0, pr->sh[0] ? pr->sh[0]->key : "-", pr->sh[1] ? pr->sh[1]->key : "-",
                    dw ? dw->count : di->count, dw ? (dw->instances ? dw->instances : 1) : di->instances, S.color.blend_en, (unsigned long long)*(uint64_t *)&S.blend[0], (int)S.ds.dt, (int)S.ds.dw, (int)S.ds.df, (int)S.poly.cull,
                    (int)S.ds.st, (int)S.ds.sf_f, (int)S.ds.sf_b, (int)S.ds.fail_f, (int)S.ds.zfail_f, (int)S.ds.zpass_f,
                    (int)S.ds.fail_b, (int)S.ds.zfail_b, (int)S.ds.zpass_b, S.st_vmask[0], S.st_vmask[1], S.st_wmask[0], S.st_wmask[1], S.st_ref[0], S.st_ref[1],
                    di ? (unsigned long long)di->ibuf : 0ull, di ? di->itype : 0, S.chmask.mask);
        if (all && pr->sh[0]) {   // Content fingerprint of each constant buffer of the vertex stage (to judge whether a draw is completely identical across frames: used by the shadow cache etc.); when unreadable (a write pending execution in this submit) record P
            char ub[400]; size_t o = 0; NvShader *v = pr->sh[0];
            for (int i = 0; i < v->rf.nb && o < sizeof ub - 40; i++) if (v->rf.b[i].kind == NVN_RB_CBUF && v->rf.b[i].reg < 16) {
                const p_bind_buf *u = &S.ubo[0][v->rf.b[i].reg]; void *mb; uint64_t mo; const uint8_t *base; uint64_t h = 1469598103934665603ull;
                if (!u->addr || !u->size) { o += snprintf(ub + o, sizeof ub - o, " r%d=-", v->rf.b[i].reg); continue; }
                if (pend_hits(u->addr, u->size) || !mtl_addr(u->addr, u->size, &mb, &mo) || !(base = [(__bridge id<MTLBuffer>)mb contents])) { o += snprintf(ub + o, sizeof ub - o, " r%d=P/%llu", v->rf.b[i].reg, (unsigned long long)u->size); continue; }
                uint64_t h64 = 0; for (uint64_t q = 0; q < u->size; q++) { h = (h ^ base[mo + q]) * 1099511628211ull; if (q == 63) h64 = h; }   // h64: the first 64 bytes (world matrix vec0–3 of the object buffer)
                { static uint64_t hp = ~0ull; static int hn; if (hp != g_stat_pass) { hp = g_stat_pass; hn = 0; }   // For the first 6 draws of each render pass, additionally print buffers of <= 608 bytes in hex (to compare byte by byte which fields changed across frames)
                  if (hn < 12 && u->size <= 608 && v->rf.b[i].reg == 3) { hn++; char hx[1300]; for (uint64_t q = 0; q < u->size; q++) snprintf(hx + 2 * q, 3, "%02x", base[mo + q]); PD("x %llu r%d %s", g_stat_pass, v->rf.b[i].reg, hx); } }
                o += snprintf(ub + o, sizeof ub - o, " r%d=%016llx/%llu/%016llx", v->rf.b[i].reg, (unsigned long long)h, (unsigned long long)u->size, (unsigned long long)h64); }
            PD("u %llu%s", g_stat_pass, ub); }
        if (all) { char vb[512]; size_t o = 0; o += snprintf(vb + o, sizeof vb - o, "nattr=%d", S.nattr);
            for (int i = 0; i < S.nattr && i < 6; i++) { int st = S.attr[i].stream;
                o += snprintf(vb + o, sizeof vb - o, " a%d{fmt=%#x off=%d s=%d stride=%u div=%u vb=%#llx/%llu}", i, S.attr[i].fmt, S.attr[i].offset, st,
                              st < 16 ? S.stream[st].stride : 0, st < 16 ? S.stream[st].divisor : 0, st < 16 ? (unsigned long long)S.vbuf[st].address : 0ull, st < 16 ? (unsigned long long)S.vbuf[st].size : 0ull); }
            PD("v %llu %s", g_stat_pass, vb);
            if (S.nattr > 0 && S.attr[0].stream < 16 && S.vbuf[S.attr[0].stream].address && S.attr[0].fmt == 0x29) {   // a0 as RGBA16F: the first 4 vertices
                const uint16_t *hv = (const uint16_t *)(uintptr_t)(S.vbuf[S.attr[0].stream].address + S.attr[0].offset); uint32_t st = S.stream[S.attr[0].stream].stride; char hx[300]; size_t ho = 0;
                for (int k = 0; k < 4 && (uint64_t)k * st + 8 <= S.vbuf[S.attr[0].stream].size; k++) { const uint16_t *e = (const uint16_t *)((const uint8_t *)hv + (size_t)k * st);
                    ho += snprintf(hx + ho, sizeof hx - ho, " (%g %g %g %g)", (double)*(const __fp16 *)&e[0], (double)*(const __fp16 *)&e[1], (double)*(const __fp16 *)&e[2], (double)*(const __fp16 *)&e[3]); }
                PD("v %llu a0 data%s", g_stat_pass, hx); } }
        if (last_pass != g_stat_pass) {
            last_pass = g_stat_pass;
            PD("D %llu prim=%d vc=%d inst=%d blend=%#x wm=%#x dt=%d dw=%d df=%d st=%d sc=%lu,%lu,%lu,%lu tgt=%lux%lu vp=%.0f,%.0f,%.0f,%.0f cull=%d front=%d dr=%.3g,%.3g it=%d", g_stat_pass, prim,
               dw ? dw->count : di->count, dw ? (dw->instances ? dw->instances : 1) : di->instances, S.color.blend_en, S.chmask.mask, (int)S.ds.dt, (int)S.ds.dw, (int)S.ds.df, (int)S.ds.st,
               (unsigned long)sc.x, (unsigned long)sc.y, (unsigned long)sc.width, (unsigned long)sc.height, (unsigned long)tw, (unsigned long)th,
               S.has_vp ? S.vp[0][0] : 0, S.has_vp ? S.vp[0][1] : 0, S.has_vp ? S.vp[0][2] : (float)tw, S.has_vp ? S.vp[0][3] : (float)th, (int)S.poly.cull, (int)S.poly.front, S.dr[0], S.dr[1], di ? di->itype : -1);
        }
    }
    if (rt_range_on() && !rt_range_pr_ro(pr)) g_pass.range_external=1;
    rt_range_note_depth();
    udc_note_draw(pr);
    // A draw with 0 primitives (the game does send count=0) draws no pixels, so skip encoding; Metal requires the count to be non-zero, otherwise the validation layer aborts on the spot (measured on the camp / wilderness / combat pages)
    if (dw && prim == 7) {   // Quads: shared indices 0,1,2,0,2,3
        if (!g_quad_ib) {
            uint32_t n = 65536; uint32_t *ix = malloc(n * 6 * 4);
            for (uint32_t q = 0; q < n; q++) { uint32_t b = q * 4; uint32_t v[6] = {b, b + 1, b + 2, b, b + 2, b + 3}; memcpy(ix + q * 6, v, 24); }
            g_quad_ib = [g_dev newBufferWithBytes:ix length:n * 24 options:MTLResourceStorageModeShared]; g_quad_gpu = g_quad_ib.gpuAddress; res_add(g_quad_ib); free(ix);
        }
        if (dw->count >= 4) enc_r_draw_indexed(en, mp, (dw->count / 4) * 6, MTLIndexTypeUInt32, g_quad_gpu, g_quad_ib.length, dw->instances ? dw->instances : 1, dw->first, dw->base_instance);
    } else if (dw) {
        if (dw->count) enc_r_draw_instanced(en, mp, dw->first, dw->count, dw->instances ? dw->instances : 1, dw->base_instance);
    } else {
        if (prim == 7) { RED_ONCE(20, "indexed quads not implemented"); return; }
        // Primitive restart: NVN restarts any primitive where an index equals the restart index exactly (SDK: an 8-bit index never matches one outside
        // 0..255); Metal restarts only strips, only at the all-ones index of the index type, always. Until the game sets it nothing changes (its default
        // state is not documented). Strips whose restart index is not all-ones get their indices rewritten in this submission's arena; what cannot be
        // expressed (a list with a restart, a strip with restart off holding all-ones) is reported (t_restart)
        int strip = mp == MTLPrimitiveTypeLineStrip || mp == MTLPrimitiveTypeTriangleStrip, rs = S.restart_set && S.restart_en;
        if (di->itype == 0) {   // 8-bit indices (Tears of the Kingdom draws most quads with them): Metal has only 16 / 32-bit index buffers, so they are widened into this submission's arena
            if (!di->count) return;
            const uint8_t *src8 = (const uint8_t *)(uintptr_t)di->ibuf; void *mb; uint64_t mo;
            if (!mtl_addr(di->ibuf, (uint64_t)di->count, &mb, &mo)) { RED_ONCE(21, "8-bit index buffer is not in a memory pool"); return; }
            if (g_wide_gpu) {   // widened on the GPU before the pass (wide_u8_gpu)
                use_rec("buf", di->ibuf, (uint64_t)di->count, 0, NULL);
                enc_r_draw_indexed(en, mp, di->count, MTLIndexTypeUInt16, g_wide_gpu, (NSUInteger)di->count * 2, di->instances ? di->instances : 1, di->base_vertex, di->base_instance);
                g_wide_gpu = 0;
                if (g_rt_range_pending) { PD("j rt_level_range pass=%llu vc=%u", g_stat_pass, (unsigned)di->count); g_rt_range_pending = 0; }
                return;
            }
            if (pend_hits(di->ibuf, (uint64_t)di->count)) RED_ONCE(83, "8-bit indices written by the GPU earlier in this submission are widened from memory (first case %#llx)", (unsigned long long)di->ibuf);
            use_rec("buf", di->ibuf, (uint64_t)di->count, 0, NULL);
            ARENA_OUT id<MTLBuffer> ab; size_t ao = arena_alloc((size_t)di->count * 2, &ab); uint16_t *d16 = (uint16_t *)((uint8_t *)ab.contents + ao);
            int r8 = rs && S.restart_idx == 0xff, hit = 0;
            for (int k = 0; k < di->count; k++) { int r = r8 && src8[k] == 0xff; hit |= r; d16[k] = r ? 0xffff : src8[k]; }
            if (hit && !strip) RED_ONCE(84, "primitive restart in a list (index %#x): Metal restarts only strips, drawn without the restart", S.restart_idx);
            enc_r_draw_indexed(en, mp, di->count, MTLIndexTypeUInt16, g_cur_arena->gpu + ao, (NSUInteger)di->count * 2, di->instances ? di->instances : 1, di->base_vertex, di->base_instance);
        } else {
            void *mb; uint64_t mo;
            uint64_t gpu;
            if (!mtl_gpu_addr(di->ibuf, 1, &mb, &mo, &gpu)) { RED_ONCE(22, "index buffer is not in a memory pool"); return; }
            int w = di->itype == 1 ? 2 : 4; uint32_t ones = w == 2 ? 0xffffu : 0xffffffffu;
            use_rec("buf", di->ibuf, (uint64_t)di->count * (uint64_t)w, 0, NULL);
            NSUInteger len = ((__bridge id<MTLBuffer>)mb).length - mo;
            if (S.restart_set && di->count && (rs ? S.restart_idx != ones : strip && w == 2)) {   // NVN and Metal would restart at different indices
                const uint8_t *src = (const uint8_t *)(uintptr_t)di->ibuf;
                if (pend_hits(di->ibuf, (uint64_t)di->count * (uint64_t)w)) RED_ONCE(85, "indices written by the GPU earlier in this submission are checked for primitive restart in memory (first case %#llx)", (unsigned long long)di->ibuf);
                if (!rs) {   // restart off: an all-ones 16-bit index is a vertex on NVN, a restart in a Metal strip
                    for (int k = 0; k < di->count; k++) { uint16_t v; memcpy(&v, src + 2 * k, 2); if (v == 0xffff) { RED_ONCE(86, "16-bit index 0xffff in a strip with primitive restart off: Metal restarts there"); break; } }
                } else if (!strip) {
                    for (int k = 0; k < di->count; k++) { uint32_t v = 0; memcpy(&v, src + w * k, w); if (v == S.restart_idx) { RED_ONCE(84, "primitive restart in a list (index %#x): Metal restarts only strips, drawn without the restart", S.restart_idx); break; } }
                } else {
                    ARENA_OUT id<MTLBuffer> ab; size_t ao = arena_alloc((size_t)di->count * w, &ab); uint8_t *dst = (uint8_t *)ab.contents + ao;
                    for (int k = 0; k < di->count; k++) { uint32_t v = 0; memcpy(&v, src + w * k, w); if (v == S.restart_idx) v = ones; else if (v == ones) RED_ONCE(54, "all-ones index in a strip whose restart index is %#x: Metal restarts there", S.restart_idx); memcpy(dst + w * k, &v, w); }
                    gpu = g_cur_arena->gpu + ao; len = (NSUInteger)di->count * w;
                }
            }
            if (di->count) enc_r_draw_indexed(en, mp, di->count, w == 2 ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32, gpu, len,
                    di->instances ? di->instances : 1, di->base_vertex, di->base_instance);
        }
    }
    if (g_rt_range_pending) { PD("j rt_level_range pass=%llu vc=%u", g_stat_pass, (unsigned)(dw ? dw->count : di->count)); g_rt_range_pending=0; }
}
static nvn_hmap g_cps = {.klen = sizeof(void *)};
static void warm_claim(void) {
    if (!atomic_exchange_explicit(&g_warm_ready, 0, memory_order_acq_rel)) return;
    os_unfair_lock_lock(&g_warm_lock);
    NSArray<WarmItem *> *items = [g_warm_items copy];
    [g_warm_items removeAllObjects];
    os_unfair_lock_unlock(&g_warm_lock);
    for (WarmItem *item in items) {
        if (item->kind == 0) {
            if (!g_shcache) g_shcache = [NSMutableDictionary dictionary];
            if (!g_shcache[item->key]) { g_shcache[item->key] = item->shader; g_warm_claim_shaders++; }
        } else if (item->kind == 1) {
            if (!g_shcache) g_shcache = [NSMutableDictionary dictionary];
            NvShader *vs = g_shcache[item->vs_key], *fs = item->fs_key.length ? g_shcache[item->fs_key] : nil;
            if (!vs) { vs = item->vs; g_shcache[item->vs_key] = vs; }
            if (item->fs_key.length && !fs) { fs = item->fs; g_shcache[item->fs_key] = fs; }
            item->pso.vs = (__bridge void *)vs; item->pso.fs = (__bridge void *)fs;
            if (!hm_get_id(&g_pso, &item->pso)) { hm_put_id(&g_pso, &item->pso, item->object); g_warm_claim_rps++; }
        } else {
            NvShader *cs = g_shcache[item->key];
            if (!cs) { cs = item->shader; if (!g_shcache) g_shcache = [NSMutableDictionary dictionary]; g_shcache[item->key] = cs; }
            const void *key = (__bridge const void *)cs;
            if (!hm_get_id(&g_cps, &key)) { hm_put_id(&g_cps, &key, item->object); g_warm_claim_cps++; }
        }
    }
}
static void dispatch(const p_dispatch *dp) {
    zskip_clear();
    if (g_nupd) upd_flush();   // compute reads uniforms from memory: renamed updates are written first
    if (!S.prog) { RED_ONCE(27, "no program bound before dispatch"); return; }
    NvShader *cs = prog_get(S.prog)->sh[5];
    if (!cs) { RED_ONCE(28, "dispatched program has no compute shader (or it is not mapped)"); return; }
    const void *k = (__bridge const void *)cs;
    id<MTLComputePipelineState> ps = hm_get_id(&g_cps, &k);
    if (!ps) {
        NSError *e = nil; uint64_t bt = bp_now(); int gave_up; id<MTLFunction> fn = cs->fn;
        ps = mtl_retry(^id(NSError **er) { return [g_dev newComputePipelineStateWithFunction:fn error:er]; }, &e, &gave_up);
        g_bp_pso_n++; g_bp_pso_ns += bp_now() - bt;
        if (!ps) { MLOG("[red] failed to create compute pipeline: %s", e.localizedDescription.UTF8String); return; }
        pso_record("C\t%s\n", cs->key);
        if (cs->key[0]) pso_user_record("C\t%s\n", cs->key);
        hm_put_id(&g_cps, &k, ps);
    }
    const uint16_t *tg = cs->rf.tg;
    if (!tg[0]) { RED_ONCE(29, "compute shader lacks threadgroup size reflection"); return; }
#if DM_NVN_M3
    rp_end(); if (g_blit) { enc_b_end(g_blit); g_blit = nil; }
    chunk_maybe_split();
    args_dirty_all(); CENC ce = enc_c_begin(pg_compute_desc());
#else
    // Back-to-back dispatches share one compute encoder (MacNativeGames, MIT): on Metal 4 the copy encoder g_blit is a compute encoder, kept open across
    // copies and closed wherever another encoder starts or the command buffer is committed; a dispatch uses it too, after a barrier that orders the blit
    // and dispatch stages with device visibility, as the end / start barriers of two encoders did (blit()). The argument table is set for every dispatch
    // (copies use none). Each dispatch opened and ended an encoder of its own (Tears of the Kingdom: about 24 a world frame, 13 right after another)
    rp_end(); int open = g_blit != nil;
    CENC ce = blit(); [ce setArgumentTable:g_ct]; args_dirty_all(); g_disp_n++; g_disp_shared += open;
#endif
    enc_c_pso(ce, ps);
    g_dispatch_grid[0] = (uint32_t)dp->x; g_dispatch_grid[1] = (uint32_t)dp->y; g_dispatch_grid[2] = (uint32_t)dp->z;   // c0 + 0x200 (drv_fill)
    encode_args(ce, cs, 5, 0);
#if !DM_NVN_M3
    int pg_slot = pg_dispatch_begin(ce);
#endif
    enc_c_dispatch(ce, MTLSizeMake(dp->x, dp->y, dp->z), MTLSizeMake(tg[0], tg[1], tg[2]));
#if DM_NVN_M3
    enc_c_end(ce);
#else
    pg_dispatch_end(ce, pg_slot);
#endif
}
// GPU: texture -> buffer (row / image strides from SetCopyRowStride / SetCopyImageStride, 0 = tightly packed; nvn_copy_pitch.h)
static void copy_t2b(const p_copy_b2t *c) {
    if (c->tex) xv_check(c->tex->serial, c->has_view && c->view.set_levels ? c->view.base_level : 0, c->has_view && c->view.set_layers ? c->view.min_layer : 0, "texture-to-buffer copy");
    id<MTLTexture> t = tex_mtl(c->tex);
    if (!t) return;
    int bw, bh, bpb = nvn_fmt_bpb(c->tex->d.format, &bw, &bh);
    unsigned level = c->has_view && c->view.set_levels ? c->view.base_level : 0, layer0 = c->has_view && c->view.set_layers ? c->view.min_layer : 0;
    size_t row, img, extent;
    if (!nvn_copy_pitch(c->r.width, c->r.height, c->r.depth, bw, bh, bpb, S.copy_stride, S.copy_image_stride, &row, &img, &extent)) {
        RED_ONCE(67, "texture→buffer copy with invalid strides (row %lld, image %lld, %dx%dx%d)", (long long)S.copy_stride, (long long)S.copy_image_stride, c->r.width, c->r.height, c->r.depth); return; }
    int is3d = c->tex->d.target == 2, nz = c->r.depth ? c->r.depth : 1;
    void *mb; uint64_t mo;
    if (!mtl_addr(c->src, extent, &mb, &mo)) { RED_ONCE(55, "texture→buffer copy destination is not in a memory pool"); return; }
    { uint32_t upf; int ubpp, r_, s_;   // no BC: the texture holds decompressed texels or ASTC blocks, not the BC blocks the copy's pitch is for
      if (bc_unpack(c->tex->d.format, &upf, &ubpp) || bc1_astc(c->tex->d.format, &r_, &s_)) { RED_ONCE(104, "texture→buffer copy of BC format %#x on a GPU without BC: not done (first case)", c->tex->d.format); return; } }
    us_drop(c->src, extent);
    BENC b = blit();
    if (is3d)
        enc_b_copy_texture_buffer(b, t, 0, level, MTLOriginMake(c->r.xoffset, c->r.yoffset, c->r.zoffset), MTLSizeMake(c->r.width, c->r.height, nz), (__bridge id<MTLBuffer>)mb, mo, row, img);
    else
        for (int z = 0; z < nz; z++)
            enc_b_copy_texture_buffer(b, t, layer0 + c->r.zoffset + z, level, MTLOriginMake(c->r.xoffset, c->r.yoffset, 0), MTLSizeMake(c->r.width, c->r.height, 1), (__bridge id<MTLBuffer>)mb, mo + z * img, row, img);
}
// CPU texture read (ReadTexels): for packaged textures the pool memory is the ground truth, de-layout and read directly; the rest are read back from the MTLTexture by blit (the caller has already waited for the GPU per NVN rules)
// CPU Read / WriteTexels on storage (plan item 206): the region's bytes alone (nvn_region_move; MacNativeGames PR #185, MIT: frames over 40 ms
// 25.9 → 1.0 a minute on an M5 Max, 1.1.2, a 64x64 region of a 2048² texture 1.9 → 0.0025 ms), the whole level as before when the region cannot
// be moved alone; counted, a line at the 1st, 2nd, 4th, ... call
static uint64_t g_tx_n, g_tx_region, g_tx_level, g_tx_bytes, g_tx_ns;
static void texels_count(int whole, size_t bytes, uint64_t t0) {
    uint64_t n = ++g_tx_n; whole ? g_tx_level++ : g_tx_region++; g_tx_bytes += bytes; g_tx_ns += bp_now() - t0;
    if (!(n & (n - 1))) MLOG("CPU Read / WriteTexels on storage: %llu calls, %llu by region, %llu by whole level, %.1f MB moved, %.2f ms in all",
                             (unsigned long long)n, (unsigned long long)g_tx_region, (unsigned long long)g_tx_level, g_tx_bytes / 1048576.0, g_tx_ns / 1e6);
}
static int texels_region(const tex_t *t, unsigned level, unsigned layer0, const NVNcopyRegion *r, uint8_t *lin, size_t row, size_t img, int to_linear) {
#ifdef DM_TEST_TEXELS_WHOLE_LEVEL   // negative control of t_writetexels only: the whole level, as before
    (void)t; (void)level; (void)layer0; (void)r; (void)lin; (void)row; (void)img; (void)to_linear; return -1;
#else
    int bw, bh, bpb = nvn_fmt_bpb(t->d.format, &bw, &bh); if (!bpb) return -1;
    uint32_t x0 = (uint32_t)(r->xoffset / bw) * (uint32_t)bpb, y0 = (uint32_t)(r->yoffset / bh), wb = (uint32_t)((r->width + bw - 1) / bw) * (uint32_t)bpb, hb = (uint32_t)((r->height + bh - 1) / bh);
    uint32_t nz = r->depth ? (uint32_t)r->depth : 1;
    if (t->d.target == 2) return nvn_region_move(&t->d, level, 0, t->mem, lin, row, img, x0, y0, (uint32_t)r->zoffset, wb, hb, nz, to_linear);
    for (uint32_t z = 0; z < nz; z++) if (nvn_region_move(&t->d, level, layer0 + (unsigned)r->zoffset + z, t->mem, lin + z * img, row, img, x0, y0, 0, wb, hb, 1, to_linear)) return -1;
    return 0;
#endif
}
void mtl_read_texels(const tex_t *t, unsigned level, unsigned layer0, const NVNcopyRegion *r, uint8_t *dst, size_t row, size_t img) { @autoreleasepool {
    xv_check(t->serial, level, layer0, "CPU readback");
    PD("U cpu_read ser=%llu lv%u sl%u xyz=%d,%d,%d size=%d,%d,%d dst=%p row=%lu img=%lu", t->serial, level, layer0, r->xoffset, r->yoffset, r->zoffset, r->width, r->height, r->depth, dst, (unsigned long)row, (unsigned long)img);
    int bw, bh, bpb = nvn_fmt_bpb(t->d.format, &bw, &bh);
    size_t wb = (size_t)((r->width + bw - 1) / bw) * bpb, hb = (r->height + bh - 1) / bh, x0 = (size_t)(r->xoffset / bw) * bpb, y0 = r->yoffset / bh;
    int nz = r->depth ? r->depth : 1, is3d = t->d.target == 2;
    if (tex_membacked(t)) {   // packaged or CPU-filled: the pool memory is the content
        uint64_t t0 = bp_now();
        if (!texels_region(t, level, layer0, r, dst, row, img, 1)) { texels_count(0, wb * hb * nz, t0); return; }
        uint32_t lw = MAX(t->d.width >> level, 1u), lh = MAX(t->d.height >> level, 1u), lz = is3d ? MAX(t->d.depth >> level, 1u) : 1;
        size_t lp = (size_t)((lw + bw - 1) / bw) * bpb, lr = (lh + bh - 1) / bh;
        uint8_t *lin = malloc(lp * lr * lz);
        for (int z = 0; z < nz; z++) {
            unsigned layer = is3d ? 0 : layer0 + r->zoffset + z;
            if ((is3d && z == 0) || !is3d) if (nvn_deswizzle(&t->d, level, layer, t->mem, lin, lp)) { RED_ONCE(56, "ReadTexels deswizzle failed"); free(lin); return; }
            const uint8_t *src = lin + (is3d ? (size_t)(r->zoffset + z) * lp * lr : 0);
            for (size_t y = 0; y < hb; y++) memcpy(dst + z * img + y * row, src + (y0 + y) * lp + x0, wb);
        }
        free(lin); texels_count(1, lp * lr * lz, t0);
        return;
    }
    id<MTLTexture> mt = tex_mtl(t);
    if (!mt) { memset(dst, 0, img * nz); return; }
    { uint32_t upf; int ubpp, r_, s_;   // no BC (as copy_t2b)
      if (bc_unpack(t->d.format, &upf, &ubpp) || bc1_astc(t->d.format, &r_, &s_)) { RED_ONCE(105, "ReadTexels of BC format %#x on a GPU without BC: zeros (first case)", t->d.format); memset(dst, 0, img * nz); return; } }
    mtl_finish_gpu();
    id<MTLBuffer> buf = [g_dev newBufferWithLength:wb * hb * nz options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> cb = q_cb(); id<MTLBlitCommandEncoder> b = enc_b_begin(cb);
    for (int z = 0; z < nz; z++)
        [b copyFromTexture:mt sourceSlice:is3d ? 0 : layer0 + r->zoffset + z sourceLevel:level sourceOrigin:MTLOriginMake(r->xoffset, r->yoffset, is3d ? r->zoffset + z : 0)
                sourceSize:MTLSizeMake(r->width, r->height, 1) toBuffer:buf destinationOffset:z * wb * hb destinationBytesPerRow:wb destinationBytesPerImage:wb * hb];
    enc_b_end(b); [cb commit]; [cb waitUntilCompleted];
    for (int z = 0; z < nz; z++) for (size_t y = 0; y < hb; y++) memcpy(dst + z * img + y * row, (uint8_t *)buf.contents + z * wb * hb + y * wb, wb);
} }
// A region of packed rows into a texture, through a shared staging buffer, committed synchronously (later submissions are queued after it on the same queue)
static void region_into(id<MTLTexture> mt, unsigned level, unsigned layer0, const NVNcopyRegion *r, const uint8_t *src, size_t row, size_t img, size_t wb, size_t hb, int nz, int is3d) {
    id<MTLBuffer> buf = [g_dev newBufferWithLength:wb * hb * nz options:MTLResourceStorageModeShared];
    for (int z = 0; z < nz; z++) for (size_t y = 0; y < hb; y++) memcpy((uint8_t *)buf.contents + z * wb * hb + y * wb, src + z * img + y * row, wb);
    id<MTLCommandBuffer> cb = q_cb(); id<MTLBlitCommandEncoder> b = enc_b_begin(cb);
    for (int z = 0; z < nz; z++)
        [b copyFromBuffer:buf sourceOffset:z * wb * hb sourceBytesPerRow:wb sourceBytesPerImage:wb * hb sourceSize:MTLSizeMake(r->width, r->height, 1)
                toTexture:mt destinationSlice:is3d ? 0 : layer0 + r->zoffset + z destinationLevel:level destinationOrigin:MTLOriginMake(r->xoffset, r->yoffset, is3d ? r->zoffset + z : 0)];
    enc_b_end(b); [cb commit]; [cb waitUntilCompleted];
}
static uint64_t g_flush_up;          // FlushTexels regions taken from storage (mtl_flush_texels)
// CPU texture write (WriteTexels): for packaged textures the pool memory is the ground truth -- deswizzle this level / layer, patch the region, swizzle back; the pool write triggers a re-upload through dirty-page tracking;
// the rest are blitted into the MTLTexture through a shared staging buffer, committed synchronously (later submissions are queued after it on the same queue)
void mtl_write_texels(const tex_t *t, unsigned level, unsigned layer0, const NVNcopyRegion *r, const uint8_t *src, size_t row, size_t img) { @autoreleasepool {
    int bw, bh, bpb = nvn_fmt_bpb(t->d.format, &bw, &bh);
    if (!bpb) { RED_ONCE(64, "WriteTexels unknown format %#x", t->d.format); return; }
    size_t wb = (size_t)((r->width + bw - 1) / bw) * bpb, hb = (r->height + bh - 1) / bh, x0 = (size_t)(r->xoffset / bw) * bpb, y0 = r->yoffset / bh;
    int nz = r->depth ? r->depth : 1, is3d = t->d.target == 2;
    if (tex_membacked(t)) {   // packaged or CPU-filled: patch the pool memory, dirty-page tracking re-uploads it
        uint64_t t0 = bp_now();
        if (!texels_region(t, level, layer0, r, (uint8_t *)src, row, img, 0)) { texels_count(0, wb * hb * nz, t0); return; }
        uint32_t lw = MAX(t->d.width >> level, 1u), lh = MAX(t->d.height >> level, 1u), lz = is3d ? MAX(t->d.depth >> level, 1u) : 1;
        size_t lp = (size_t)((lw + bw - 1) / bw) * bpb, lr = (lh + bh - 1) / bh;
        uint8_t *lin = malloc(lp * lr * lz);
        for (int z = 0; z < nz; z++) {
            unsigned layer = is3d ? 0 : layer0 + r->zoffset + z;
            if ((is3d && z == 0) || !is3d) if (nvn_deswizzle(&t->d, level, layer, t->mem, lin, lp)) { RED_ONCE(65, "WriteTexels deswizzle failed format %#x target %d", t->d.format, t->d.target); free(lin); return; }
            uint8_t *dst = lin + (is3d ? (size_t)(r->zoffset + z) * lp * lr : 0);
#ifndef DM_TEST_WT_ROWS_AT_REGION   // negative control of t_writetexels only: rows placed at the region's pitch, not the level's
            for (size_t y = 0; y < hb; y++) memcpy(dst + (y0 + y) * lp + x0, src + z * img + y * row, wb);
#else
            for (size_t y = 0; y < hb; y++) memcpy(dst + (y0 + y) * wb + x0, src + z * img + y * row, wb);
#endif
            if ((is3d && z == nz - 1) || !is3d) if (nvn_swizzle(&t->d, level, layer, lin, lp, t->mem)) { RED_ONCE(57, "WriteTexels swizzle failed format %#x target %d", t->d.format, t->d.target); free(lin); return; }
        }
        free(lin); texels_count(1, 2 * lp * lr * lz, t0);
        return;
    }
    uint32_t upf; int ubpp;
    { int r_, s_; if (bc_unpack(t->d.format, &upf, &ubpp) || bc1_astc(t->d.format, &r_, &s_)) { RED_ONCE(66, "WriteTexels: compressed format %#x on a device without BC not implemented", t->d.format); return; } }
    id<MTLTexture> mt = tex_mtl(t);
    if (!mt) return;
    region_into(mt, level, layer0, r, src, row, img, wb, hb, nz, is3d);
    pthread_mutex_lock(&g_cpuf_mu);   // the MTLTexture holds these texels, the storage does not: the FlushTexels after it takes nothing from storage
    if (!g_wt_since) g_wt_since = CFSetCreateMutable(NULL, 0, NULL);
    CFSetAddValue(g_wt_since, (const void *)(uintptr_t)t->serial);
    pthread_mutex_unlock(&g_cpuf_mu);
} }
// FlushTexels (plan item 64j; dr3-mobile's reading of it, MIT, written anew): on the console the CPU writes a texture's storage and FlushTexels
// makes the region visible to the GPU. A texture whose storage is its content here (packaged, CPU-filled, linear on its pool buffer) needs nothing:
// its pages are watched or read in place. One whose content is its MTLTexture (the GPU has written it) takes the flushed region from its storage,
// unless WriteTexels wrote it since its last flush: ours writes such a texture's MTLTexture and not its storage (Tears of the Kingdom pairs each of
// its 42 WriteTexels with a FlushTexels; dr3-mobile's game writes the storage itself, 491 flushes before its first frame)
void mtl_flush_texels(const tex_t *t, unsigned level, unsigned layer0, const NVNcopyRegion *r) { @autoreleasepool {
    if (!t || !t->serial || !t->mem || !t->pool || (t->pool->flags & NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS) || t->d.target == NVN_TEXTURE_TARGET_BUFFER
        || (t->d.flags & (NVN_TEXTURE_FLAGS_LINEAR | NVN_TEXTURE_FLAGS_LINEAR_RENDER_TARGET)) || !r->width || !r->height || tex_membacked(t)) return;
    pthread_mutex_lock(&g_cpuf_mu);
    int wt = g_wt_since && CFSetContainsValue(g_wt_since, (const void *)(uintptr_t)t->serial);
    if (wt) CFSetRemoveValue(g_wt_since, (const void *)(uintptr_t)t->serial);
    pthread_mutex_unlock(&g_cpuf_mu);
#ifndef DM_TEST_FLUSH_AFTER_WT   // negative control of t_flushtexels only: the region taken from storage after a WriteTexels too
    if (wt) return;
#endif
#ifdef DM_TEST_FLUSH_IGNORED   // negative control of t_flushtexels only: FlushTexels does nothing, as before
    return;
#endif
    __unsafe_unretained id have = serial_get(t->serial);
    if (!have || [have isKindOfClass:[NvPkg class]]) return;   // not made yet: a CPU-filled texture's first use reads its storage
    { uint32_t upf; int ubpp, r_, s_; if (bc_unpack(t->d.format, &upf, &ubpp) || bc1_astc(t->d.format, &r_, &s_)) { RED_ONCE(109, "FlushTexels of BC format %#x on a GPU without BC: the region is not taken from storage", t->d.format); return; } }
    int bw, bh, bpb = nvn_fmt_bpb(t->d.format, &bw, &bh);
    if (!bpb) return;
    int nz = r->depth ? r->depth : 1, is3d = t->d.target == 2;
    uint32_t lw = MAX(t->d.width >> level, 1u), lh = MAX(t->d.height >> level, 1u), lz = is3d ? MAX(t->d.depth >> level, 1u) : 1;
    size_t lp = (size_t)((lw + bw - 1) / bw) * bpb, lr = (lh + bh - 1) / bh;
    size_t wb = (size_t)((r->width + bw - 1) / bw) * bpb, hb = (r->height + bh - 1) / bh, x0 = (size_t)(r->xoffset / bw) * bpb, y0 = r->yoffset / bh;
    if (x0 + wb > lp || y0 + hb > lr) return;
    uint8_t *lin = malloc(lp * lr * lz), *reg = malloc(wb * hb * nz);
    for (int z = 0; z < nz; z++) {
        unsigned layer = is3d ? 0 : layer0 + r->zoffset + z;
        if ((is3d && z == 0) || !is3d) if (nvn_deswizzle(&t->d, level, layer, t->mem, lin, lp)) { free(lin); free(reg); return; }
        const uint8_t *from = lin + (is3d ? (size_t)(r->zoffset + z) * lp * lr : 0);
        for (size_t y = 0; y < hb; y++) memcpy(reg + z * wb * hb + y * wb, from + (y0 + y) * lp + x0, wb);
    }
    region_into((id<MTLTexture>)have, level, layer0, r, reg, wb, wb * hb, wb, hb, nz, is3d);
    free(lin); free(reg);
    uint64_t k = ++g_flush_up; if (k <= 4 || !(k % 100)) MLOG("FlushTexels: texture %llu %ux%u format %#x takes a %ux%u region from its storage (%llu so far)", (unsigned long long)t->serial, t->d.width, t->d.height, t->d.format, r->width, r->height, (unsigned long long)k);
} }
// Last buffer→texture copy handed to the driver, for the crash report (crash.c calls mtl_crash_note; plain values only, written before each encode):
// a crash inside the driver's copy encoder was seen a few times in repeated scene loads on the Mac, with a garbage or near-null address
static struct { uint64_t seq, tid, ser, src, off, row, img, buf_len; const void *buf, *tex; unsigned w, h, d, arr, mips, pf, type, storage, usage, fmt, target, cw, ch, cz, x, y, slice, level, in_pools; } g_last_b2t;
void mtl_crash_note(void) {
    if (!g_last_b2t.seq) return;
    fprintf(stderr, "[dm] last buffer→texture copy #%llu (thread %llu): texture %p ser=%llu %ux%ux%u array %u mips %u Metal format %u type %u storage %u usage %#x (NVN format %#x target %u); "
            "buffer %p length %llu offset %llu (guest %#llx, %s the pool table); row %llu image %llu; region %ux%ux%u at %u,%u slice %u level %u\n",
            (unsigned long long)g_last_b2t.seq, (unsigned long long)g_last_b2t.tid, g_last_b2t.tex, (unsigned long long)g_last_b2t.ser, g_last_b2t.w, g_last_b2t.h, g_last_b2t.d, g_last_b2t.arr, g_last_b2t.mips,
            g_last_b2t.pf, g_last_b2t.type, g_last_b2t.storage, g_last_b2t.usage, g_last_b2t.fmt, g_last_b2t.target, g_last_b2t.buf, (unsigned long long)g_last_b2t.buf_len, (unsigned long long)g_last_b2t.off,
            (unsigned long long)g_last_b2t.src, g_last_b2t.in_pools ? "in" : "NOT in", (unsigned long long)g_last_b2t.row, (unsigned long long)g_last_b2t.img, g_last_b2t.cw, g_last_b2t.ch, g_last_b2t.cz,
            g_last_b2t.x, g_last_b2t.y, g_last_b2t.slice, g_last_b2t.level);
}
static int pool_buf_listed(const void *buf) { int hit = 0; pthread_mutex_lock(&g_mu); for (int i = 0; i < g_npool && !hit; i++) hit = (__bridge const void *)g_pools[i].buf == buf; pthread_mutex_unlock(&g_mu); return hit; }
static void b2t_note(const p_copy_b2t *c, id<MTLTexture> t, id<MTLBuffer> b, uint64_t off, size_t row, size_t img, unsigned cz, unsigned slice, unsigned level) {
    static uint64_t seq; uint64_t tid; pthread_threadid_np(NULL, &tid);
    int listed = pool_buf_listed((__bridge const void *)b);
    if (!listed) RED_ONCE(58, "buffer→texture copy: the source buffer %p is no longer in the memory pool table at encode time (texture ser=%llu)", (__bridge const void *)b, c->tex->serial);
    g_last_b2t = (typeof(g_last_b2t)){++seq, tid, c->tex->serial, c->src, off, row, img, (uint64_t)b.length, (__bridge const void *)b, (__bridge const void *)t,
        (unsigned)t.width, (unsigned)t.height, (unsigned)t.depth, (unsigned)t.arrayLength, (unsigned)t.mipmapLevelCount, (unsigned)t.pixelFormat, (unsigned)t.textureType, (unsigned)t.storageMode, (unsigned)t.usage,
        c->tex->d.format, (unsigned)c->tex->d.target, c->r.width, c->r.height, cz, (unsigned)c->r.xoffset, (unsigned)c->r.yoffset, slice, level, (unsigned)listed};
}
static void copy_b2t(const p_copy_b2t *c) {
    if (c->tex) zskip_del(c->tex->serial);
    tex_gpu_write(c->tex);
    id<MTLTexture> t = tex_mtl(c->tex);
    PD("C ser=%llu", c->tex->serial);
    if (!t) return;
    int bw, bh, bpb = nvn_fmt_bpb(c->tex->d.format, &bw, &bh);
    unsigned level = c->has_view && c->view.set_levels ? c->view.base_level : 0, layer0 = c->has_view && c->view.set_layers ? c->view.min_layer : 0;
    size_t row, img, extent;
    if (!nvn_copy_pitch(c->r.width, c->r.height, c->r.depth, bw, bh, bpb, S.copy_stride, S.copy_image_stride, &row, &img, &extent)) {
        RED_ONCE(68, "buffer→texture copy with invalid strides (row %lld, image %lld, %dx%dx%d)", (long long)S.copy_stride, (long long)S.copy_image_stride, c->r.width, c->r.height, c->r.depth); return; }
    int is3d = c->tex->d.target == 2, nz = c->r.depth ? c->r.depth : 1;
    void *mb; uint64_t mo;
    if (!mtl_addr(c->src, extent, &mb, &mo)) { RED_ONCE(23, "buffer→texture copy source is not in a memory pool"); return; }
    uint32_t upf; int ubpp, a1r, a1s;
    if (bc1_astc(c->tex->d.format, &a1r, &a1s)) {   // No BC, BC1: the source's blocks transcoded to ASTC on the CPU into a temporary buffer, copied as blocks
        const uint8_t *src = [(__bridge id<MTLBuffer>)mb contents];
        if (!src) { RED_ONCE(59, "no-BC decompression: the source buffer of the buffer→texture copy is not CPU-readable"); return; }
        uint32_t bw4 = (c->r.width + 3) / 4, bh4 = (c->r.height + 3) / 4; size_t ap = (size_t)bw4 * 16, aimg = ap * bh4;
        id<MTLBuffer> tb = [g_dev newBufferWithLength:aimg * nz ?: 16 options:MTLResourceStorageModeShared];
        for (int z = 0; z < nz; z++) nvn_bc1_astc_image(src + mo + z * img, row, bw4, bh4, (uint8_t *)tb.contents + z * aimg, a1r);
        res_add(tb);
        BENC b = blit();
        if (is3d) enc_b_copy_buffer_texture(b, tb, 0, ap, aimg, MTLSizeMake(c->r.width, c->r.height, nz), t, 0, level, MTLOriginMake(c->r.xoffset, c->r.yoffset, c->r.zoffset));
        else for (int z = 0; z < nz; z++) enc_b_copy_buffer_texture(b, tb, z * aimg, ap, aimg, MTLSizeMake(c->r.width, c->r.height, 1), t, layer0 + c->r.zoffset + z, level, MTLOriginMake(c->r.xoffset, c->r.yoffset, 0));
        defer_obj_release(tb);
        return;
    }
    if (bc_unpack(c->tex->d.format, &upf, &ubpp)) {   // No BC: the compressed blocks in the source are decompressed on the CPU into a temporary buffer, then copied pixel-wise into the decompressed-format texture
        const uint8_t *src = [(__bridge id<MTLBuffer>)mb contents];
        if (!src) { RED_ONCE(59, "no-BC decompression: the source buffer of the buffer→texture copy is not CPU-readable"); return; }
        size_t up = (size_t)c->r.width * ubpp, uimg = up * c->r.height;
        id<MTLBuffer> tb = [g_dev newBufferWithLength:uimg * nz ?: 16 options:MTLResourceStorageModeShared];
        for (int z = 0; z < nz; z++) nvn_bc_decode(c->tex->d.format, src + mo + z * img, row, c->r.width, c->r.height, (uint8_t *)tb.contents + z * uimg, up);
        res_add(tb);
        BENC b = blit();
        if (is3d) enc_b_copy_buffer_texture(b, tb, 0, up, uimg, MTLSizeMake(c->r.width, c->r.height, nz), t, 0, level, MTLOriginMake(c->r.xoffset, c->r.yoffset, c->r.zoffset));
        else for (int z = 0; z < nz; z++) enc_b_copy_buffer_texture(b, tb, z * uimg, up, uimg, MTLSizeMake(c->r.width, c->r.height, 1), t, layer0 + c->r.zoffset + z, level, MTLOriginMake(c->r.xoffset, c->r.yoffset, 0));
        defer_obj_release(tb);   // Release after this submit has finished running (and remove from residency)
        return;
    }
    BENC b = blit();
    if (is3d) {
        b2t_note(c, t, (__bridge id<MTLBuffer>)mb, mo, row, img, (unsigned)nz, 0, level);
        enc_b_copy_buffer_texture(b, (__bridge id<MTLBuffer>)mb, mo, row, img, MTLSizeMake(c->r.width, c->r.height, nz), t, 0, level, MTLOriginMake(c->r.xoffset, c->r.yoffset, c->r.zoffset));
    } else
        for (int z = 0; z < nz; z++) {
            b2t_note(c, t, (__bridge id<MTLBuffer>)mb, mo + z * img, row, img, 1, layer0 + c->r.zoffset + z, level);
            enc_b_copy_buffer_texture(b, (__bridge id<MTLBuffer>)mb, mo + z * img, row, img, MTLSizeMake(c->r.width, c->r.height, 1), t, layer0 + c->r.zoffset + z, level, MTLOriginMake(c->r.xoffset, c->r.yoffset, 0));
        }
}

// No BC + Legacy Device Pack: the game copies textures from files (already ASTC 4x4) whole into a BC3 texture array it creates itself (created as RGBA8 when there is no BC). On Switch the two formats have the same block size and
// the copy moves raw blocks; here it is changed to a compute kernel that decodes pixel by pixel and writes them in, with sRGB sources first read out as linear values and then encoded back to sRGB values, so the result = the values read in the destination format after moving the blocks as-is
static const char k_a2u_msl[] =
"#include <metal_stdlib>\nusing namespace metal;\n"
"static float3 enc(float3 c) { return select(1.055 * pow(c, 1.0 / 2.4) - 0.055, c * 12.92, c <= 0.0031308); }\n"
"kernel void a2u(texture2d<float> src [[texture(0)]], texture2d<float, access::write> dst [[texture(1)]], constant uint4 *p [[buffer(0)]], uint2 g [[thread_position_in_grid]]) {\n"
"  if (g.x >= p[1].x || g.y >= p[1].y) return; float4 v = src.read(g + p[0].xy); if (p[1].z) v.rgb = enc(v.rgb); dst.write(v, g + p[0].zw); }\n";
static int copy_t2t_astc(id<MTLTexture> s, id<MTLTexture> d, unsigned sl, unsigned ss, unsigned dl, unsigned ds, int sx, int sy, int dx, int dy, int w, int h) {
    static int failed; static id<MTLComputePipelineState> ps;
#if !DM_NVN_M3
    static id<MTL4ArgumentTable> at;
#endif
    if (failed) return 0;
    if (!ps) { NSError *e = nil; id<MTLLibrary> lib = [g_dev newLibraryWithSource:@(k_a2u_msl) options:nil error:&e]; id<MTLFunction> f = lib ? [lib newFunctionWithName:@"a2u"] : nil;
        ps = f ? [g_dev newComputePipelineStateWithFunction:f error:&e] : nil;
#if !DM_NVN_M3
        MTL4ArgumentTableDescriptor *ad = [MTL4ArgumentTableDescriptor new]; ad.maxTextureBindCount = 2; ad.maxBufferBindCount = 1; at = [g_dev newArgumentTableWithDescriptor:ad error:&e];
#endif
        if (!ps M4_ONLY(|| !at)) { failed = 1; MLOG("[red] no BC: cannot create the compute pipeline for the ASTC → decompressed format copy %s", e.localizedDescription.UTF8String); return 0; } }
    MTLTextureType tt = MTLTextureType2D;
    id<MTLTexture> sv = [s newTextureViewWithPixelFormat:s.pixelFormat textureType:tt levels:NSMakeRange(sl, 1) slices:NSMakeRange(ss, 1)];
    id<MTLTexture> dv = [d newTextureViewWithPixelFormat:MTLPixelFormatRGBA8Unorm textureType:tt levels:NSMakeRange(dl, 1) slices:NSMakeRange(ds, 1)];
    id<MTLBuffer> pb = [g_dev newBufferWithLength:32 options:MTLResourceStorageModeShared];
    if (!sv || !dv || !pb) { RED_ONCE(47, "no BC: ASTC → decompressed format copy cannot create view / parameter block"); return 0; }
    uint32_t *p = pb.contents; p[0] = sx; p[1] = sy; p[2] = dx; p[3] = dy; p[4] = w; p[5] = h; p[6] = s.pixelFormat == MTLPixelFormatASTC_4x4_sRGB; p[7] = 0;
    res_add(pb);
    rp_end(); if (g_blit) { enc_b_end(g_blit); g_blit = nil; }
    CENC ce = (CENC)enc_c_begin(NULL);
#if DM_NVN_M3
    [ce setTexture:sv atIndex:0]; [ce setTexture:dv atIndex:1]; [ce setBuffer:pb offset:0 atIndex:0];
#else
    [ce setArgumentTable:at];
    [at setTexture:sv.gpuResourceID atIndex:0]; [at setTexture:dv.gpuResourceID atIndex:1]; [at setAddress:pb.gpuAddress atIndex:0];
#endif
    enc_c_pso(ce, ps); enc_c_dispatch(ce, MTLSizeMake((w + 7) / 8, (h + 7) / 8, 1), MTLSizeMake(8, 8, 1)); enc_c_end(ce);
    args_dirty_all();
    defer_obj_unref(sv); defer_obj_unref(dv); defer_obj_release(pb);   // Release after this submit has finished running (views are not made resident separately; the parameter block is removed from residency)
    return 1;
}
// The blocks drawn into an auxiliary texture decoded into the BC texture kept unpacked (plan item 62c3, see blk_dec_kind): the level / slice's blocks
// copied into a buffer, then bcd writes the texels through a 2D view of the texture (an sRGB RGBA8 texture through an RGBA8Unorm view, as the
// upload), or b1a writes ASTC blocks behind them in the buffer, copied into the texture
static void blk_decode(const blk_wb *w) {
#if !DM_NVN_M3
    static id<MTL4ArgumentTable> at; static int failed;
    if (!at && !failed) { NSError *e = nil; MTL4ArgumentTableDescriptor *ad = [MTL4ArgumentTableDescriptor new]; ad.maxTextureBindCount = 1; ad.maxBufferBindCount = 2; at = [g_dev newArgumentTableWithDescriptor:ad error:&e];
        if (!at) { failed = 1; MLOG("[red] no BC: no argument table for decoding drawn blocks %s", e.localizedDescription.UTF8String); } }
    if (!at) return;
#endif
    NSUInteger row = w->bx * w->bpb, size = row * w->by, asz = w->dec == 2 ? w->bx * w->by * 16 : 0;
    id<MTLBuffer> b = [g_dev newBufferWithLength:size + asz options:MTLResourceStorageModePrivate], pb = [g_dev newBufferWithLength:32 options:MTLResourceStorageModeShared];
    id<MTLTexture> v = nil; int is3d = w->dst.textureType == MTLTextureType3D;
    if (w->dec == 1) { MTLPixelFormat vf = w->dst.pixelFormat == MTLPixelFormatRGBA8Unorm_sRGB ? MTLPixelFormatRGBA8Unorm : w->dst.pixelFormat;   // 3D: a 3D view of the level, the plane in the parameters
                       v = is3d ? [w->dst newTextureViewWithPixelFormat:vf textureType:MTLTextureType3D levels:NSMakeRange(w->level, 1) slices:NSMakeRange(0, 1)]
                                : [w->dst newTextureViewWithPixelFormat:vf textureType:MTLTextureType2D levels:NSMakeRange(w->level, 1) slices:NSMakeRange(w->slice, 1)]; }
    if (!b || !pb || (w->dec == 1 && !v)) { RED_ONCE(114, "no BC: drawn blocks not decoded: no buffer or view (first case)"); return; }
    res_add(b); res_add(pb);
    uint32_t *p = pb.contents;
    if (w->dec == 1) { p[0] = 0; p[1] = (uint32_t)row; p[2] = (uint32_t)w->lw; p[3] = (uint32_t)w->lh; p[4] = w->fmt; p[5] = (uint32_t)w->plane;   // bcd: off, pitch, w, h, fmt, z
#ifdef DM_TEST_BLKUNP_3D_PLANE0   // negative control of t_blkunp only: every plane decoded into plane 0
                       p[5] = 0;
#endif
    }
    else { p[0] = 0; p[1] = (uint32_t)row; p[2] = (uint32_t)w->bx; p[3] = (uint32_t)w->by; p[4] = (uint32_t)size; p[5] = w->fmt; }   // b1a: src, pitch, bw, bh, dst, rgba
    if (g_blit) { enc_b_end(g_blit); g_blit = nil; }
    BENC e = blit();
    enc_b_copy_texture_buffer(e, w->aux, w->slice, w->level, MTLOriginMake(0, 0, w->plane), MTLSizeMake(w->bx, w->by, 1), b, 0, row, size);
    enc_b_end(g_blit); g_blit = nil;
    CENC ce = (CENC)enc_c_begin(NULL);
#if DM_NVN_M3
    [ce setBuffer:b offset:0 atIndex:0]; [ce setBuffer:pb offset:0 atIndex:1]; if (v) [ce setTexture:v atIndex:0];
#else
    [ce setArgumentTable:at]; [at setAddress:b.gpuAddress atIndex:0]; [at setAddress:pb.gpuAddress atIndex:1]; if (v) [at setTexture:v.gpuResourceID atIndex:0];
#endif
    enc_c_pso(ce, w->dec == 1 ? (is3d ? bcd3_pso() : bcd_pso()) : b1a_pso()); enc_c_dispatch(ce, MTLSizeMake((w->bx + 7) / 8, (w->by + 7) / 8, 1), MTLSizeMake(8, 8, 1)); enc_c_end(ce);
    if (w->dec == 2) { BENC e2 = blit(); enc_b_copy_buffer_texture(e2, b, size, w->bx * 16, w->bx * 16 * w->by, MTLSizeMake(w->lw, w->lh, 1), w->dst, w->slice, w->level, MTLOriginMake(0, 0, w->plane));
                       enc_b_end(g_blit); g_blit = nil; }
    args_dirty_all();
    defer_obj_release(b); defer_obj_release(pb); if (v) defer_obj_unref(v);
}
// GPU: texture -> texture (previously mislisted as a no-op, 3 times per frame on the character select page). Same size, not mirrored, same format -> blit layer by layer; the other semantics (scaling with filtering, mirroring, format change) cannot be expressed by blit: report a failure, to be implemented
static void copy_t2t(const p_copy_t2t *c) {
    if (c->src) xv_check(c->src->serial, c->has_sv && c->sv.set_levels ? c->sv.base_level : 0, c->has_sv && c->sv.set_layers ? c->sv.min_layer : 0, "texture copy source");
    rp_flush();   // Land the pending pure-clear render pass first (zero-source candidates only enter the set when the render pass ends); the blit() below would end the render pass anyway
    if (c->dst) zskip_del(c->dst->serial);
    if (c->src && c->dst && c->sr.xoffset == 0 && c->sr.yoffset == 0 && c->dr.xoffset == 0 && c->dr.yoffset == 0
        && c->sr.width == (int)c->src->d.width && c->sr.height == (int)c->src->d.height && c->dr.width == (int)c->dst->d.width && c->dr.height == (int)c->dst->d.height) {
        int sl = c->has_sv && c->sv.set_levels ? c->sv.base_level : 0, dl = c->has_dv && c->dv.set_levels ? c->dv.base_level : 0;
        int ss = c->has_sv && c->sv.set_layers ? c->sv.min_layer : 0, ds = c->has_dv && c->dv.set_layers ? c->dv.min_layer : 0;
        if (zskip_has(c->src->serial, sl, ss)) zskip_add(c->dst->serial, dl, ds);
    }
    tex_gpu_write(c->dst);
    id<MTLTexture> s = tex_mtl(c->src), d = tex_mtl(c->dst);
    PD("T src=%llu dst=%llu %dx%dx%d→%dx%dx%d flags=%#x", c->src->serial, c->dst->serial, c->sr.width, c->sr.height, c->sr.depth, c->dr.width, c->dr.height, c->dr.depth, c->flags);
    if (!s || !d) return;
    unsigned sl = c->has_sv && c->sv.set_levels ? c->sv.base_level : 0, dl = c->has_dv && c->dv.set_levels ? c->dv.base_level : 0;
    unsigned sz0 = c->has_sv && c->sv.set_layers ? c->sv.min_layer : 0, dz0 = c->has_dv && c->dv.set_layers ? c->dv.min_layer : 0;
    PD("t src=%llu lv%u sl%u xyz=%d,%d,%d dst=%llu lv%u sl%u xyz=%d,%d,%d size=%d,%d,%d", c->src->serial, sl, sz0, c->sr.xoffset, c->sr.yoffset, c->sr.zoffset,
       c->dst->serial, dl, dz0, c->dr.xoffset, c->dr.yoffset, c->dr.zoffset, c->sr.width, c->sr.height, c->sr.depth);
    int s3 = c->src->d.target == 2, d3 = c->dst->d.target == 2, nz = c->sr.depth ? c->sr.depth : 1;
    if (no_bc() && (s.pixelFormat == MTLPixelFormatASTC_4x4_LDR || s.pixelFormat == MTLPixelFormatASTC_4x4_sRGB) && (d.pixelFormat == MTLPixelFormatRGBA8Unorm || d.pixelFormat == MTLPixelFormatRGBA8Unorm_sRGB)
        && !s3 && !d3 && c->sr.width == c->dr.width && c->sr.height == c->dr.height && (d.usage & MTLTextureUsageShaderWrite)) {   // No BC: ASTC source -> decompressed-format destination (see copy_t2t_astc)
        int ok = 1;
        for (int z = 0; z < nz && ok; z++) ok = copy_t2t_astc(s, d, sl, sz0 + c->sr.zoffset + z, dl, dz0 + c->dr.zoffset + z, c->sr.xoffset, c->sr.yoffset, c->dr.xoffset, c->dr.yoffset, c->sr.width, c->sr.height);
        if (ok) return;
    }
    if (c->sr.width != c->dr.width || c->sr.height != c->dr.height || (c->sr.depth ? c->sr.depth : 1) != (c->dr.depth ? c->dr.depth : 1) || (c->flags & ~NVN_COPY_FLAGS_ENGINE_2D_BIT & ~NVN_COPY_FLAGS_LINEAR_FILTER_BIT)
        || s.pixelFormat != d.pixelFormat || s3 != d3) {
        RED_ONCE(33, "texture→texture copy shape not implemented: %dx%dx%d→%dx%dx%d flags %#x format %lu→%lu 3D %d→%d", c->sr.width, c->sr.height, c->sr.depth, c->dr.width, c->dr.height, c->dr.depth,
                 c->flags, (unsigned long)s.pixelFormat, (unsigned long)d.pixelFormat, s3, d3);
        return;
    }
    BENC b = blit();
    if (s3)
        enc_b_copy_texture(b, s, 0, sl, MTLOriginMake(c->sr.xoffset, c->sr.yoffset, c->sr.zoffset), MTLSizeMake(c->sr.width, c->sr.height, nz), d, 0, dl, MTLOriginMake(c->dr.xoffset, c->dr.yoffset, c->dr.zoffset));
    else
        for (int z = 0; z < nz; z++)
            enc_b_copy_texture(b, s, sz0 + c->sr.zoffset + z, sl, MTLOriginMake(c->sr.xoffset, c->sr.yoffset, 0), MTLSizeMake(c->sr.width, c->sr.height, 1), d, dz0 + c->dr.zoffset + z, dl, MTLOriginMake(c->dr.xoffset, c->dr.yoffset, 0));
}

// GPU: clear a texture region with the given value (previously mislisted as a no-op, called 5 times at initialization; the initial value of a Metal private texture is undefined, and the game relies on the cleared initial value).
// Whole level and whole face, all four channels written -> open one clear-only render pass per layer / depth slice (the hardware converts the clear value by the texture format, including integer and depth);
// view with changed format: clear on a view of that format; partial region, partial channels, compressed / packaged textures (no render target usage): report a failure, to be implemented
static void clear_tex(const p_clear_tex *c) {
    if (counts_on()) g_cnt_cleartex++;
    if (c->tex) zskip_del(c->tex->serial);
    tex_gpu_write(c->tex);
    id<MTLTexture> t = tex_mtl(c->tex);
    PD("X ser=%llu view format %#x region %d,%d,%d %dx%dx%d mask %#x %s %g,%g,%g,%g", c->tex->serial, c->has_view && c->view.set_format ? c->view.format : -1, c->r.xoffset, c->r.yoffset, c->r.zoffset, c->r.width, c->r.height, c->r.depth, c->mask,
       c->is_uint ? "ui" : "f", c->is_uint ? (double)c->c.u[0] : c->c.f[0], c->is_uint ? (double)c->c.u[1] : c->c.f[1], c->is_uint ? (double)c->c.u[2] : c->c.f[2], c->is_uint ? (double)c->c.u[3] : c->c.f[3]);
    if (!t) return;
    unsigned lv = c->has_view && c->view.set_levels ? c->view.base_level : 0, l0 = c->has_view && c->view.set_layers ? c->view.min_layer : 0;
    NSUInteger w = MAX(t.width >> lv, 1u), h = MAX(t.height >> lv, 1u);
    int is3d = t.textureType == MTLTextureType3D, nz = c->r.depth ? c->r.depth : 1;
    if (c->has_view && c->view.set_format) {   // View with changed format: the clear value is interpreted in the view format (all 5 of the game's calls are the ui version with an integer view), and the clear is done on a view of the same format
        uint32_t vpf = 0; uint8_t swz[4];
        if (!nvn_fmt_to_mtl(c->view.format, &vpf, swz) || !vpf) { RED_ONCE(42, "clear-texture view format %#x has no equivalent", c->view.format); return; }
        if (vpf != (uint32_t)t.pixelFormat && tex_stored_unpacked(c->tex)) { RED_ONCE(112, "clear-texture view format %#x of a BC texture kept as Metal %lu on a GPU without BC: not cleared (first case)", c->view.format, (unsigned long)t.pixelFormat); return; }
        if (tex_need_pfv(t.pixelFormat, (MTLPixelFormat)vpf) && !(t.usage & MTLTextureUsagePixelFormatView)) t = tex_upgrade_pfv(c->tex, t, (MTLPixelFormat)vpf);
        if (tex_need_pfv(t.pixelFormat, (MTLPixelFormat)vpf)) {
            id<MTLTexture> v = [t newTextureViewWithPixelFormat:(MTLPixelFormat)vpf];
            if (!v) { RED_ONCE(43, "clear-texture cannot create view: format %lu → %u", (unsigned long)t.pixelFormat, vpf); return; }
            t = v;
        }
    }
    MTLPixelFormat pf = t.pixelFormat;
    int depth = pf == MTLPixelFormatDepth32Float || pf == MTLPixelFormatDepth16Unorm || pf == MTLPixelFormatDepth32Float_Stencil8, stencil = pf == MTLPixelFormatStencil8 || pf == MTLPixelFormatDepth32Float_Stencil8;
    if (c->r.xoffset || c->r.yoffset || (NSUInteger)c->r.width != w || (NSUInteger)c->r.height != h || (c->mask & 0xf) != 0xf
        || !(t.usage & MTLTextureUsageRenderTarget)) {
        RED_ONCE(44, "clear-texture shape not implemented: region %d,%d %dx%d (this level %lux%lu) mask %#x renderable %d", c->r.xoffset, c->r.yoffset, c->r.width, c->r.height,
                 (unsigned long)w, (unsigned long)h, c->mask, !!(t.usage & MTLTextureUsageRenderTarget));
        return;
    }
    rp_end(); if (g_blit) { enc_b_end(g_blit); g_blit = nil; }
    for (int z = 0; z < nz; z++) {
        RPD *rp = [RPD new];
        MTLRenderPassAttachmentDescriptor *a = depth ? rp.depthAttachment : stencil ? rp.stencilAttachment : rp.colorAttachments[0];
        a.texture = t; a.level = lv; a.loadAction = MTLLoadActionClear; a.storeAction = MTLStoreActionStore;
        if (is3d) a.depthPlane = c->r.zoffset + z; else a.slice = l0 + c->r.zoffset + z;
        if (depth) { rp.depthAttachment.clearDepth = t.pixelFormat == MTLPixelFormatDepth16Unorm ? nvn_depth16_clear_value(c->c.f[0]) : c->c.f[0];
            if (stencil) { rp.stencilAttachment.texture = t; rp.stencilAttachment.level = lv; rp.stencilAttachment.slice = a.slice; rp.stencilAttachment.loadAction = MTLLoadActionClear;
                           rp.stencilAttachment.storeAction = MTLStoreActionStore; rp.stencilAttachment.clearStencil = c->c.u[1]; } }
        else if (stencil) rp.stencilAttachment.clearStencil = c->c.u[0];
        else rp.colorAttachments[0].clearColor = c->is_uint ? MTLClearColorMake(c->c.u[0], c->c.u[1], c->c.u[2], c->c.u[3]) : MTLClearColorMake(c->c.f[0], c->c.f[1], c->c.f[2], c->c.f[3]);
        enc_r_end(enc_r_begin(rp));
    }
}

// Context description of one submit (mtl_sub_commit takes one per submit and prints it into the failure line when the command buffer errors): for the GPU memory fault on the first frame entering Travincal, the failure line had only the sequence number and error code, and it was impossible to tell what was in that submit
static const char *cctx_snap(void) {
    static char b[320]; static uint64_t p_pass, p_up, p_reup, p_ser; uint64_t up = 0, reup = 0; mtl_pkg_stats(&up, &reup);
    pthread_mutex_lock(&g_deferred_mu); unsigned long nd = (unsigned long)g_deferred.count; pthread_mutex_unlock(&g_deferred_mu);
    snprintf(b, sizeof b, "frame %llu; since the last submit: passes +%llu, first uploads +%llu, re-uploads +%llu, serial table changes +%llu; deferred-release queue %lu",
             (unsigned long long)atomic_load(&g_cur_frame), (unsigned long long)(g_stat_pass - p_pass), (unsigned long long)(up - p_up), (unsigned long long)(reup - p_reup),
             (unsigned long long)(g_ser_gen - p_ser), nd);
    p_pass = g_stat_pass; p_up = up; p_reup = reup; p_ser = g_ser_gen; return b;
}
