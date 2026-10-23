// gfx/nvn/mtl/kpass.inc, part of nvn_mtl.m (included there in order, one translation unit): Single-draw passes as compute kernels (plan item 217, opt-in)
// ---------- A single-draw pass as two dispatches (plan item 217, DM_NVN_KPASS=1; after nxport's tiny_kernels.rs, MIT, written anew) ----------
// A render pass costs a fixed 16-23 µs on the M5 and more on an iPhone, a dispatch about a tenth of that; Tears of the Kingdom draws about 70 passes
// of one draw into targets of up to 256x256 a frame (the iPhone's island: dropping every such pass took 1.2-1.6 ms of GPU and 4.5 ms of frame p95).
// Such a pass, when both stages have kernels (<key>k.metallib beside the library, gfx/tools/dmsh_kernel.py), runs instead as the vertex kernel for
// the draw's 3-6 vertices and the fragment kernel over the target's pixels, in the shared compute encoder (blit(): a barrier before each) with an
// argument table of its own. Taken only when nothing else of the pass matters: one draw before the pass ends (kp_single), colour targets of up to
// 256x256 made with shader-write usage (tex_desc: their levels, cube faces), formats the kernels write (and read where blending or a partial write mask needs the target:
// MTLReadWriteTextureTier 2), no pending clear, no depth or stencil use, triangles or a strip of 3-6 vertices, one instance, one sample, no
// alpha-to-coverage, alpha test or dual-source blending, a fragment stage that does not sample its own targets, none of the merged, deferred,
// layered or range passes. While the game counts samples passed the kernel adds the pixels it shades to a visibility slot of the pass's own. Metal 4 only. gfx/tests/t_kpass.m checks the kernels against Metal's rasterization
#if !DM_NVN_M3
static int g_kp_on = -1;
static int kp_on(void) { if (g_kp_on < 0) { const char *e = getenv("DM_NVN_KPASS"); g_kp_on = e ? atoi(e) : 0;   // 2: the targets' usage only (measuring its cost)
                             if (g_kp_on) MLOG("DM_NVN_KPASS: single-draw passes up to %dx%d run as compute kernels where both shaders have them", KP_MAX_SIZE, KP_MAX_SIZE); }
                         return g_kp_on; }
static id<MTL4ArgumentTable> g_kpt;   // the kernels' argument table: buffers 0 to KP_TKP, textures 0 to KP_TKO + 7
static uint64_t g_kp_n, g_kp_px, g_kp_no[8];   // passes taken, pixels shaded; declined: 0 no kernels, 1 more than one draw, 2 targets, 3 state, 4 vertex input, 5 draw, 6 blending, 7 the pass's kind (merged, deferred, layered, range)
void mtl_kp_stats(uint64_t out[10]) { out[0] = g_kp_n; out[1] = g_kp_px; for (int i = 0; i < 8; i++) out[2 + i] = g_kp_no[i]; }
// The stage's kernels (tk_vert, or tk_frag_w and tk_frag_rw), loaded and made into pipelines at its first single-draw pass; 0 without them
static int kp_shader(NvShader *sh, int frag) {
    if (sh->k_tried) return sh->k_tried > 0;
    sh->k_tried = -1;
    NSString *path = [NSString stringWithFormat:@"%@/%sk.metallib", g_msl_dir, sh->key];
    if (![[NSFileManager defaultManager] fileExistsAtPath:path]) return 0;
    NSError *e = nil; id<MTLLibrary> lib = lib_from_file(path, &e);
    id<MTLFunction> f0 = [lib newFunctionWithName:frag ? @"tk_frag_w" : @"tk_vert"], f1 = frag ? [lib newFunctionWithName:@"tk_frag_rw"] : nil;
    sh->kps[0] = f0 ? [g_dev newComputePipelineStateWithFunction:f0 error:&e] : nil;
    if (frag) sh->kps[1] = f1 ? [g_dev newComputePipelineStateWithFunction:f1 error:&e] : nil;
    if (!sh->kps[0] || (frag && !sh->kps[1])) { MLOG("[red] kernels of %s (%s): %s", sh->key, path.UTF8String, e ? e.localizedDescription.UTF8String : "a function is missing"); return 0; }
    sh->k_tried = 1; return 1;
}
// DM_NVN_KPASS_CHECK=<frame> (diagnostics): in that frame each pass the kernels would take draws as a render pass as usual, and the kernels write
// into copies of its targets made just before; at the next frame's first single-draw pass both are read back and compared, one line a pass
// (the largest channel difference and the pixels off by more than 1/64). Shows which pass a picture difference comes from
typedef struct { __strong id<MTLTexture> copy, real, snap; char vs[24], fs[24]; uint32_t w, h; int pend; } kp_chk;   // snap: real right after its pass (kp_check_snap)
static kp_chk g_kpc[96]; static int g_kpc_n, g_kpc_pend; static long g_kpc_frame = -2;
static long kp_check_frame(void) { if (g_kpc_frame == -2) { const char *e = getenv("DM_NVN_KPASS_CHECK"); g_kpc_frame = e ? atol(e) : -1; } return g_kpc_frame; }
static void kp_check_report(void) {
    if (!g_kpc_n) return;
    mtl_finish_gpu();
    for (int i = 0; i < g_kpc_n; i++) {
        kp_chk *c = &g_kpc[i]; size_t row = (size_t)c->w * 16;
        MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float width:c->w height:c->h mipmapped:NO];
        d.usage = MTLTextureUsageShaderWrite | MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget; d.storageMode = MTLStorageModeShared;
        // each texture drawn into an RGBA32Float one by a blit-free path: a CPU read of its bytes would need its format's layout, so both are
        // converted on the GPU by sampling (read) into float
        static id<MTLComputePipelineState> cv; if (!cv) { NSError *e = nil; id<MTLLibrary> l = [g_dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\n"
            "kernel void cv(texture2d<float, access::read> a [[texture(0)]], texture2d<float, access::write> o [[texture(1)]], uint2 g [[thread_position_in_grid]]) { if (g.x < o.get_width() && g.y < o.get_height()) o.write(a.read(g), g); }\n" options:nil error:&e];
            cv = [g_dev newComputePipelineStateWithFunction:[l newFunctionWithName:@"cv"] error:&e]; }
        id<MTLTexture> fa = [g_dev newTextureWithDescriptor:d], fb = [g_dev newTextureWithDescriptor:d];
        id<MTLCommandBuffer> cb = [g_q commandBuffer]; id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder]; [ce setComputePipelineState:cv];
        [ce setTexture:c->copy atIndex:0]; [ce setTexture:fa atIndex:1]; [ce dispatchThreads:MTLSizeMake(c->w, c->h, 1) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)];
        [ce setTexture:c->snap atIndex:0]; [ce setTexture:fb atIndex:1]; [ce dispatchThreads:MTLSizeMake(c->w, c->h, 1) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)];
        [ce endEncoding]; [cb commit]; [cb waitUntilCompleted];
        float *pa = malloc(row * c->h), *pb = malloc(row * c->h);
        [fa getBytes:pa bytesPerRow:row fromRegion:MTLRegionMake2D(0, 0, c->w, c->h) mipmapLevel:0]; [fb getBytes:pb bytesPerRow:row fromRegion:MTLRegionMake2D(0, 0, c->w, c->h) mipmapLevel:0];
        double worst = 0; long off = 0; int wx = 0, wy = 0;
        for (uint32_t y = 0; y < c->h; y++) for (uint32_t x = 0; x < c->w; x++) { double m = 0; for (int k = 0; k < 4; k++) { double dd = fabs((double)pa[(y * c->w + x) * 4 + k] - pb[(y * c->w + x) * 4 + k]); if (dd > m) m = dd; }
            if (m > worst) { worst = m; wx = (int)x; wy = (int)y; } off += m > 1.0 / 64; }
        MLOG("DM_NVN_KPASS_CHECK %d: %ux%u vs=%s fs=%s: largest difference %.4g at %d,%d (kernel %g %g %g %g, pass %g %g %g %g), %ld pixels off by more than 1/64", i, c->w, c->h, c->vs, c->fs, worst, wx, wy,
             pa[(wy * c->w + wx) * 4], pa[(wy * c->w + wx) * 4 + 1], pa[(wy * c->w + wx) * 4 + 2], pa[(wy * c->w + wx) * 4 + 3], pb[(wy * c->w + wx) * 4], pb[(wy * c->w + wx) * 4 + 1], pb[(wy * c->w + wx) * 4 + 2], pb[(wy * c->w + wx) * 4 + 3], off);
        free(pa); free(pb); c->copy = nil; c->real = nil; c->snap = nil;
    }
    g_kpc_n = 0;
}
// The targets of the check's passes as their render passes left them (rp_end, once the pass this draw opened has ended)
static void kp_check_snap(void) {
    if (!g_kpc_pend) return;
    g_kpc_pend = 0;
    for (int i = 0; i < g_kpc_n; i++) if (g_kpc[i].pend) { g_kpc[i].pend = 0;
        enc_b_copy_texture(blit(), g_kpc[i].real, 0, 0, MTLOriginMake(0, 0, 0), MTLSizeMake(g_kpc[i].w, g_kpc[i].h, 1), g_kpc[i].snap, 0, 0, MTLOriginMake(0, 0, 0)); }
}
// Whether the pass this draw opens ends before another draw or clear (as ds_scan walks: the rest of this list, its callers, the submission's
// remaining lists). Reports, resets, events, fences and render-enable switches after the draw leave it as it is: a samples-passed report sums the
// slot the kernel pass took at its draw
enum { KP_RET, KP_END, KP_MORE };
static int kp_scan(const uint8_t *p, int depth) {
    for (int guard = 0; guard < 4096; guard++) {
        op_hdr h; memcpy(&h, p, sizeof h); const void *a = p + 8;
        if (g_hoist_n && hoist_skipped(p)) { p += h.size; continue; }
        switch (h.op) {
            case OP_END: return KP_RET;
            case OP_JUMP: { const uint8_t *t; memcpy(&t, a, 8); p = t; continue; }
            case OP_CALL: { uint64_t t; memcpy(&t, a, 8); if (depth >= 8) return KP_MORE; int r = kp_scan((const uint8_t *)t, depth + 1); if (r != KP_RET) return r; break; }
            case OP_UPDATE_UB: if (ub_rename_on()) break; return KP_END;
            case OP_SET_RT: case OP_COPY_B2B: case OP_COPY_B2T: case OP_COPY_T2B: case OP_COPY_T2T: case OP_CLEAR_TEX: case OP_DISPATCH: return KP_END;
            case OP_DRAW: case OP_DRAW_IDX: case OP_CLEAR_COLOR: case OP_CLEAR_DS: return KP_MORE;
            default: break;
        }
        p += h.size;
    }
    return KP_MORE;
}
static int kp_single(void) {
#ifdef DM_TEST_KPASS_NO_SCAN   // negative control of t_kpass_nvn only: every pass taken as single
    return 1;
#endif
    if (!g_exec_next || g_ret_n > 16) return 0;
    int r = kp_scan(g_exec_next, 0);
    for (int k = g_ret_n - 1; r == KP_RET && k >= 0; k--) r = kp_scan(g_ret[k], 0);
    for (int k = 0; r == KP_RET && k < g_nrest; k++) r = kp_scan((const uint8_t *)g_rest[k], 0);
    return r != KP_MORE;   // (KP_RET at the end of the submission: the pass ends with it)
}
#define KP_NO(r) { g_kp_no[r]++; PD("KN %llu why=%d line %d", g_stat_pass + 1, r, __LINE__); return 0; }   // a decline, counted, its reason in the pass dump
// Whether the stages sample a level and layer of a colour target this pass writes (as count_self_sample reads the views), or bind an image: the
// render path's draw_samples_rt compares whole textures, which declined the downsample chains that read level k - 1 while they draw level k
static int kp_self_overlap(NvProg *pr) {
    prog_summary(pr);
    if (pr->sum_img) return 1;
    for (int st = 0; st < 2; st++) { __unsafe_unretained NvShader *sh = pr->sh[st]; if (!sh) continue;
        for (int i = 0; i < sh->rf.nb; i++) { const nvn_rbind *b = &sh->rf.b[i];
            uint64_t h = (b->kind == NVN_RB_TEX || b->kind == NVN_RB_TBUF) && b->reg < 128 ? S.tex[st][b->reg] : 0;
            int tid = h_tid(h); if (!tid) continue;
            treg_t r; int ok = 0; pthread_mutex_lock(&g_rmu); if (tid < g_ntreg && g_treg[tid].ok) { r = g_treg[tid]; ok = 1; } pthread_mutex_unlock(&g_rmu);
            if (!ok) continue;
            int l0, l1, y0, y1; view_span(&r.v, r.has_v, &r.t, 0, &l0, &l1, &y0, &y1);
            for (int k = 0; k < R.rt.n && k < 8; k++) { const tex_t *a = R.rt.color[k]; if (!a) continue;
                if (a->serial != r.t.serial) { if (tex_same_storage(a, &r.t)) return 1; continue; }
                int m0, m1, z0, z1; view_span(&R.rt.cview[k], R.rt.has_cview[k], a, 1, &m0, &m1, &z0, &z1);
                if (l0 < m1 && m0 < l1 && y0 < z1 && z0 < y1) return 1;
            } } }
    return 0;
}
static int kp_draw(NvProg *pr, const p_draw *dw, const p_draw_idx *di) {
    if (kp_on() != 1 || g_enc || g_defer_replaying || g_fz_checking) return 0;
    int chk = kp_check_frame() >= 0 && (long)atomic_load(&g_cur_frame) == kp_check_frame();
    if (g_kpc_n && !chk) kp_check_report();
    {   int why = g_fz.active ? 1 : g_rt_merged ? 2 : g_lay.active ? 3 : g_rt_range_active ? 4 : g_defer_natq || defer_has_in_p() ? 5 : !R.has_rt || R.rt.n > 8 ? 7 : 0;
        if (why) { g_kp_no[7]++; PD("KN %llu why=7 pass state %d", g_stat_pass + 1, why); return 0; } }
    __unsafe_unretained NvShader *vs = pr->sh[0], *fs = pr->sh[1];
    if (!vs || !fs || pr->sh[2] || pr->sh[3] || pr->sh[4] || !fs->rf.color_known) KP_NO(0)
    kp_f f; memset(&f, 0, sizeof f);
    int prim = dw ? dw->prim : di->prim, cnt = dw ? dw->count : di->count, inst = dw ? dw->instances : di->instances;
    int nt = inst <= 1 && cnt > 0 ? kp_tris(prim, (uint32_t)cnt, f.tri) : 0;
    if (!nt || (di && (di->itype < 0 || di->itype > 2 || (S.restart_set && S.restart_en)))) KP_NO(5)
    if ((R.rt.depth && (S.ds.dt || S.ds.dw || S.ds.st || S.db_en)) || R.clear_d || R.clear_s || S.ms.a2c || S.ms.samples > 1 || S.ms.raster_samples > 1 || alpha_on()   // (depth state without a depth target: no effect)
        || S.poly.mode == NVN_POLYGON_MODE_LINE || S.poly.cull == NVN_FACE_FRONT_AND_BACK) KP_NO(3)
    if (!kp_shader(vs, 0) || !kp_shader(fs, 1)) KP_NO(0)
    {   // DM_NVN_KPASS_FS=<fragment key>[,...] (diagnostics): only these fragment shaders' passes, to find the one a difference comes from
        static char only[32][20]; static int nonly = -1;
        if (nonly < 0) { nonly = 0; for (const char *q = getenv("DM_NVN_KPASS_FS"); q && *q && nonly < 32; q = strchr(q, ',') ? strchr(q, ',') + 1 : q + strlen(q)) {
                             size_t n = strcspn(q, ","); if (n > 19) n = 19; memcpy(only[nonly], q, n); only[nonly++][n] = 0; } }
        if (nonly) { int hit = 0; for (int k = 0; k < nonly && !hit; k++) hit = !strncmp(fs->key, only[k], strlen(only[k])); if (!hit) KP_NO(0) } }
    if (!kp_single()) KP_NO(1)
    if (kp_self_overlap(pr)) KP_NO(3)
    // the vertex stage's inputs, as pso_desc lays them out
    kp_v v; memset(&v, 0, sizeof v);
    for (int k = 0; k < vs->rf.nv; k++) {
        int loc = vs->rf.v[k].loc, a = vs->rf.v[k].nvn;
        if (loc < 0 || loc >= 16) KP_NO(4)
        MTLVertexFormat vf = a < 16 && a < S.nattr ? vfmt(S.attr[a].fmt) : MTLVertexFormatInvalid;
        if (vf == MTLVertexFormatInvalid) { if (a < 16 && a < S.nattr && S.attr[a].fmt) KP_NO(4) continue; }   // (unset: zeros, as the zero buffer)
        int st = S.attr[a].stream, iin = vs->rf.v[k].ct != NVN_CT_FLOAT;
        if (st >= 16 || S.stream[st].divisor || !kp_vfmt_ok(vf, iin) || (!iin && vfmt_is_int(vf) && !vfmt_scaled(S.attr[a].fmt))) KP_NO(4)
        void *mb; uint64_t mo, gpu;
        if (!S.vbuf[st].address || !mtl_gpu_addr(S.vbuf[st].address, 1, &mb, &mo, &gpu)) KP_NO(4)
        v.a[loc] = (kp_va){gpu, (uint32_t)S.attr[a].offset, S.stream[st].stride, (uint32_t)vf, S.stream[st].stride == 0};
        use_rec("buf", S.vbuf[st].address, S.vbuf[st].size, 0, NULL);
    }
    if (di) { void *mb; uint64_t mo, gpu; if (!mtl_gpu_addr(di->ibuf, 1, &mb, &mo, &gpu)) KP_NO(4)
              v.ib = gpu; v.itype = (uint32_t)di->itype; v.base_vertex = (uint32_t)di->base_vertex; }
    else v.first = (uint32_t)dw->first;
    v.count = (uint32_t)cnt; v.base_instance = (uint32_t)(dw ? dw->base_instance : di->base_instance);
    // the colour targets the fragment stage writes: their level as a 2D texture, one size
    __strong id<MTLTexture> view[8] = {nil}; __unsafe_unretained id<MTLTexture> base[8] = {nil}; uint32_t tw = 0, th = 0, lv[8] = {0}, sl[8] = {0}; int rw = 0, nw = 0;
    for (int i = 0; i < 8; i++) {
        int writes = fs->rf.color_mask >> i & 1;
        if (!writes) continue;
        if (i >= R.rt.n || !R.rt.color[i]) { kp_blend(f.bl[i], 0, 1, 0, 1, 0, 0, 0, 0, 0); continue; }   // an output with no target: written nowhere
        if (R.clear_c[i] || rt_dup_slot(&R.rt, i)) KP_NO(2)
        MTLRenderPassColorAttachmentDescriptor *ad = [MTLRenderPassColorAttachmentDescriptor new];
        attach(ad, R.rt.color[i], &R.rt.cview[i], R.rt.has_cview[i], 1);
        id<MTLTexture> t = ad.texture;
        int kind = t ? kp_fmt_kind(t.pixelFormat) : -1;
        if (!t || kind < 0 || t.sampleCount > 1 || !(t.usage & MTLTextureUsageShaderWrite) || (t.textureType != MTLTextureType2D && t.textureType != MTLTextureType2DArray
            && t.textureType != MTLTextureTypeCube && t.textureType != MTLTextureTypeCubeArray)) {
            PD("KN %llu target c%d: %s fmt=%lu %lux%lu levels %lu level %lu type %lu samples %lu shader-write %d", g_stat_pass + 1, i, t ? "" : "none", (unsigned long)t.pixelFormat, (unsigned long)t.width,
               (unsigned long)t.height, (unsigned long)t.mipmapLevelCount, (unsigned long)ad.level, (unsigned long)t.textureType, (unsigned long)t.sampleCount, (t.usage & MTLTextureUsageShaderWrite) != 0);
            KP_NO(2) }
        uint32_t w = (uint32_t)MAX(t.width >> ad.level, 1u), h = (uint32_t)MAX(t.height >> ad.level, 1u);
        if (w > KP_MAX_SIZE || h > KP_MAX_SIZE || (nw && (w != tw || h != th))) KP_NO(2)
        tw = w; th = h; nw++; base[i] = t; lv[i] = (uint32_t)ad.level; sl[i] = (uint32_t)ad.slice;
        uint32_t m = (S.chmask.mask >> (4 * i)) & 15; int ben = S.color.blend_en >> i & 1; blend_st b = S.blend[i];
        MTLBlendFactor fsr = bfac(b.src_rgb), fdr = bfac(b.dst_rgb), fsa = bfac(b.src_a), fda = bfac(b.dst_a);
        if (ben && (fsr >= MTLBlendFactorSource1Color || fdr >= MTLBlendFactorSource1Color || fsa >= MTLBlendFactorSource1Color || fda >= MTLBlendFactorSource1Color)) KP_NO(6)
        kp_blend(f.bl[i], ben, (unsigned)fsr, (unsigned)fdr, (unsigned)fsa, (unsigned)fda, (unsigned)bop(b.eq_rgb), (unsigned)bop(b.eq_a), m, kind);
        rw |= ben || (m != 15 && m != 0);
    }
    if (!nw) KP_NO(2)
    for (int i = 0; i < 8; i++) if (rw && base[i] && !kp_fmt_rw(base[i].pixelFormat)) KP_NO(6)
    // the viewport and the scissor, as draw() sets them
    float vp[4] = {S.has_vp ? S.vp[0][0] : 0, S.has_vp ? S.vp[0][1] : 0, S.has_vp ? S.vp[0][2] : (float)tw, S.has_vp ? S.vp[0][3] : (float)th};
    uint32_t sc[4]; int has_sc = 0;
    if (R.has_sc) {
        long x0 = R.sc.x < 0 ? 0 : R.sc.x, y0 = R.sc.y <= 0 ? 0 : R.sc.y, x1 = (long)R.sc.x + R.sc.w, y1 = (long)R.sc.y + R.sc.h;
        if (x1 > (long)tw) x1 = tw; if (y1 > (long)th) y1 = th;
        if (x1 <= x0 || y1 <= y0) return 1;   // nothing drawn, as draw() returns there
        sc[0] = (uint32_t)x0; sc[1] = (uint32_t)y0; sc[2] = (uint32_t)(x1 - x0); sc[3] = (uint32_t)(y1 - y0); has_sc = 1;
    }
    memcpy(f.vp, vp, sizeof vp); f.dr[0] = S.dr[0]; f.dr[1] = S.dr[1];
    if (!kp_rect(&f, tw, th, vp, has_sc ? sc : NULL)) return 1;   // nothing in reach: nothing drawn
    f.ntri = (uint32_t)nt; f.cull = S.poly.cull == NVN_FACE_FRONT ? 1 : S.poly.cull == NVN_FACE_BACK ? 2 : 0; f.front_cw = S.poly.front != NVN_FRONT_FACE_CCW; f.zclip = !S.depth_clamp;
    memcpy(f.bc, S.blend_color, sizeof f.bc);
    if (!g_kpt) { MTL4ArgumentTableDescriptor *ad = [MTL4ArgumentTableDescriptor new]; ad.maxBufferBindCount = KP_TKP + 1; ad.maxTextureBindCount = KP_TKO + 8;
                  NSError *e = nil; g_kpt = [g_dev newArgumentTableWithDescriptor:ad error:&e];
                  if (!g_kpt) { MLOG("[red] DM_NVN_KPASS: no argument table for the kernels (%s): off", e.localizedDescription.UTF8String); g_kp_on = 0; return 0; } }
    for (int i = 0; i < 8; i++) if (base[i]) {
        view[i] = base[i].textureType == MTLTextureType2D && !lv[i] && base[i].mipmapLevelCount == 1 ? base[i]
                : [base[i] newTextureViewWithPixelFormat:base[i].pixelFormat textureType:MTLTextureType2D levels:NSMakeRange(lv[i], 1) slices:NSMakeRange(sl[i], 1)];
        if (!view[i]) { RED_ONCE(116, "single-draw pass as kernels: no 2D view of a target (format %lu, level %u, slice %u): drawn as a pass", (unsigned long)base[i].pixelFormat, lv[i], sl[i]); KP_NO(2) }
    }
    if (chk) {   // DM_NVN_KPASS_CHECK: the kernels write copies of the targets (made from them now); the pass draws as usual after this
        for (int i = 0; i < 8; i++) if (view[i] && g_kpc_n < 96) {
            MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:view[i].pixelFormat width:tw height:th mipmapped:NO];
            d.usage = MTLTextureUsageShaderWrite | MTLTextureUsageShaderRead; d.storageMode = MTLStorageModePrivate;
            id<MTLTexture> cp = [g_dev newTextureWithDescriptor:d]; res_add(cp);
            BENC b = blit(); enc_b_copy_texture(b, view[i], 0, 0, MTLOriginMake(0, 0, 0), MTLSizeMake(tw, th, 1), cp, 0, 0, MTLOriginMake(0, 0, 0));
            kp_chk *k = &g_kpc[g_kpc_n++]; k->copy = cp; k->real = view[i]; strlcpy(k->vs, vs->key, sizeof k->vs); strlcpy(k->fs, fs->key, sizeof k->fs); k->w = tw; k->h = th;
            k->snap = [g_dev newTextureWithDescriptor:d]; res_add(k->snap); k->pend = 1; g_kpc_pend = 1;
            view[i] = cp;
        }
    }
    // taken: the vertex kernel, a barrier, the fragment kernel
    PD("KP %llu %ux%u rect %u,%u-%u,%u tris %u %s vs=%s fs=%s", g_stat_pass + 1, tw, th, f.lo[0], f.lo[1], f.lim[0], f.lim[1], f.ntri, rw ? "rw" : "w", vs->key, fs->key);
    // The stages' arguments are made before blit(): it writes the submission's logged uniform updates to memory (upd_flush), after which a stage
    // would read its uniforms where the render path does not (the 12 passes that draw the sky's cube map read the faces' matrices from such updates)
    args_dirty_all(); g_kp_bind = g_kpt; encode_args(nil, vs, 0, 1); uint64_t va = g_kp_va; encode_args(nil, fs, 1, 0); uint64_t fa = g_kp_va; g_kp_bind = nil;
    CENC ce = blit(); [ce setArgumentTable:g_kpt];
    if (vs->ae) [g_kpt setAddress:va atIndex:vs->rf.abi];
    ARENA_OUT id<MTLBuffer> pb; size_t po = arena_alloc(sizeof v, &pb); memcpy(g_cur_arena->cpu + po, &v, sizeof v); [g_kpt setAddress:g_cur_arena->gpu + po atIndex:KP_TKP];
    ARENA_OUT id<MTLBuffer> rb; size_t ro = arena_alloc(6 * KP_TKS * 16, &rb); uint64_t rec = g_cur_arena->gpu + ro, vis = rec; [g_kpt setAddress:rec atIndex:KP_TKR];
    if (g_vis_counting && !getenv("DM_NVN_KPASS_NOVIS")) {   // the game counts samples passed: a slot of the pass's own, as draw() takes one a pass, zeroed here and added to by the kernel
        if (!g_vis || g_vis_n >= VIS_SLOTS) { if (g_vis && !vis_in_ring(g_vis)) defer_obj_release(g_vis); g_vis = [g_dev newBufferWithLength:VIS_SLOTS * 8 options:MTLResourceStorageModeShared]; res_add(g_vis); g_vis_n = 0; }
        uint32_t slot = g_vis_n++; ((uint64_t *)g_vis.contents)[slot] = 0;
        if (g_nvs == g_vscap) { g_vscap = g_vscap ? g_vscap * 2 : 64; g_vs = realloc(g_vs, g_vscap * sizeof *g_vs); }
        g_vs[g_nvs++] = (vis_slot){(void *)CFBridgingRetain(g_vis), slot};
        vis = g_vis.gpuAddress + 8ull * slot; f.vis = 1;
    }
    enc_c_pso(ce, vs->kps[0]);
    int slot = pg_dispatch_begin(ce); enc_c_dispatch(ce, MTLSizeMake(1, 1, 1), MTLSizeMake(8, 1, 1)); pg_dispatch_end(ce, slot);
    ce = blit(); [ce setArgumentTable:g_kpt];   // (blit(): the barrier between the records' write and their reads)
    if (fs->ae) [g_kpt setAddress:fa atIndex:fs->rf.abi];
    po = arena_alloc(sizeof f, &pb); memcpy(g_cur_arena->cpu + po, &f, sizeof f); [g_kpt setAddress:g_cur_arena->gpu + po atIndex:KP_TKP]; [g_kpt setAddress:rec atIndex:KP_TKR];
    [g_kpt setAddress:vis atIndex:KP_TKV];
    for (int i = 0; i < 8; i++) if (fs->rf.color_mask >> i & 1) [g_kpt setTexture:(view[i] ? view[i] : dummy_img(0, 0)).gpuResourceID atIndex:KP_TKO + i];
    enc_c_pso(ce, fs->kps[rw]);
    slot = pg_dispatch_begin(ce); enc_c_dispatch(ce, MTLSizeMake((f.nq * 4 + 63) / 64, 1, 1), MTLSizeMake(64, 1, 1)); pg_dispatch_end(ce, slot);
    args_dirty_all();
    for (int i = 0; i < 8; i++) if (base[i]) {
        xv_touch_add(R.rt.color[i]->serial, lv[i], sl[i], 0);
        if (view[i] != base[i]) defer_obj_unref(view[i]);
    }
    g_kp_n++; g_kp_px += (uint64_t)f.nq * 4;
    return !chk;   // (checked: the pass draws as usual too)
}
#else
static int kp_on(void) { return 0; }
static void kp_check_snap(void) {}
static int kp_draw(NvProg *pr, const p_draw *dw, const p_draw_idx *di) { (void)pr; (void)dw; (void)di; return 0; }
void mtl_kp_stats(uint64_t out[10]) { memset(out, 0, 10 * sizeof *out); }
#endif
