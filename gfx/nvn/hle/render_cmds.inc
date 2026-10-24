// gfx/nvn/hle/render_cmds.inc, part of hle_nvn.c (included there in order, one translation unit): Render commands: parameters recorded into command memory in nvn_obj.h's payload format
// ---------- Render commands: parameters are recorded into command memory in the payload format of nvn_obj.h; for replay see nvn_mtl.m ----------
static void *rec(NVNcommandBuffer *c, uint32_t op, size_t payload) {
    uint32_t sz = (uint32_t)((8 + payload + 7) & ~(size_t)7);
    uint8_t *p = cmd_alloc(CB(c), sz); put_hdr(p, op, sz);
    memset(p + 8, 0, sz - 8);
    return p + 8;
}
#define REC(c, OP, T) ((T *)rec((c), (OP), sizeof(T)))
// Binding records with arrays are allocated by the actual count: prefix fields + n elements (the replay side reads only the first n). Previously they were recorded at fixed length for the maximum count (bind vertex buffers 272 bytes, bind textures / samplers 280 bytes,
// while there are usually only 1–2 in practice), and the empty tail took 40–50% of the command bytes per frame; the advanced stats page of the level 88 sorceress recorded 12.5 MB per frame, using up the game's own command memory pool (about 105 segments x 128 KB per frame)
#define RECN(c, OP, T, ARR, N) ((T *)rec((c), (OP), offsetof(T, ARR) + sizeof(((T *)0)->ARR[0]) * (size_t)(N)))
static void copy_view(view_t *dst, uint8_t *has, const NVNtextureView *v) { *has = v != NULL; if (v) *dst = *(const view_t *)v; }
NVN(nvnCommandBufferSetRenderTargets, void, NVNcommandBuffer *c, int n, const NVNtexture *const *ct, const NVNtextureView *const *cv,
    const NVNtexture *dt, const NVNtextureView *dv) {
    if (n < 0 || n > 8) { LOG("[red] SetRenderTargets: count %d", n); n = n < 0 ? 0 : 8; }
    { static int rl = -1; if (rl < 0) { const char *e = getenv("DM_RTLOG"); rl = e ? atoi(e) : 0; }   // Diagnostics: arguments of the first N SetRenderTargets calls
      if (rl > 0) { rl--; char b[512]; int k = snprintf(b, sizeof b, "SetRT n=%d", n);
        for (int i = 0; i < n && k < 400; i++) { const tex_t *t = ct ? (const tex_t *)ct[i] : NULL;
            k += snprintf(b + k, sizeof b - k, " c%d=%p", i, (const void *)t); if (t) k += snprintf(b + k, sizeof b - k, "(ser %llu %ux%u fmt %#x mg %#x mem %p pool %p off %#llx)", t->serial, t->d.width, t->d.height, t->d.format, t->magic, (void *)t->mem, (void *)t->pool, (long long)t->off); }
        const tex_t *d = (const tex_t *)dt; k += snprintf(b + k, sizeof b - k, " d=%p", (const void *)d); if (d) snprintf(b + k, sizeof b - k, "(ser %llu %ux%u fmt %#x)", d->serial, d->d.width, d->d.height, d->d.format);
        LOG("[nvn] %s", b);
        if (n > 0 && ct && ct[0] && !((const tex_t *)ct[0])->serial) { static int dumped; if (!dumped++) { const uint8_t *q = (const uint8_t *)ct[0]; char hx[0xC0 * 2 + 1];
            for (int i = 0; i < 0xC0; i++) snprintf(hx + 2 * i, 3, "%02x", q[i]); LOG("[nvn] SetRT color object at index 0, 0xC0 bytes: %s", hx); } } } }

    for (int i = 0; i < n && ct; i++) if (ct[i]) {
        const tex_t *t = (const tex_t *)ct[i]; uint8_t swz[4]; uint32_t pf = 0; tex_census(t, 1);
        if (nvn_fmt_to_mtl(t->d.format, &pf, swz) && swz[0] != NVN_SWZ_R) { static int red; if (!red++) LOG("[red] SetRenderTargets: format %#x as a render target, which is sampled with R and B swapped (nvn_mtl.m tex_by_id): what the GPU renders into it reads crossed (first case)", t->d.format); }
    }
    p_set_rt *r = REC(c, OP_SET_RT, p_set_rt); r->n = n;
    for (int i = 0; i < n; i++) { r->color[i] = ct ? tex_snap(ct[i]) : NULL; copy_view(&r->cview[i], &r->has_cview[i], cv ? cv[i] : NULL); }
    r->depth = tex_snap(dt); copy_view(&r->dview, &r->has_dview, dv);
}
NVN(nvnCommandBufferClearColor, void, NVNcommandBuffer *c, int index, const float *rgba, int mask) {
    p_clear_color *r = REC(c, OP_CLEAR_COLOR, p_clear_color); r->index = index; r->mask = mask; memcpy(r->rgba, rgba, 16);
}
NVN(nvnCommandBufferClearDepthStencil, void, NVNcommandBuffer *c, float depth, uint32_t dmask, int stencil, int smask) {
    p_clear_ds *r = REC(c, OP_CLEAR_DS, p_clear_ds); *r = (p_clear_ds){depth, (int)B(dmask), stencil, smask};
}
NVN(nvnCommandBufferSetViewports, void, NVNcommandBuffer *c, int first, int n, const float *v) {
    if (n < 0 || first + n > 16) { LOG("[red] SetViewports first=%d n=%d", first, n); return; }
    p_viewports *r = REC(c, OP_VIEWPORTS, p_viewports); r->first = first; r->n = n;
    for (int i = 0; i < n; i++) memcpy(r->v[i], v + 4 * i, 16);
}
// Integer single viewport = viewport 0 (same as SetViewports(0, 1, {x, y, w, h}))
NVN(nvnCommandBufferSetViewport, void, NVNcommandBuffer *c, int x, int y, int w, int h) {
    p_viewports *r = REC(c, OP_VIEWPORTS, p_viewports); r->first = 0; r->n = 1;
    r->v[0][0] = (float)x; r->v[0][1] = (float)y; r->v[0][2] = (float)w; r->v[0][3] = (float)h;
}
NVN(nvnCommandBufferSetDepthClamp, void, NVNcommandBuffer *c, uint32_t en) { REC(c, OP_DEPTH_CLAMP, p_depth_clamp)->enable = B(en) != 0; }
NVN(nvnCommandBufferSetPrimitiveRestart, void, NVNcommandBuffer *c, uint32_t en, int index) { p_prim_restart *r = REC(c, OP_PRIM_RESTART, p_prim_restart); r->enable = B(en) != 0; r->index = (uint32_t)index; }   // replay: nvn_mtl.m draw
// Conservative rasterization: no Metal equivalent. Disabling it (Unity's normal case) needs nothing; enabling it is reported red
NVN(nvnCommandBufferSetConservativeRasterEnable, void, NVNcommandBuffer *c, uint32_t en) {
    (void)c; static int red; if (B(en) && !red++) LOG("[red] CommandBufferSetConservativeRasterEnable 1: Metal has no conservative rasterization, drawing with normal rasterization");
}
// Not modelled yet, reported red once when they would change what is drawn: render enable (plain and conditional on a query result) draws always,
// rasterizer discard is ignored. The tiled cache calls are hints for the console's binning rasterizer with no Metal counterpart. (nxnative)
NVN(nvnCommandBufferSetRenderEnable, void, NVNcommandBuffer *c, uint32_t en) { *REC(c, OP_RENDER_ENABLE, p_render_enable) = (p_render_enable){B(en) ? 1 : 0, 0}; }
NVN(nvnCommandBufferSetRenderEnableConditional, void, NVNcommandBuffer *c, NVNconditionalRenderMode m, NVNbufferAddress a) {
    *REC(c, OP_RENDER_ENABLE, p_render_enable) = (p_render_enable){m == NVN_CONDITIONAL_RENDER_MODE_RENDER_IF_EQUAL ? 2 : 3, a};
}
// Rasterizer discard (plan item 64d): a record; the replay leaves the draws out while it is on (nvn_mtl.m draw: exact when the vertex stage writes
// no memory; one that may is reported, its writes lost: Metal would want a vertex function that returns void, GirlNet's P5R branch builds such
// variants). Before, it was ignored with a red line and the fragments drawn
NVN(nvnCommandBufferSetRasterizerDiscard, void, NVNcommandBuffer *c, uint32_t en) { *(int32_t *)rec(c, OP_RASTER_DISCARD, 8) = B(en) != 0; }
NVN(nvnCommandBufferSetTiledCacheAction, void, NVNcommandBuffer *c, NVNtiledCacheAction a) { (void)c; (void)a; }
NVN(nvnCommandBufferSetTiledCacheTileSize, void, NVNcommandBuffer *c, int w, int h) { (void)c; (void)w; (void)h; }
NVN(nvnCommandBufferSetScissor, void, NVNcommandBuffer *c, int x, int y, int w, int h) { *REC(c, OP_SCISSOR, p_scissor) = (p_scissor){x, y, w, h}; }
NVN(nvnCommandBufferSetDepthRange, void, NVNcommandBuffer *c, float n, float f) { *REC(c, OP_DEPTH_RANGE, p_depth_range) = (p_depth_range){n, f}; }
// The batch forms (plan item 64d; GirlNet's P5R branch takes uniform batches from 0 and stops the process on others): the first entry is viewport
// 0's, as the single forms; an entry past 0 that differs, or a batch from past 0, is reported once (viewport arrays beyond 0 are not modelled)
static int viewport_batch(const char *name, int first, int n, const void *v, size_t stride) {
    if (first < 0 || n <= 0 || first + n > 16 || !v) return 0;
    int extra = first > 0; for (int i = 1; i < n && !extra; i++) extra = memcmp(v, (const uint8_t *)v + i * stride, stride) != 0;
    if (extra) { static int once[2]; if (!once[name[3] == 'D']++) LOG("[red] CommandBuffer%s from %d, %d entries: only viewport 0 is modelled (first case)", name, first, n); }
    return first == 0;
}
NVN(nvnCommandBufferSetScissors, void, NVNcommandBuffer *c, int first, int n, const int *r) { if (viewport_batch("SetScissors", first, n, r, 16)) h_nvnCommandBufferSetScissor(c, r[0], r[1], r[2], r[3]); }
NVN(nvnCommandBufferSetDepthRanges, void, NVNcommandBuffer *c, int first, int n, const float *r) { if (viewport_batch("SetDepthRanges", first, n, r, 8)) h_nvnCommandBufferSetDepthRange(c, r[0], r[1]); }
// The program's shaders as of recording: the console copies them into the command stream, and a game may give the same NVNprogram other shaders
// (ProgramSetShaders again) before the recording is submitted or while it is still replayed. Tears of the Kingdom does (2142 SetShaders), and the
// replay layer, which read the keys through the program pointer at submit, ran a bloom upsample with another program's pixel shader: it read the
// vertex shader's matrix row as its bias and painted the title screen red. So the record points to an immutable copy of the key set, one per
// distinct set, with its own serial (the replay layer resolves programs by serial). Programs without keys (unit-test objects) are recorded as they are
static const prog_t *prog_snapshot(const prog_t *p) {
    if (!p || !p->nstage) return p;
    uint8_t k[1 + 6 * 8 + 6] = {0}; k[0] = (uint8_t)p->nstage; memcpy(k + 1, p->key, sizeof p->key); memcpy(k + 1 + 48, p->kstage, sizeof p->kstage);
    static nvn_hmap snaps; static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&mu);
    if (!snaps.klen) nvn_hm_init(&snaps, sizeof k);
    prog_t *c = nvn_hm_get(&snaps, k);
    if (!c) { c = malloc(sizeof *c); *c = *p; c->magic = MAGIC('P', 'R', 'G', 'S'); c->serial = nvn_new_serial(); nvn_hm_put(&snaps, k, c); }
    pthread_mutex_unlock(&mu);
    return c;
}
NVN(nvnCommandBufferBindProgram, void, NVNcommandBuffer *c, const NVNprogram *p, int stages) {
    *REC(c, OP_BIND_PROGRAM, p_bind_program) = (p_bind_program){prog_snapshot((const prog_t *)p), stages};
}
static void bind_state(NVNcommandBuffer *c, int kind, const void *st, size_t n) { p_bind_state *r = REC(c, OP_BIND_STATE, p_bind_state); r->kind = kind; memcpy(r->bytes, st, n); }
NVN(nvnCommandBufferBindBlendState, void, NVNcommandBuffer *c, const NVNblendState *st) { bind_state(c, 0, st, sizeof(blend_st)); }
NVN(nvnCommandBufferBindColorState, void, NVNcommandBuffer *c, const NVNcolorState *st) { bind_state(c, 1, st, sizeof(color_st)); }
NVN(nvnCommandBufferSetAlphaRef, void, NVNcommandBuffer *c, float ref) { bind_state(c, 6, &ref, sizeof ref); }   // the alpha test's reference (nvn_mtl.m alpha_fn)
NVN(nvnCommandBufferBindChannelMaskState, void, NVNcommandBuffer *c, const NVNchannelMaskState *st) { bind_state(c, 2, st, sizeof(chmask_st)); }
NVN(nvnCommandBufferBindMultisampleState, void, NVNcommandBuffer *c, const NVNmultisampleState *st) { bind_state(c, 3, st, sizeof(ms_st)); }
NVN(nvnCommandBufferBindPolygonState, void, NVNcommandBuffer *c, const NVNpolygonState *st) { bind_state(c, 4, st, sizeof(poly_st)); }
NVN(nvnCommandBufferBindDepthStencilState, void, NVNcommandBuffer *c, const NVNdepthStencilState *st) { bind_state(c, 5, st, sizeof(ds_st)); }
NVN(nvnCommandBufferBindVertexAttribState, void, NVNcommandBuffer *c, int n, const NVNvertexAttribState *st) {
    if (n < 0 || n > 16) { LOG("[red] BindVertexAttribState n=%d", n); return; }
    p_bind_vattr *r = RECN(c, OP_BIND_VATTR, p_bind_vattr, st, n); r->first = 0; r->n = n; memcpy(r->st, st, 4 * n);
}
NVN(nvnCommandBufferBindVertexStreamState, void, NVNcommandBuffer *c, int n, const NVNvertexStreamState *st) {
    if (n < 0 || n > 16) { LOG("[red] BindVertexStreamState n=%d", n); return; }
    p_bind_vstream *r = RECN(c, OP_BIND_VSTREAM, p_bind_vstream, st, n); r->first = 0; r->n = n; memcpy(r->st, st, 8 * n);
}
NVN(nvnCommandBufferBindVertexBuffers, void, NVNcommandBuffer *c, int first, int n, const NVNbufferRange *rg) {
    if (n < 0 || first + n > 16) { LOG("[red] BindVertexBuffers first=%d n=%d", first, n); return; }
    p_bind_vbuf *r = RECN(c, OP_BIND_VBUF, p_bind_vbuf, r, n); r->first = first; r->n = n; memcpy(r->r, rg, sizeof *rg * n);
}
NVN(nvnCommandBufferBindUniformBuffer, void, NVNcommandBuffer *c, NVNshaderStage st, int i, NVNbufferAddress a, size_t n) {
    { static int dbg = -1; if (dbg < 0) dbg = getenv("DM_NVN_UBLOG") != NULL; if (dbg) LOG("[ub] BindUniformBuffer stage %d slot %d %#llx size %#zx", (int)st, i, (unsigned long long)a, n); }
    *REC(c, OP_BIND_UBO, p_bind_buf) = (p_bind_buf){st, i, a, n};
}
NVN(nvnCommandBufferBindUniformBuffers, void, NVNcommandBuffer *c, NVNshaderStage st, int first, int n, const NVNbufferRange *rg) {
    for (int i = 0; i < n; i++) *REC(c, OP_BIND_UBO, p_bind_buf) = (p_bind_buf){st, first + i, rg[i].address, rg[i].size};
}
NVN(nvnCommandBufferBindVertexBuffer, void, NVNcommandBuffer *c, int i, NVNbufferAddress a, size_t n) { NVNbufferRange r = {a, n}; h_nvnCommandBufferBindVertexBuffers(c, i, 1, &r); }   // single form (nxnative)
NVN(nvnCommandBufferBindStorageBuffer, void, NVNcommandBuffer *c, NVNshaderStage st, int i, NVNbufferAddress a, size_t n) {
    *REC(c, OP_BIND_SSBO, p_bind_buf) = (p_bind_buf){st, i, a, n};
}
NVN(nvnCommandBufferBindStorageBuffers, void, NVNcommandBuffer *c, NVNshaderStage st, int first, int n, const NVNbufferRange *rg) {
    for (int i = 0; i < n; i++) *REC(c, OP_BIND_SSBO, p_bind_buf) = (p_bind_buf){st, first + i, rg[i].address, rg[i].size};
}
// More than 32 handles (separate textures: 128 a stage, NVN_DEVICE_INFO_SEPARATE_TEXTURE_BINDINGS_PER_STAGE) go in records of 32 (plan item 61b, after
// bayonetta3-mobile nvn55_snapshot.h, MIT; they were dropped with a red line). Refused: slots past the replay's tables (textures and samplers 128, images
// 32). defer: each record keeps where its own handles are (BindTexturesDeferred)
static void bind_handles_at(NVNcommandBuffer *c, int op, int st, int first, int n, const uint64_t *h, int defer) {
    int lim = op == OP_BIND_IMG ? 32 : 128;
    if (n < 0 || first < 0 || first + n > lim) { LOG("[red] bind of %d handles from slot %d (op %d): past the %d slots", n, first, op, lim); return; }
    for (int i = 0; i == 0 || i < n; i += 32) {
        int k = n - i < 32 ? n - i : 32;
#ifdef DM_TEST_BIND_NO_SPLIT   // negative control of t_bindfreeze only: more than 32 dropped, as before
        if (n > 32) { LOG("[red] bind handle count %d", n); return; }
#endif
        p_bind_handles *r = RECN(c, op, p_bind_handles, h, k); r->stage = st; r->first = first + i; r->n = k;
        r->defer = defer && h; r->src = defer && h ? (uint64_t)(uintptr_t)(h + i) : 0; if (h) memcpy(r->h, h + i, 8 * (size_t)k);
    }
    if (defer && h) mtl_bh_deferred_seen();
}
static void bind_handles(NVNcommandBuffer *c, int op, int st, int first, int n, const uint64_t *h) { bind_handles_at(c, op, st, first, n, h, 0); }
// Deferred binding: the GPU reads the handles at h when it executes the command, not at recording. TotK main 0x895e8 binds one texture this way through the
// pointer the game got from GetProcAddress; nxnative found BotW's engine binds every texture so and fills / replaces the handle after recording (a copy taken
// at recording gave texture 0 everywhere: the whole 3D picture black). The address is recorded and the replay reads it once a submission, at its start
// (nvn_mtl.m bh_fresh, plan item 61b). From nxnative (seedds, MIT)
NVN_NOPROTO(nvnCommandBufferBindTexturesDeferred, void, NVNcommandBuffer *c, NVNshaderStage st, int first, int n, const NVNtextureHandle *h) {
    bind_handles_at(c, OP_BIND_TEX, st, first, n, (const uint64_t *)h, 1);
}
// Image (writable texture): handle = texture pool index (GetImageHandle returns it as-is; the game does not call RegisterImage, images and textures share registration)
NVN(nvnCommandBufferBindImages, void, NVNcommandBuffer *c, NVNshaderStage st, int first, int n, const NVNimageHandle *h) {
    bind_handles(c, OP_BIND_IMG, st, first, n, (const uint64_t *)h);
}
NVN(nvnCommandBufferBindSeparateTextures, void, NVNcommandBuffer *c, NVNshaderStage st, int first, int n, const NVNseparateTextureHandle *h) {
    bind_handles(c, OP_BIND_TEX, st, first, n, (const uint64_t *)h);
}
NVN(nvnCommandBufferBindSeparateSamplers, void, NVNcommandBuffer *c, NVNshaderStage st, int first, int n, const NVNseparateSamplerHandle *h) {
    bind_handles(c, OP_BIND_SAMP, st, first, n, (const uint64_t *)h);
}
NVN(nvnCommandBufferBindTexture, void, NVNcommandBuffer *c, NVNshaderStage st, int i, NVNtextureHandle h) { uint64_t v = (uint64_t)h; bind_handles(c, OP_BIND_TEX, st, i, 1, &v); }   // combined handle, as is (nxnative)
NVN(nvnCommandBufferBindSeparateTexture, void, NVNcommandBuffer *c, NVNshaderStage st, int i, NVNseparateTextureHandle h) {
    bind_handles(c, OP_BIND_TEX, st, i, 1, &h.value);
}
NVN(nvnCommandBufferBindSeparateSampler, void, NVNcommandBuffer *c, NVNshaderStage st, int i, NVNseparateSamplerHandle h) {
    bind_handles(c, OP_BIND_SAMP, st, i, 1, &h.value);
}
#ifndef DM_TEST_DEFER_AT_RECORD   // negative control of gfx/tests t_nvn64a only: the new deferred binds read at recording
#define DEFER_NEW 1
#else
#define DEFER_NEW 0
#endif
// More binds a next game may call (plan item 64a, bayonetta3-mobile's list): one image, combined textures as BindTexture takes them, and the deferred
// image / separate texture / separate sampler binds, read at the submission's start as BindTexturesDeferred's (nvn_mtl.m bh_fresh, plan item 61b)
NVN(nvnCommandBufferBindImage, void, NVNcommandBuffer *c, NVNshaderStage st, int i, NVNimageHandle h) { uint64_t v = (uint64_t)h; bind_handles(c, OP_BIND_IMG, st, i, 1, &v); }
NVN(nvnCommandBufferBindTextures, void, NVNcommandBuffer *c, NVNshaderStage st, int first, int n, const NVNtextureHandle *h) { bind_handles(c, OP_BIND_TEX, st, first, n, (const uint64_t *)h); }
NVN_NOPROTO(nvnCommandBufferBindImagesDeferred, void, NVNcommandBuffer *c, NVNshaderStage st, int first, int n, const NVNimageHandle *h) { bind_handles_at(c, OP_BIND_IMG, st, first, n, (const uint64_t *)h, DEFER_NEW); }
NVN_NOPROTO(nvnCommandBufferBindSeparateTexturesDeferred, void, NVNcommandBuffer *c, NVNshaderStage st, int first, int n, const NVNseparateTextureHandle *h) { bind_handles_at(c, OP_BIND_TEX, st, first, n, (const uint64_t *)h, DEFER_NEW); }
NVN_NOPROTO(nvnCommandBufferBindSeparateSamplersDeferred, void, NVNcommandBuffer *c, NVNshaderStage st, int first, int n, const NVNseparateSamplerHandle *h) { bind_handles_at(c, OP_BIND_SAMP, st, first, n, (const uint64_t *)h, DEFER_NEW); }
// Line width: Metal draws lines one pixel wide; anything else is reported once (TotK main 0x83a28)
NVN(nvnCommandBufferSetLineWidth, void, NVNcommandBuffer *c, float w) { (void)c; if (w != 1.0f) { static int once; if (!once++) LOG("[nvn] SetLineWidth %.2f: lines are drawn 1 pixel wide", w); } }
NVN(nvnCommandBufferSetStencilValueMask, void, NVNcommandBuffer *c, NVNface f, int m) { *REC(c, OP_STENCIL, p_stencil) = (p_stencil){f, m, 0, 0, 0}; }
NVN(nvnCommandBufferSetStencilMask, void, NVNcommandBuffer *c, NVNface f, int m) { *REC(c, OP_STENCIL, p_stencil) = (p_stencil){f, 0, m, 0, 1}; }
NVN(nvnCommandBufferSetStencilRef, void, NVNcommandBuffer *c, NVNface f, int v) { *REC(c, OP_STENCIL, p_stencil) = (p_stencil){f, 0, 0, v, 2}; }
NVN(nvnCommandBufferSetPolygonOffsetClamp, void, NVNcommandBuffer *c, float fa, float un, float cl) {
    *REC(c, OP_POLY_OFFSET, p_poly_offset) = (p_poly_offset){fa, un, cl};
}
NVN(nvnCommandBufferDrawArrays, void, NVNcommandBuffer *c, NVNdrawPrimitive pr, int first, int n) { *REC(c, OP_DRAW, p_draw) = (p_draw){pr, first, n, 1, 0}; }
// Parameter order (mode, first, count, baseInstance, instanceCount): NVN reference nvn::CommandBuffer::DrawArraysInstanced
NVN(nvnCommandBufferDrawArraysInstanced, void, NVNcommandBuffer *c, NVNdrawPrimitive pr, int first, int n, int bi, int inst) { *REC(c, OP_DRAW, p_draw) = (p_draw){pr, first, n, inst, bi}; }
NVN(nvnCommandBufferDrawElementsInstanced, void, NVNcommandBuffer *c, NVNdrawPrimitive pr, NVNindexType it, int n, NVNbufferAddress ib,
    int bv, int bi, int inst) {
    *REC(c, OP_DRAW_IDX, p_draw_idx) = (p_draw_idx){pr, it, n, bv, bi, inst, ib};
}
NVN(nvnCommandBufferDrawElements, void, NVNcommandBuffer *c, NVNdrawPrimitive pr, NVNindexType it, int n, NVNbufferAddress ib) { h_nvnCommandBufferDrawElementsInstanced(c, pr, it, n, ib, 0, 0, 1); }
NVN(nvnCommandBufferDrawElementsBaseVertex, void, NVNcommandBuffer *c, NVNdrawPrimitive pr, NVNindexType it, int n, NVNbufferAddress ib, int bv) {
    *REC(c, OP_DRAW_IDX, p_draw_idx) = (p_draw_idx){pr, it, n, bv, 0, 1, ib};
}
NVN(nvnCommandBufferDispatchCompute, void, NVNcommandBuffer *c, int x, int y, int z) {
    // The thunk is a tail jump (br), so the return address here is the call site in the game
    *REC(c, OP_DISPATCH, p_dispatch) = (p_dispatch){x, y, z, DM_GUEST_RA()};
}
NVN(nvnCommandBufferBarrier, void, NVNcommandBuffer *c, int bits) { *(int32_t *)rec(c, OP_BARRIER, 8) = bits; }
// The replay keeps one texture ID space and one sampler ID space (registrations go by ID, the pool set here is not used): a second pool of either
// kind would read the first's objects under the same IDs (Dark Souls switches between 12 sampler pools within a frame, lordran b826e34). Red once
static void pool_second(const char *what, const void *p) {
    static _Atomic(const void *) first[2]; static _Atomic int red[2]; int k = what[0] == 's';
    const void *none = NULL; if (!p || atomic_compare_exchange_strong(&first[k], &none, p) || atomic_load(&first[k]) == p) return;
    if (!atomic_exchange(&red[k], 1)) LOG("[nvn] Set%sPool: a second %s pool (%p after %p), its IDs in a range of their own (plan item 64e; first case)",
                                          k ? "Sampler" : "Texture", what, p, atomic_load(&first[k]));
}
NVN(nvnCommandBufferSetTexturePool, void, NVNcommandBuffer *c, const NVNtexturePool *p) { pool_second("texture", p); REC(c, OP_SET_TEX_POOL, p_set_pool)->pool = (const dpool_t *)p; }
NVN(nvnCommandBufferSetSamplerPool, void, NVNcommandBuffer *c, const NVNsamplerPool *p) { pool_second("sampler", p); REC(c, OP_SET_SAMP_POOL, p_set_pool)->pool = (const dpool_t *)p; }

NVN(nvnCommandBufferCopyBufferToTexture, void, NVNcommandBuffer *c, NVNbufferAddress src, const NVNtexture *t, const NVNtextureView *v,
    const NVNcopyRegion *r, int flags) {
    p_copy_b2t *o = REC(c, OP_COPY_B2T, p_copy_b2t); o->src = src; o->tex = tex_snap(t); copy_view(&o->view, &o->has_view, v); o->r = *r; o->flags = flags;
}
NVN(nvnCommandBufferCopyTextureToBuffer, void, NVNcommandBuffer *c, const NVNtexture *t, const NVNtextureView *v, const NVNcopyRegion *r,
    NVNbufferAddress dst, int flags) {
    p_copy_b2t *o = REC(c, OP_COPY_T2B, p_copy_b2t); o->src = dst; o->tex = tex_snap(t); copy_view(&o->view, &o->has_view, v); o->r = *r; o->flags = flags;
}
// Clear a texture region with the given value (float / unsigned integer versions; mask = channel write mask)
NVN(nvnCommandBufferClearTexture, void, NVNcommandBuffer *c, const NVNtexture *t, const NVNtextureView *v, const NVNcopyRegion *r, const float *rgba, int mask) {
    p_clear_tex *o = REC(c, OP_CLEAR_TEX, p_clear_tex); o->tex = tex_snap(t); copy_view(&o->view, &o->has_view, v); o->r = *r; o->mask = mask; o->is_uint = 0; memcpy(o->c.f, rgba, 16);
}
NVN(nvnCommandBufferClearTextureui, void, NVNcommandBuffer *c, const NVNtexture *t, const NVNtextureView *v, const NVNcopyRegion *r, const uint32_t *rgba, int mask) {
    p_clear_tex *o = REC(c, OP_CLEAR_TEX, p_clear_tex); o->tex = tex_snap(t); copy_view(&o->view, &o->has_view, v); o->r = *r; o->mask = mask; o->is_uint = 1; memcpy(o->c.u, rgba, 16);
}
NVN(nvnCommandBufferCopyTextureToTexture, void, NVNcommandBuffer *c, const NVNtexture *s, const NVNtextureView *sv, const NVNcopyRegion *sr,
    const NVNtexture *d, const NVNtextureView *dv, const NVNcopyRegion *dr, int flags) {
    p_copy_t2t *o = REC(c, OP_COPY_T2T, p_copy_t2t); o->src = tex_snap(s); o->dst = tex_snap(d);
    copy_view(&o->sv, &o->has_sv, sv); copy_view(&o->dv, &o->has_dv, dv); o->sr = *sr; o->dr = *dr; o->flags = flags;
}
NVN(nvnCommandBufferSetCopyRowStride, void, NVNcommandBuffer *c, ptrdiff_t st) { REC(c, OP_COPY_STRIDE, p_copy_stride)->stride = st; }
NVN(nvnCommandBufferSetCopyImageStride, void, NVNcommandBuffer *c, ptrdiff_t st) { REC(c, OP_COPY_IMAGE_STRIDE, p_copy_stride)->stride = st; }
NVN(nvnCommandBufferSetBlendColor, void, NVNcommandBuffer *c, const float *v) { memcpy(REC(c, OP_BLEND_COLOR, p_blend_color)->rgba, v, 16); }

