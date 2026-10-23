// gfx/nvn/mtl/u8_layers.inc, part of nvn_mtl.m (included there in order, one translation unit): 8-bit indices the GPU wrote, and layered passes
// ---------- 8-bit indices the GPU wrote earlier in this submission (MacNativeGames' widen_u8_gpu, as seedds/nxnative 7dde056 adapted it; both MIT;
// written anew here) ----------
// Metal has no 8-bit index type, so draw widens them to 16 bits on the CPU at replay. When a buffer write encoded earlier in this submission (copy,
// inline update) covers them, the CPU reads the bytes from before that write (Tears of the Kingdom, red line 83). Such a draw is widened on the GPU:
// the render pass ends, a compute pass writes the 16-bit indices into the arena (the restart index 0xff mapped as on the CPU path), and the draw
// opens the pass again. Measured per run (the count below); nxnative batches the following draws up to the next buffer write to split fewer passes
static uint64_t g_wide_gpu, g_wide_runs;   // the current draw's widened indices (0: none); draws widened so far
static uint64_t wide_u8_gpu(const p_draw_idx *di, int r8) {
    static id<MTLComputePipelineState> ps; static int failed; M4_ONLY(static id<MTL4ArgumentTable> at;)
    if (failed) return 0;
    if (!ps) { NSError *e = nil; id<MTLLibrary> l = [g_dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\n"
            "kernel void wide_u8(device const uchar *src [[buffer(0)]], device ushort *dst [[buffer(1)]], constant uint4 &p [[buffer(2)]], uint i [[thread_position_in_grid]]) {\n"
            "    if (i < p.x) { uchar v = src[p.y + i]; dst[i] = p.z && v == 0xff ? ushort(0xffff) : ushort(v); } }\n" options:nil error:&e];
        id<MTLFunction> f = l ? [l newFunctionWithName:@"wide_u8"] : nil; ps = f ? [g_dev newComputePipelineStateWithFunction:f error:&e] : nil;
#if !DM_NVN_M3
        if (ps) { MTL4ArgumentTableDescriptor *ad = [MTL4ArgumentTableDescriptor new]; ad.maxBufferBindCount = 3; at = [g_dev newArgumentTableWithDescriptor:ad error:&e]; }
#endif
        if (!ps M4_ONLY(|| !at)) { failed = 1; MLOG("[red] 8-bit index widening on the GPU: no compute pipeline (%s): widened on the CPU", e.localizedDescription.UTF8String); return 0; } }
    void *mb; uint64_t mo, src;
    if (!mtl_gpu_addr(di->ibuf, (uint64_t)di->count, &mb, &mo, &src)) return 0;
    rp_end(); if (g_blit) { enc_b_end(g_blit); g_blit = nil; }
    ARENA_OUT id<MTLBuffer> ob; size_t oo = arena_alloc((size_t)di->count * 2, &ob); ARENA_OUT id<MTLBuffer> pb; size_t po = arena_alloc(16, &pb);
    uint32_t *p = (uint32_t *)((uint8_t *)pb.contents + po); p[0] = (uint32_t)di->count; p[1] = (uint32_t)(mo & 15); p[2] = (uint32_t)r8; p[3] = 0;
    CENC ce = (CENC)enc_c_begin(NULL);
#if DM_NVN_M3
    [ce setBuffer:(__bridge id<MTLBuffer>)mb offset:(NSUInteger)(mo & ~15ull) atIndex:0]; [ce setBuffer:ob offset:oo atIndex:1]; [ce setBuffer:pb offset:po atIndex:2];
#else
    [ce setArgumentTable:at]; [at setAddress:src - (mo & 15) atIndex:0]; [at setAddress:ob.gpuAddress + oo atIndex:1]; [at setAddress:pb.gpuAddress + po atIndex:2];
#endif
    enc_c_pso(ce, ps); enc_c_dispatch(ce, MTLSizeMake(((NSUInteger)di->count + 63) / 64, 1, 1), MTLSizeMake(64, 1, 1)); enc_c_end(ce);
    args_dirty_all();
    uint64_t n = ++g_wide_runs; if (n <= 4 || !(n % 1000)) MLOG("8-bit indices written by the GPU earlier in this submission widened on the GPU (%llu draws so far; each ends the render pass)", (unsigned long long)n);
    return ob.gpuAddress + oo;
}
// Before a draw opens its render pass: an updated range it reads other than as a uniform buffer (vertex, index, storage buffers, buffer textures), or a
// uniform range without a snapshot, needs the updates in memory first. Not while deferred or merged passes are being built (their draws are replayed
// later in one pass): the copy then still comes out right if a snapshot exists, else reports (upd_shadow)
static void upd_guard_draw(NvProg *pr, const p_draw_idx *di) {   // only what this draw's shaders read (stale bindings of earlier draws do not count)
    int why = -1;
    if (di) { int isz = di->itype == 0 ? 1 : di->itype == 1 ? 2 : 4; if (upd_overlap_seq(di->ibuf, di->ibuf + (uint64_t)di->count * isz)) why = 0; }
    __unsafe_unretained NvShader *vs = pr ? pr->sh[0] : nil;
    for (int v = 0; vs && why < 0 && v < vs->rf.nv; v++) { int a = vs->rf.v[v].nvn; if (a < 0 || a >= 16 || a >= S.nattr || S.attr[a].stream >= 16) continue;
        const NVNbufferRange *r = &S.vbuf[S.attr[a].stream]; if (r->address && upd_overlap_seq(r->address, r->address + (r->size ? r->size : 1))) why = 1; }
    for (int st = 0; st < 5 && why < 0 && pr; st++) { __unsafe_unretained NvShader *sh = pr->sh[st]; if (!sh || sh->stage < 0 || sh->stage >= 5) continue;
        for (int i = 0; i < sh->rf.nb && why < 0; i++) { const nvn_rbind *b = &sh->rf.b[i];
            if (b->kind == NVN_RB_SBUFP && b->reg < 16) { const p_bind_buf *u = &S.ssbo[sh->stage][b->reg]; if (u->addr && upd_overlap_seq(u->addr, u->addr + (u->size ? u->size : 1))) why = 2; }
            else if (b->kind == NVN_RB_TBUF && b->reg < 128) { int tid = h_tid(S.tex[sh->stage][b->reg]); pthread_mutex_lock(&g_rmu);
                if (tid > 0 && tid < g_ntreg && g_treg[tid].ok && g_treg[tid].t.mem) { int bw, bh, bpb = nvn_fmt_bpb(g_treg[tid].t.d.format, &bw, &bh); uint64_t m = (uint64_t)(uintptr_t)g_treg[tid].t.mem;
                    if (upd_overlap_seq(m, m + (uint64_t)g_treg[tid].t.d.width * (bpb ? bpb : 16))) why = 3; }
                pthread_mutex_unlock(&g_rmu); }
            else if (b->kind == NVN_RB_CBUF && b->reg < 16) { const p_bind_buf *u = &S.ubo[sh->stage][b->reg]; if (u->addr && u->size && upd_overlap_seq(u->addr, u->addr + u->size) && !us_find(u->addr, u->size)) why = 4; } } }
    if (why < 0) return;
    if (g_defer_nact || g_fz.active || g_fz_checking) { RED_ONCE(91, "renamed uniform update: a draw reads an updated range while a deferred or merged pass is built (first case): not written first"); return; }
    g_ren_guard++; g_ren_g[why]++; upd_flush();
}
static int lay_reads_target(NvProg *pr);
// Layered passes (see g_lay). Off with the TBDR rewrites (they own pass boundaries), on GPUs without two-view amplification, by DM_NVN_LAYER_MERGE=0
static int lay_on(void) {
    static int v = -1;
    if (v < 0) { const char *e = getenv("DM_NVN_LAYER_MERGE"); v = !(e && !strcmp(e, "0")) && [g_dev supportsVertexAmplificationCount:2]; if (e && !v) MLOG("DM_NVN_LAYER_MERGE=0: layered passes off"); }
    return v && !tbdr_on();
}
static int lay_prim_ok(int prim) { return prim == NVN_DRAW_PRIMITIVE_TRIANGLES || prim == NVN_DRAW_PRIMITIVE_TRIANGLE_STRIP; }   // the triangle topology class a layered pipeline needs
static int lay_same(const p_set_rt *r, uint64_t ser, const view_t *v0) {   // one colour target, the same texture, level and view but for its layer
    if (r->n != 1 || r->depth || !r->color[0] || r->color[0]->serial != ser || !r->has_cview[0]) return 0;
    view_t v = r->cview[0]; if (!v.set_layers || v.nlayers != 1) return 0;
    v.min_layer = v0->min_layer; return !memcmp(&v, v0, sizeof v);
}
static int lay_op_safe(unsigned op) {   // ops that leave a pass open: state and bindings (draws and SetRenderTargets are looked at by the caller)
    switch (op) {
        case OP_VIEWPORTS: case OP_SCISSOR: case OP_DEPTH_RANGE: case OP_BIND_PROGRAM: case OP_BIND_STATE: case OP_BIND_VATTR: case OP_BIND_VSTREAM: case OP_BIND_VBUF:
        case OP_BIND_UBO: case OP_BIND_SSBO: case OP_BIND_TEX: case OP_BIND_SAMP: case OP_BIND_IMG: case OP_STENCIL: case OP_POLY_OFFSET: case OP_BLEND_COLOR: case OP_DEPTH_CLAMP: case OP_DEPTH_BOUNDS: case OP_RASTER_DISCARD:
        case OP_SET_TEX_POOL: case OP_SET_SAMP_POOL: case OP_BARRIER: case OP_PRIM_RESTART: case OP_BEGIN: return 1;
        default: return 0;
    }
}
// rp_begin_ex: the number of layers of a layered pass starting at *first, or 0. The lookahead only sizes the range: a SetRenderTargets for a layer
// outside it, a clear, any other op and a draw that cannot go to a layer end the pass as before (lay_continue, the clear cases, draw)
static int lay_decide(int clear_only, int *first) {
    if (g_lay.refuse) { g_lay.refuse = 0; return 0; }
    if (clear_only || !lay_on() || g_vis_counting || R.rt.n != 1 || !R.rt.color[0] || R.rt.depth || !R.rt.has_cview[0] || g_defer_replaying || g_defer_nact || g_fz.active) return 0;
    const tex_t *t = R.rt.color[0]; const view_t *v = &R.rt.cview[0];
    if ((t->d.target != NVN_TEXTURE_TARGET_CUBEMAP && t->d.target != NVN_TEXTURE_TARGET_2D_ARRAY && t->d.target != NVN_TEXTURE_TARGET_CUBEMAP_ARRAY) || t->d.samples > 1 || !v->set_layers || v->nlayers != 1) return 0;
    if (R.clear_c[0] || R.clear_d || R.clear_s) return 0;   // a load action would clear every layer of the range
    unsigned lv = v->set_levels ? v->base_level : 0;
    if (mip_extent(t->d.width, lv) > LAY_MAX_EXTENT || mip_extent(t->d.height, lv) > LAY_MAX_EXTENT) return 0;
    if (!(g_fd_dw ? lay_prim_ok(g_fd_dw->prim) : g_fd_di && lay_prim_ok(g_fd_di->prim))) return 0;
    if (!g_exec_next || g_ret_n > 8) return 0;
    fz_cursor cur = {.p = g_exec_next}; for (int k = 0; k < g_ret_n; k++) cur.ret[k] = g_ret[k]; cur.ret_n = g_ret_n;   // the replay's own return stack first
    int lo = v->min_layer, hi = v->min_layer;
    for (int guard = 0; guard < 4096; guard++) {
        const uint8_t *q; op_hdr oh; const void *oa;
        if (!fz_next(&cur, &q, &oh, &oa) || cur.bad) break;
        if (oh.op == OP_SET_RT) { const p_set_rt *r = oa; if (!lay_same(r, t->serial, v)) break; int l = r->cview[0].min_layer; lo = MIN(lo, l); hi = MAX(hi, l); continue; }
        if (oh.op == OP_DRAW || oh.op == OP_DRAW_IDX) { if (!lay_prim_ok(oh.op == OP_DRAW ? ((const p_draw *)oa)->prim : ((const p_draw_idx *)oa)->prim)) break; continue; }
        if (!lay_op_safe(oh.op)) break;
    }
    int layers = t->d.target == NVN_TEXTURE_TARGET_CUBEMAP ? 6 : (int)t->d.depth;   // a cube array's depth counts faces
    if (hi <= lo || hi >= layers) return 0;
    g_lay.ser = t->serial; g_lay.v = *v; g_lay.first = lo; g_lay.count = hi - lo + 1;
    if (lay_reads_target(S.prog ? prog_get(S.prog) : nil)) return 0;   // the draw opening the pass is checked as the later ones are (draw)
    *first = lo; return hi - lo + 1;
}
static void lay_report(uint64_t f) {   // every 300 frames, when layered passes happened
    static uint64_t p[3]; uint64_t c[3] = {g_lay.passes, g_lay.merges, g_lay.splits};
    if (f % 300 || c[0] == p[0]) return;
    MLOG("layered passes up to frame %llu: %llu passes holding %llu more SetRenderTargets; %llu ended early by a draw they could not hold", (unsigned long long)f,
         (unsigned long long)(c[0] - p[0]), (unsigned long long)(c[1] - p[1]), (unsigned long long)(c[2] - p[2]));
    memcpy(p, c, sizeof p);
}
static int lay_continue(const p_set_rt *r) {   // SetRenderTargets in a layered pass: another layer of its range goes on in it
    if (!lay_same(r, g_lay.ser, &g_lay.v)) return 0;
    int l = r->cview[0].min_layer; if (l < g_lay.first || l >= g_lay.first + g_lay.count) return 0;
    R.rt = *r; g_lay.cur = l; g_lay.merges++;
    return 1;
}
// A draw in a layered pass that samples the level being rendered (any of its layers): the separate passes it replaces had only one layer attached.
// The cube filters sample the level above through a view of that level, which is fine; a view whose levels cannot be told counts as a read
static int lay_reads_target(NvProg *pr) {
    if (!pr) return 1;
    unsigned lv = g_lay.v.set_levels ? g_lay.v.base_level : 0;
    for (int st = 0; st < 5; st++) { __unsafe_unretained NvShader *sh = pr->sh[st]; if (!sh || sh->stage < 0 || sh->stage >= 5) continue;
        for (int i = 0; i < sh->rf.nb; i++) { const nvn_rbind *b = &sh->rf.b[i]; uint64_t h;
            if (b->kind == NVN_RB_IMG) h = b->reg < 32 ? S.img[sh->stage][b->reg] : 0; else if (b->kind == NVN_RB_TEX || b->kind == NVN_RB_TBUF) h = b->reg < 128 ? S.tex[sh->stage][b->reg] : 0; else continue;
            int tid = h_tid(h); if (!tid) continue;
            int hit = 0; pthread_mutex_lock(&g_rmu);
            if (tid < g_ntreg && g_treg[tid].ok && g_treg[tid].t.serial == g_lay.ser) {
                const view_t *v = &g_treg[tid].v; int hv = g_treg[tid].has_v;
                int lv_out = hv && v->set_levels && v->nlevels && (lv < v->base_level || lv >= (unsigned)v->base_level + v->nlevels);
                int ly_out = hv && v->set_layers && v->nlayers && (g_lay.first + g_lay.count <= v->min_layer || g_lay.first >= v->min_layer + v->nlayers);
                hit = !lv_out && !ly_out;
            }
            pthread_mutex_unlock(&g_rmu);
            if (hit) return 1; } }
    return 0;
}
