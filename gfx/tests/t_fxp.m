// MetalFX for the game's own upscale of its scene into the canvas (plan item 63, nvn_mtl.m fxp_dock / fxp_scene), with the two passes named (as the
// profile's game.json upscale_pass / compose_pass do), a 32x18 RGBA8 scene bound at fragment texture slot 0 and a 64x36 RGBA8 canvas.
// The upscale pass (its constants in fragment uniform slot 0: c[0].xy = the valid region over the output size, c[0].zw = 0.5 c[0].xy - 0.5, c[4].xy
// = the output size as integers; its own draw writes magenta):
//   MetalFX upscaling on, the whole scene valid and one colour: the canvas that colour (MetalFX wrote it, the draw left out)
//   the valid region the top-left 16x9, another colour outside it: the canvas the region's colour (only the region upscaled), short of its last 8
//   rows and columns (2 texels of the scene: MetalFX's filter reads one texel past the region there, as the game's own does; T_FXP_MAP=1 draws it)
//   MetalFX upscaling off: magenta (the game's draw)
//   constants not of that layout (c[0].z 0): magenta
//   a clear of the canvas pending before the pass, then a 1x1 white draw at the corner: the corner white, the rest the scene's colour
// The compose pass (its draw writes the width and height of the texture it samples at slot 0 and the blue of that texture's centre), then a 1x1
// draw at the corner by another shader that samples slot 0 the same way:
//   MetalFX on: the compose pass samples a 64x36 texture whose centre is the scene's colour; the draw after it the 32x18 scene
//   MetalFX off: the compose pass samples the 32x18 scene
// The draw encoded as well (-DDM_TEST_FXP_DRAWN): 3 fail; the whole scene texture upscaled (-DDM_TEST_FXP_NO_CONTENT): 1 fails; the pending clear
// kept (-DDM_TEST_FXP_CLEAR_KEPT): 1 fails; the constants not checked (-DDM_TEST_FXP_NO_CONST): 1 fails; the compose pass not bound to the
// upscaled scene (-DDM_TEST_FXP_SCENE_NOT_BOUND): 1 fails; the substitute kept for the draws after it (-DDM_TEST_FXP_SUB_KEPT): 2 fail.
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
enum { SW = 32, SH = 18, CW = 64, CH = 36 };
static uint8_t *g_mem; static pool_t g_pool; static ptrdiff_t g_cmd;
static prog_t mkprog(id<MTLLibrary> lib, const char *frag, const char *key, uint64_t serial) {
    prog_t pg = {.serial = serial}; NvProg *p = [NvProg new];
    p->sh[0] = [NvShader new]; p->sh[0]->fn = [lib newFunctionWithName:@"v"]; p->sh[1] = [NvShader new]; p->sh[1]->fn = [lib newFunctionWithName:@(frag)]; p->sh[1]->stage = 1; p->sh[1]->rf.color_mask = 1;
    strlcpy(p->sh[1]->key, key, sizeof p->sh[1]->key); serial_put(serial, p); return pg;
}
static void fill(id<MTLTexture> t, const uint8_t in[4], const uint8_t out[4], int vw, int vh) {   // [0, vw) x [0, vh) in, the rest out
    id<MTLBuffer> b = [g_dev newBufferWithLength:SW * SH * 4 options:MTLResourceStorageModeShared]; uint8_t *p = b.contents;
    for (int y = 0; y < SH; y++) for (int x = 0; x < SW; x++) memcpy(p + 4 * (y * SW + x), x < vw && y < vh ? in : out, 4);
    id<MTLCommandBuffer> cb = [g_q commandBuffer]; id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
    [bl copyFromBuffer:b sourceOffset:0 sourceBytesPerRow:SW * 4 sourceBytesPerImage:SW * SH * 4 sourceSize:MTLSizeMake(SW, SH, 1) toTexture:t destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
    [bl endEncoding]; [cb commit]; [cb waitUntilCompleted];
}
static void read_canvas(id<MTLTexture> t, uint8_t *out) {
    id<MTLBuffer> b = [g_dev newBufferWithLength:CW * CH * 4 options:MTLResourceStorageModeShared]; id<MTLCommandBuffer> cb = [g_q commandBuffer]; id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
    [bl copyFromTexture:t sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(CW, CH, 1) toBuffer:b destinationOffset:0 destinationBytesPerRow:CW * 4 destinationBytesPerImage:CW * CH * 4];
    [bl endEncoding]; [cb commit]; [cb waitUntilCompleted]; memcpy(out, b.contents, CW * CH * 4);
}
static int near(const uint8_t *p, const uint8_t c[4]) { for (int i = 0; i < 4; i++) if (abs((int)p[i] - (int)c[i]) > 6) return 0; return 1; }
static int count_near(const uint8_t *px, const uint8_t c[4], int skip_corner) { int k = 0; for (int i = 0; i < CW * CH; i++) if (!(skip_corner && i == 0)) k += near(px + 4 * i, c); return k; }
static prog_t g_easu, g_white, g_comp, g_after; static uint64_t g_th, g_ubo; static uint64_t g_canvas_ser = 2800100;
// One submission: [a clear of the canvas,] the upscale pass, [a 1x1 white draw at the corner]; the canvas read back
static void pass_with(const prog_t *first, const prog_t *after, int clear_first, uint8_t *px) {
    tex_t canvas = {.magic = MAGIC('T', 'E', 'X', 0), .serial = g_canvas_ser++};
    canvas.d = (nvn_texdesc){.target = NVN_TEXTURE_TARGET_2D, .format = NVN_FORMAT_RGBA8, .width = CW, .height = CH, .depth = 1, .levels = 1, .samples = 1};
    id<MTLTexture> ct = tex_mtl(&canvas); if (!ct) { printf("no canvas\n"); exit(2); }
    NVNcommandBuffer c; h_nvnCommandBufferInitialize(&c, NULL); h_nvnCommandBufferAddCommandMemory(&c, (const NVNmemoryPool *)&g_pool, g_cmd, 16384); g_cmd += 16384; h_nvnCommandBufferBeginRecording(&c);
    const NVNtexture *tg[] = {(const NVNtexture *)&canvas}; h_nvnCommandBufferSetRenderTargets(&c, 1, tg, NULL, NULL, NULL);
    h_nvnCommandBufferSetViewports(&c, 0, 1, (float[]){0, 0, CW, CH}); h_nvnCommandBufferSetScissor(&c, 0, 0, CW, CH);
    if (clear_first) h_nvnCommandBufferClearColor(&c, 0, (float[]){1, 0, 0, 1}, 15);
    h_nvnCommandBufferBindTexture(&c, NVN_SHADER_STAGE_FRAGMENT, 0, (NVNtextureHandle)g_th);
    h_nvnCommandBufferBindUniformBuffer(&c, NVN_SHADER_STAGE_FRAGMENT, 0, g_ubo, 256);
    h_nvnCommandBufferBindProgram(&c, (const NVNprogram *)first, 3); h_nvnCommandBufferDrawArrays(&c, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3);
    if (after) { h_nvnCommandBufferSetScissor(&c, 0, 0, 1, 1); h_nvnCommandBufferBindProgram(&c, (const NVNprogram *)after, 3); h_nvnCommandBufferDrawArrays(&c, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3); }
    NVNcommandHandle h = h_nvnCommandBufferEndRecording(&c); uint64_t hs[1] = {(uint64_t)h}; state_reset(); memset(&R, 0, sizeof R);
    mtl_sub_begin(); mtl_sub_index(hs, 1); mtl_sub_snapshot(hs, 1); nvn_exec_rest(NULL, 0); nvn_exec((const uint8_t *)(uintptr_t)h); mtl_wait(mtl_sub_commit(), 5000000000ull);
    read_canvas(tex_mtl(&canvas), px);
}
static void pass(int clear_first, int white_after, uint8_t *px) { pass_with(&g_easu, white_after ? &g_white : NULL, clear_first, px); }
static void constants(int vw, int vh, int bad) {   // the pass's uniform block: valid region vw x vh of the scene, output CW x CH
    float k[4] = {(float)vw / CW, (float)vh / CH, 0, 0}; k[2] = bad ? 0 : 0.5f * k[0] - 0.5f; k[3] = 0.5f * k[1] - 0.5f;
    uint32_t o[2] = {CW, CH}; memcpy(g_mem + (g_ubo - (uint64_t)(uintptr_t)g_mem), k, sizeof k); memcpy(g_mem + (g_ubo - (uint64_t)(uintptr_t)g_mem) + 64, o, sizeof o);
}

int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); setenv("DM_UPSCALE", "metalfx", 1); mtl_init(); mtl_set_upscale_pass("fa11fa11fa11fa11", 0, "c0c0c0c0c0c0c0c0", 0);   // (as DeviceInitialize does from the profile)
    if (posix_memalign((void **)&g_mem, getpagesize(), 1 << 20)) abort(); memset(g_mem, 0, 1 << 20);
    g_pool = (pool_t){.mem = g_mem, .size = 1 << 20}; mtl_pool_add(g_mem, 1 << 20); g_cmd = 0x20000; g_ubo = (uint64_t)(uintptr_t)(g_mem + 0x10000);
    NSError *e = nil; id<MTLLibrary> lib = [g_dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\nconstant float2 P[3] = {float2(-1,-1), float2(3,-1), float2(-1,3)};\n"
        "vertex float4 v(uint n [[vertex_id]]) { return float4(P[n], 0.5, 1); }\n"
        "fragment float4 magenta() { return float4(1, 0, 1, 1); }\nfragment float4 white() { return float4(1, 1, 1, 1); }\n"
        "struct A { texture2d<float> t [[id(0)]]; };\n"
        "fragment float4 compose(constant A &a [[buffer(0)]]) { constexpr sampler s(filter::nearest); return float4(a.t.get_width() / 255.0, a.t.get_height() / 255.0, a.t.sample(s, float2(0.5)).b, 1); }\n" options:nil error:&e];
    if (!lib) { printf("%s\n", e.localizedDescription.UTF8String); return 2; }
    g_easu = mkprog(lib, "magenta", "fa11fa11fa11fa11_b", 2800001); g_white = mkprog(lib, "white", "0000000000000001_b", 2800002);
    g_comp = mkprog(lib, "compose", "c0c0c0c0c0c0c0c0_b", 2800003); g_after = mkprog(lib, "compose", "0000000000000002_b", 2800004);
    for (int i = 0; i < 2; i++) { NvProg *pp = (NvProg *)serial_get(i ? g_after.serial : g_comp.serial); NvShader *f = pp->sh[1];
        f->rf.nb = 1; f->rf.b[0] = (nvn_rbind){.kind = NVN_RB_TEX, .reg = 0, .dtt = 2, .decl_tt = MTLTextureType2D}; f->ae = [f->fn newArgumentEncoderWithBufferIndex:0]; }
    tex_t scene = {.magic = MAGIC('T', 'E', 'X', 0), .serial = 2800010};
    scene.d = (nvn_texdesc){.target = NVN_TEXTURE_TARGET_2D, .format = NVN_FORMAT_RGBA8, .width = SW, .height = SH, .depth = 1, .levels = 1, .samples = 1};
    id<MTLTexture> st = tex_mtl(&scene); if (!st) { printf("no scene texture\n"); return 2; }
    NVNtexturePool tp; h_nvnTexturePoolInitialize(&tp, (const NVNmemoryPool *)&g_pool, 0x8000, 64);
    h_nvnTexturePoolRegisterTexture(&tp, 5, (const NVNtexture *)&scene, NULL); g_th = (uint64_t)h_nvnDeviceGetTextureHandle(NULL, 5, 0);
    const uint8_t A[4] = {40, 120, 200, 255}, B[4] = {250, 10, 10, 255}, M[4] = {255, 0, 255, 255}, W[4] = {255, 255, 255, 255};
    static uint8_t px[CW * CH * 4]; char line[200];

    fill(st, A, A, SW, SH); constants(SW, SH, 0); pass(0, 0, px);
    snprintf(line, sizeof line, "MetalFX on, the whole scene valid, one colour: the canvas that colour (%d of %d pixels)", count_near(px, A, 0), CW * CH);
    check(line, count_near(px, A, 0) == CW * CH);
    fill(st, A, B, SW / 2, SH / 2); constants(SW / 2, SH / 2, 0); pass(0, 0, px);
    int in = 0; for (int y = 0; y < CH - 8; y++) for (int x = 0; x < CW - 8; x++) in += near(px + 4 * (y * CW + x), A);
    snprintf(line, sizeof line, "... the valid region 16x9, another colour outside: the region's colour (%d of %d pixels short of the last 8 rows / columns)", in, (CW - 8) * (CH - 8));
    check(line, in == (CW - 8) * (CH - 8));
    if (getenv("T_FXP_MAP")) for (int y = 0; y < CH; y++) { for (int x = 0; x < CW; x++) putchar(near(px + 4 * (y * CW + x), A) ? '.' : near(px + 4 * (y * CW + x), B) ? 'B' : 'x'); putchar('\n'); }
    mtl_upscale_set(0); fill(st, A, A, SW, SH); constants(SW, SH, 0); pass(0, 0, px); mtl_upscale_set(1);
    check("MetalFX upscaling off: magenta (the game's draw)", count_near(px, M, 0) == CW * CH);
    constants(SW, SH, 1); pass(0, 0, px);
    check("constants not of the layout (c[0].z 0): magenta (the game's draw)", count_near(px, M, 0) == CW * CH);
    constants(SW, SH, 0); pass(1, 1, px);
    snprintf(line, sizeof line, "a clear pending before the pass, a white corner after: the corner white, the rest the scene's (%d of %d)", count_near(px, A, 1), CW * CH - 1);
    check(line, near(px, W) && count_near(px, A, 1) == CW * CH - 1);
    // the compose pass, then a draw by another shader at the corner
    fill(st, A, A, SW, SH); pass_with(&g_comp, &g_after, 0, px);
    snprintf(line, sizeof line, "compose pass, MetalFX on: samples a 64x36 texture, its centre the scene's blue (got %ux%u, blue %u)", px[4], px[5], px[6]);
    check(line, px[4] == CW && px[5] == CH && abs((int)px[6] - A[2]) <= 6);
    snprintf(line, sizeof line, "... the draw after it samples the 32x18 scene (got %ux%u)", px[0], px[1]);
    check(line, px[0] == SW && px[1] == SH);
    mtl_upscale_set(0); pass_with(&g_comp, NULL, 0, px); mtl_upscale_set(1);
    snprintf(line, sizeof line, "compose pass, MetalFX off: samples the 32x18 scene (got %ux%u)", px[4], px[5]);
    check(line, px[4] == SW && px[5] == SH);
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
} }
