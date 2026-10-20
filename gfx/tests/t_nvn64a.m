// NVN functions bayonetta3-mobile has and the host lacked (plan item 64a, first part: getters, binds, queue acquire and settings with nothing to
// set in Metal). Each now has its implementation in the slot table (none left to the red-reporting stub); the getters read back what their
// setters stored (colour state, vertex attribute and stream state, sampler builder, texture view, window crop, a texture's address, a builder's
// view offset); BindImage and BindTextures reach their slots; the deferred image, separate texture and separate sampler binds take the
// handles held at the submission's start, not at recording; the queue's acquire hands out the window's textures in turn; the checkpoint mask
// is accepted. The new deferred binds read at recording (-DDM_TEST_DEFER_AT_RECORD): 1 fails.
#include "../nvn/nvn_mtl.m"
#include "../nvn/hle_nvn.c"
const dm_game_profile dm_game = { .id = "test", .nvn_api_major = 55, .nvn_api_minor = 15, .nvn_c2 = {0x168, 128, 0x568, 128, 0x120, 8}, .nvn_depth_mode = 1 };
uint64_t g_t0_ns;
void dm_touch_screen(int w, int h) { (void)w; (void)h; }
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int n, fail;
static void check(const char *what, int ok) { n++; fail += !ok; printf("%-100s %s\n", what, ok ? "✓" : "✗"); }
static const char *const k_new[] = {"nvnColorStateGetAlphaTest", "nvnColorStateGetBlendEnable", "nvnColorStateGetLogicOp", "nvnVertexAttribStateGetFormat",
    "nvnVertexAttribStateGetStreamIndex", "nvnVertexStreamStateGetStride", "nvnVertexStreamStateGetDivisor", "nvnSamplerBuilderGetLodClamp",
    "nvnSamplerBuilderGetLodBias", "nvnSamplerBuilderGetCompare", "nvnSamplerBuilderGetBorderColor", "nvnSamplerBuilderGetMaxAnisotropy",
    "nvnTextureViewGetSwizzle", "nvnTextureViewGetDepthStencilMode", "nvnTextureGetTextureAddress", "nvnTextureBuilderGetViewOffset", "nvnWindowGetCrop",
    "nvnQueueAcquireTexture", "nvnCommandBufferBindImage", "nvnCommandBufferBindTextures", "nvnCommandBufferBindImagesDeferred",
    "nvnCommandBufferBindSeparateTexturesDeferred", "nvnCommandBufferBindSeparateSamplersDeferred", "nvnCommandBufferSetColorReductionEnable",
    "nvnCommandBufferSetColorReductionThresholds", "nvnCommandBufferSetZCullZF32CompressionEnable", "nvnDeviceSetAutomaticCheckpointsMask"};
static uint64_t g_img[2], g_tex[2], g_smp[2];
static void rewrite(void) { g_img[0] = 0x61; g_img[1] = 0x62; g_tex[0] = 0x71; g_tex[1] = 0x72; g_smp[0] = 0x81; g_smp[1] = 0x82; }

int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); mtl_init(); nvn_init_slots();
    int all = 1; for (unsigned k = 0; k < sizeof k_new / sizeof *k_new; k++) {
        unsigned i = 0; while (i < NVN_NFUNC && strcmp(nvn_names[i], k_new[k])) i++;
        if (i == NVN_NFUNC || nvn_slot[i].fn == (void *)nvn_unimpl_entry) { all = 0; printf("  not implemented: %s\n", k_new[k]); } }
    check("the 27 functions: each with its implementation in the slot table", all);

    NVNcolorState cs; h_nvnColorStateSetDefaults(&cs); h_nvnColorStateSetAlphaTest(&cs, NVN_ALPHA_FUNC_GREATER); h_nvnColorStateSetBlendEnable(&cs, 3, 1); h_nvnColorStateSetLogicOp(&cs, NVN_LOGIC_OP_XOR);
    check("colour state: alpha test, blend enable of target 3 (not 2, not 9), logic op", h_nvnColorStateGetAlphaTest(&cs) == NVN_ALPHA_FUNC_GREATER && h_nvnColorStateGetBlendEnable(&cs, 3) &&
          !h_nvnColorStateGetBlendEnable(&cs, 2) && !h_nvnColorStateGetBlendEnable(&cs, 9) && h_nvnColorStateGetLogicOp(&cs) == NVN_LOGIC_OP_XOR);
    NVNvertexAttribState va; h_nvnVertexAttribStateSetDefaults(&va); h_nvnVertexAttribStateSetFormat(&va, NVN_FORMAT_RGBA8, 0x24); h_nvnVertexAttribStateSetStreamIndex(&va, 5);
    NVNformat vf; ptrdiff_t vo; h_nvnVertexAttribStateGetFormat(&va, &vf, &vo);
    NVNvertexStreamState vs; h_nvnVertexStreamStateSetDefaults(&vs); h_nvnVertexStreamStateSetStride(&vs, 48); h_nvnVertexStreamStateSetDivisor(&vs, 3);
    check("vertex attribute (RGBA8 at 0x24, stream 5) and stream (stride 48, divisor 3)", vf == NVN_FORMAT_RGBA8 && vo == 0x24 && h_nvnVertexAttribStateGetStreamIndex(&va) == 5 &&
          h_nvnVertexStreamStateGetStride(&vs) == 48 && h_nvnVertexStreamStateGetDivisor(&vs) == 3);
    NVNsamplerBuilder sb; h_nvnSamplerBuilderSetDefaults(&sb); h_nvnSamplerBuilderSetLodClamp(&sb, 0.5f, 7); h_nvnSamplerBuilderSetLodBias(&sb, -1.25f);
    h_nvnSamplerBuilderSetCompare(&sb, NVN_COMPARE_MODE_COMPARE_R_TO_TEXTURE, NVN_COMPARE_FUNC_LEQUAL); float bc[4] = {.1f, .2f, .3f, .4f}, gb[4]; h_nvnSamplerBuilderSetBorderColor(&sb, bc);
    h_nvnSamplerBuilderSetMaxAnisotropy(&sb, 8); float lo, hi; NVNcompareMode cm; NVNcompareFunc cf;
    h_nvnSamplerBuilderGetLodClamp(&sb, &lo, &hi); h_nvnSamplerBuilderGetCompare(&sb, &cm, &cf); h_nvnSamplerBuilderGetBorderColor(&sb, gb);
    check("sampler builder: LOD clamp, bias, compare, border colour, anisotropy", lo == 0.5f && hi == 7 && h_nvnSamplerBuilderGetLodBias(&sb) == -1.25f &&
          cm == NVN_COMPARE_MODE_COMPARE_R_TO_TEXTURE && cf == NVN_COMPARE_FUNC_LEQUAL && !memcmp(bc, gb, 16) && h_nvnSamplerBuilderGetMaxAnisotropy(&sb) == 8);
    NVNtextureView v0, v1; h_nvnTextureViewSetDefaults(&v0); h_nvnTextureViewSetDefaults(&v1); h_nvnTextureViewSetSwizzle(&v1, NVN_TEXTURE_SWIZZLE_B, NVN_TEXTURE_SWIZZLE_G, NVN_TEXTURE_SWIZZLE_R, NVN_TEXTURE_SWIZZLE_ONE);
    NVNtextureSwizzle sr, sg, sbl, sa; int unset = !h_nvnTextureViewGetSwizzle(&v0, &sr, &sg, &sbl, &sa), set = h_nvnTextureViewGetSwizzle(&v1, &sr, &sg, &sbl, &sa);
    NVNtextureDepthStencilMode dsm; int ds = h_nvnTextureViewGetDepthStencilMode(&v1, &dsm);
    check("texture view: no swizzle set on a default view; B G R 1 read back; depth / stencil mode never set (not modelled)", unset && set && sr == NVN_TEXTURE_SWIZZLE_B &&
          sg == NVN_TEXTURE_SWIZZLE_G && sbl == NVN_TEXTURE_SWIZZLE_R && sa == NVN_TEXTURE_SWIZZLE_ONE && !ds && dsm == 0);
    static uint8_t store[1 << 16] __attribute__((aligned(4096)));
    tex_t t = {.mem = store}; tex_builder_t tb; memset(&tb, 0, sizeof tb); tb.d = (nvn_texdesc){.target = NVN_TEXTURE_TARGET_2D_ARRAY, .format = NVN_FORMAT_RGBA8, .width = 64, .height = 64, .depth = 6, .levels = 4, .samples = 1};
    NVNtextureView lv; h_nvnTextureViewSetDefaults(&lv); h_nvnTextureViewSetLevels(&lv, 2, 1); h_nvnTextureViewSetLayers(&lv, 3, 1); nvn_texlayout L; nvn_tex_layout(&tb.d, &L);
    check("a texture's address is its storage; a builder's view offset is level 2's offset and 3 layers", h_nvnTextureGetTextureAddress((NVNtexture *)&t) == (NVNtextureAddress)(uintptr_t)store &&
          h_nvnTextureBuilderGetViewOffset((NVNtextureBuilder *)&tb, &lv) == (ptrdiff_t)(nvn_tex_level_offset(&tb.d, 2) + 3 * L.layer_size) && h_nvnTextureBuilderGetViewOffset((NVNtextureBuilder *)&tb, NULL) == 0);
    win_t w; memset(&w, 0, sizeof w); w.ntex = 2; h_nvnWindowSetCrop((NVNwindow *)&w, 8, 16, 1280, 720); NVNrectangle cr; h_nvnWindowGetCrop((NVNwindow *)&w, &cr);
    int i0 = -1, i1 = -1, i2 = -1; NVNqueue q; int r0 = h_nvnQueueAcquireTexture(&q, (NVNwindow *)&w, &i0), r1 = h_nvnQueueAcquireTexture(&q, (NVNwindow *)&w, &i1), r2 = h_nvnQueueAcquireTexture(&q, (NVNwindow *)&w, &i2);
    check("window crop read back; the queue's acquire hands out textures 0, 1, 0 (success)", cr.x == 8 && cr.y == 16 && cr.width == 1280 && cr.height == 720 && !r0 && !r1 && !r2 && i0 == 0 && i1 == 1 && i2 == 0);
    check("checkpoint mask accepted", h_nvnDeviceSetAutomaticCheckpointsMask(NULL, 7) == 1);

    void *mem = NULL; if (posix_memalign(&mem, getpagesize(), 65536)) abort(); memset(mem, 0, 65536); pool_t pool = {.mem = mem, .size = 65536}; mtl_pool_add(mem, 65536);
    NVNcommandBuffer cb; h_nvnCommandBufferInitialize(&cb, NULL); h_nvnCommandBufferAddCommandMemory(&cb, (const NVNmemoryPool *)&pool, 0, 32768); h_nvnCommandBufferBeginRecording(&cb);
    const int fs = NVN_SHADER_STAGE_FRAGMENT; uint64_t two[2] = {0x41, 0x42};
    h_nvnCommandBufferBindImage(&cb, fs, 3, 0x33); h_nvnCommandBufferBindTextures(&cb, fs, 1, 2, (const NVNtextureHandle *)two);
    g_img[0] = 0x51; g_img[1] = 0x52; g_tex[0] = 0x53; g_tex[1] = 0x54; g_smp[0] = 0x55; g_smp[1] = 0x56;
    h_nvnCommandBufferBindImagesDeferred(&cb, fs, 5, 2, g_img); h_nvnCommandBufferBindSeparateTexturesDeferred(&cb, fs, 10, 2, (const NVNseparateTextureHandle *)g_tex);
    h_nvnCommandBufferBindSeparateSamplersDeferred(&cb, fs, 20, 2, (const NVNseparateSamplerHandle *)g_smp);
    NVNcommandHandle h = h_nvnCommandBufferEndRecording(&cb);
    rewrite();   // the game fills the arrays after recording, before submitting (as BotW's engine does, nxnative)
    uint64_t hs[1] = {(uint64_t)h}; state_reset(); mtl_sub_begin(); mtl_sub_index(hs, 1); mtl_sub_snapshot(hs, 1); mtl_exec_rest(NULL, 0); mtl_exec((const uint8_t *)(uintptr_t)h); mtl_wait(mtl_sub_commit(), 5000000000ull);
    check("BindImage: slot 3; BindTextures: slots 1 and 2", S.img[fs][3] == 0x33 && S.tex[fs][1] == 0x41 && S.tex[fs][2] == 0x42);
    check("the deferred image, separate texture and separate sampler binds: the handles at the submission's start", S.img[fs][5] == 0x61 && S.img[fs][6] == 0x62 &&
          S.tex[fs][10] == 0x71 && S.tex[fs][11] == 0x72 && S.samp[fs][20] == 0x81 && S.samp[fs][21] == 0x82);
    mtl_pool_del(mem); free(mem);
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
} }
