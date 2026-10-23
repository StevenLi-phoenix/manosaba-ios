// gfx/nvn/mtl/store_index.inc, part of nvn_mtl.m (included there in order, one translation unit): The store lookahead index's backend side: what the index needs of the backend, its build at the start of a submit
// The index itself is gfx/nvn/core/store_index.c, the ledger's check against it gfx/nvn/core/tbdr.c xv_forward (plan item 264)
int nvn_be_prog_refl(const prog_t *p, const nvn_refl *rf[6], int stage[6]) {
    __unsafe_unretained NvProg *pr = prog_get(p); if (!pr) return 0;
    for (int st = 0; st < 6; st++) { __unsafe_unretained NvShader *sh = pr->sh[st]; rf[st] = sh ? &sh->rf : NULL; stage[st] = sh ? sh->stage : -1; }
    return 1;
}
void nvn_be_bind_sers(const p_bind_handles *b, int n, uint64_t *ser) { bh_fresh(b); for (int k = 0; k < n; k++) ser[k] = h_ser(b->h[k]); }
void mtl_sub_index(const uint64_t *h, int n) {   // Start of submit (hle_nvn.c QueueSubmitCommands): build this submit's store lookahead index
    g_xv.ntouch = 0;
    if (!sdx_begin(tbdr_part(TBP_SDX))) { g_xv.n = 0; g_xv.block = 1; return; }   // Index unavailable: clear the ledger and add nothing in this submit (the next touch cannot be verified)
    g_xv.block = 0;
    for (int i = 0; i < g_xv.n; i++) sdx_bucket(g_xv.ent[i].ser, 1);   // Textures in the ledger enter the candidate set: otherwise, when one is only sampled and not used as an attachment in this submit, its read events are not indexed and the fallback cannot see them
    static sdx_start st;   // (7.7 KB: not on the stack of the thread that submits)
    st.rt = R.has_rt ? &R.rt : NULL;   // The targets at the start of the submit (the first draw may reopen the render pass without going through SetRenderTargets)
    st.prog = S.prog; st.render_off = g_render_off;   // render enable is queue state carried from the last submission
    st.sc = R.sc; st.has_sc = R.has_sc;
    for (int s = 0; s < 6; s++) { for (int k = 0; k < 128; k++) st.slot[s][k] = S.tex[s][k] ? h_ser(S.tex[s][k]) : 0;
                                  for (int k = 0; k < 32; k++) st.slot[s][128 + k] = S.img[s][k] ? h_ser(S.img[s][k]) : 0; }
    sdx_build(&st, h, n);
    if (g_sdx.ok) xv_forward();
}
