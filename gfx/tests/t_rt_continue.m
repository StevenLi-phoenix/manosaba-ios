// A render pass continued into a later command buffer of the same submission, checked by its pixels (plan item 179; the ten cases of GirlNet's
// d2r-mobile 639cf35 gfx/tests/t_p5r_rt_continue_gpu.m, MIT, written anew for this replay, and an eleventh). Recorded through the public NVN calls:
// an 8x4 RGBA8 target of two levels with a depth-stencil (D32F S8) one, cleared (black, 0.875, 0x2d); the first command buffer draws red at depth
// 0.5 with stencil 0x55 into the left half (scissor 4x4); the second draws green at depth 0.75 with stencil 0x66 over all of it (LESS: only the
// right half passes). Between or in the second: nothing (same), a clear of both again (all green, depth 0.75, stencil 0x66), a barrier before or
// after its SetRenderTargets, other programs before or after (once without the removed DM_NVN_RT_RANGE's read-only metadata), a copy of the colour target to a buffer (which must hold the
// first command buffer's result), a view of level 0 (the same subresource), the colour texture's ID registered again, a second program that
// samples level 1 of the colour target (cleared red beforehand: red everywhere), and a view of level 1 with the same depth target, drawn without depth
// or stencil tests (other level: level 0 keeps the first command buffer's result alone, level 1 all green; only the view differs). Every case reads back colour, depth and stencil exactly, with the bytes past
// each row untouched, and counts the passes and the attachment bytes loaded.
//   TBDR rewrites off (DM_NVN_TBDR=0, Tears of the Kingdom's default on the Mac): every case two passes, the second loading what the first stored
//   (but after a clear).
//   On (the default, and iOS's): one pass where nothing between the two needs the first one's result in memory: same, the barriers (OP_BARRIER
//   does not end a pass), the programs without read-only metadata (the continuation does not look at it), the ID registered again, and the view
//   of level 0 (the deferral, sub_eq: the same texture, level and layer); two where something does or the target differs: the clear, the copy,
//   the sampling program (a split, logged), the other level.
// Colour targets compared by texture alone in rt_same (-DDM_TEST_RT_SAME_NO_VIEW): with the rewrites on the other level continues the level-0 pass
// and draws green over it, 1 fails.
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
static int fail, count;
#define CHECK(c, ...) do { count++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)

enum { SAME, CLEAR, BARRIER_BEFORE, BARRIER_AFTER, UNKNOWN_BEFORE, UNKNOWN_AFTER, COPY, VIEW_CHANGE, REGISTRY_CHANGE, SAMPLE_ATTACHMENT, OTHER_LEVEL, NCASE };
static const char *const names[NCASE] = {"same", "clear", "barrier before", "barrier after", "unknown before", "unknown after", "copy", "view change",
                                         "registry change", "sample attachment", "other level"};
enum { M_NORMAL, M_CLEAR, M_SAMPLED, M_FIRST_ONLY };

// Colour, depth and stencil of level 0 as expected; 0 when every texel and every guard byte is right
static int planes_ok(tex_t *color, tex_t *depth, int mode, char *why, size_t n) {
    id<MTLBuffer> out[3]; id<MTLCommandBuffer> cb = [g_q commandBuffer]; id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
    for (int k = 0; k < 3; k++) {
        out[k] = [g_dev newBufferWithLength:1024 options:MTLResourceStorageModeShared]; memset(out[k].contents, 0xa7, 1024);
        if (!k) [bl copyFromTexture:tex_mtl(color) sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(8, 4, 1)
                           toBuffer:out[k] destinationOffset:0 destinationBytesPerRow:256 destinationBytesPerImage:1024];
        else [bl copyFromTexture:tex_mtl(depth) sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(8, 4, 1)
                        toBuffer:out[k] destinationOffset:0 destinationBytesPerRow:256 destinationBytesPerImage:1024
                         options:k == 1 ? MTLBlitOptionDepthFromDepthStencil : MTLBlitOptionStencilFromDepthStencil];
    }
    [bl endEncoding]; [cb commit]; [cb waitUntilCompleted];
    if (cb.status != MTLCommandBufferStatusCompleted) { snprintf(why, n, "readback failed"); return 0; }
    for (int y = 0; y < 4; y++) for (int x = 0; x < 8; x++) {
        int second = mode == M_CLEAR || (x >= 4 && mode != M_FIRST_ONLY), cleared = mode == M_FIRST_ONLY && x >= 4;
        const uint8_t *c = (const uint8_t *)out[0].contents + y * 256 + x * 4;
        const uint8_t want[4] = {mode == M_SAMPLED || !(second || cleared) ? 255 : 0, second && mode != M_SAMPLED ? 255 : 0, 0, 255};
        float d; memcpy(&d, (const uint8_t *)out[1].contents + y * 256 + x * 4, 4); uint8_t s = *((const uint8_t *)out[2].contents + y * 256 + x);
        if (memcmp(c, want, 4)) { snprintf(why, n, "colour (%d,%d) %02x%02x%02x%02x", x, y, c[0], c[1], c[2], c[3]); return 0; }
        if (d != (cleared ? 0.875f : second ? 0.75f : 0.5f)) { snprintf(why, n, "depth (%d,%d) %g", x, y, d); return 0; }
        if (s != (cleared ? 0x2d : second ? 0x66 : 0x55)) { snprintf(why, n, "stencil (%d,%d) %#x", x, y, s); return 0; }
    }
    for (int k = 0; k < 3; k++) for (int y = 0; y < 4; y++) for (int x = k == 2 ? 8 : 32; x < 256; x++)
        if (*((const uint8_t *)out[k].contents + y * 256 + x) != 0xa7) { snprintf(why, n, "plane %d row %d written past its end", k, y); return 0; }
    return 1;
}
static int level1_green(tex_t *color, char *why, size_t n) {   // the other level: all of level 1 (4x2) green
    id<MTLBuffer> out = [g_dev newBufferWithLength:32 options:MTLResourceStorageModeShared]; id<MTLCommandBuffer> cb = [g_q commandBuffer];
    id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
    [bl copyFromTexture:tex_mtl(color) sourceSlice:0 sourceLevel:1 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(4, 2, 1) toBuffer:out
      destinationOffset:0 destinationBytesPerRow:16 destinationBytesPerImage:32];
    [bl endEncoding]; [cb commit]; [cb waitUntilCompleted];
    const uint8_t *px = out.contents;
    for (int i = 0; i < 8; i++) if (memcmp(px + i * 4, (uint8_t[]){0, 255, 0, 255}, 4)) { snprintf(why, n, "level 1 (%d,%d) %02x%02x%02x%02x", i % 4, i / 4, px[i * 4], px[i * 4 + 1], px[i * 4 + 2], px[i * 4 + 3]); return 0; }
    return 1;
}

static void run_case(int sc, prog_t *pg, int tbdr) {
    tex_t color = {.serial = 2100100 + sc * 3}, depth = {.serial = 2100101 + sc * 3};
    color.d = (nvn_texdesc){.target = NVN_TEXTURE_TARGET_2D, .format = NVN_FORMAT_RGBA8, .width = 8, .height = 4, .depth = 1, .levels = 2, .samples = 1};
    depth.d = color.d; depth.d.format = NVN_FORMAT_DEPTH32F_STENCIL8; depth.d.levels = 1;
    if (!tex_mtl(&color) || !tex_mtl(&depth)) { CHECK(0, "%s: targets", names[sc]); return; }
    mtl_reg_tex(1900, &color, NULL);
    if (sc == SAMPLE_ATTACHMENT) {   // level 1 red, for the sampling program
        id<MTLCommandBuffer> cb = [g_q commandBuffer]; MTLRenderPassDescriptor *pd = [MTLRenderPassDescriptor renderPassDescriptor];
        pd.colorAttachments[0].texture = tex_mtl(&color); pd.colorAttachments[0].level = 1; pd.colorAttachments[0].loadAction = MTLLoadActionClear;
        pd.colorAttachments[0].storeAction = MTLStoreActionStore; pd.colorAttachments[0].clearColor = MTLClearColorMake(1, 0, 0, 1);
        [[cb renderCommandEncoderWithDescriptor:pd] endEncoding]; [cb commit]; [cb waitUntilCompleted];
    }
    void *mem = NULL; if (posix_memalign(&mem, getpagesize(), 131072)) abort(); memset(mem, 0, 131072);
    pool_t pool = {.mem = mem, .size = 131072}; mtl_pool_add(mem, 131072);
    NVNcommandBuffer cmd; h_nvnCommandBufferInitialize(&cmd, NULL);
    h_nvnCommandBufferAddCommandMemory(&cmd, (const NVNmemoryPool *)&pool, 0, 65536);
    h_nvnCommandBufferBeginRecording(&cmd);
    const NVNtexture *tg[] = {(const NVNtexture *)&color};
    h_nvnCommandBufferSetRenderTargets(&cmd, 1, tg, NULL, (const NVNtexture *)&depth, NULL);
    h_nvnCommandBufferSetViewports(&cmd, 0, 1, (float[]){0, 0, 8, 4});
    h_nvnCommandBufferClearColor(&cmd, 0, (float[]){0, 0, 0, 1}, 15);
    h_nvnCommandBufferClearDepthStencil(&cmd, 0.875f, 1, 0x2d, 255);
    NVNdepthStencilState ds; h_nvnDepthStencilStateSetDefaults(&ds);
    h_nvnDepthStencilStateSetDepthTestEnable(&ds, 1); h_nvnDepthStencilStateSetDepthWriteEnable(&ds, 1); h_nvnDepthStencilStateSetDepthFunc(&ds, NVN_DEPTH_FUNC_LESS);
    h_nvnDepthStencilStateSetStencilTestEnable(&ds, 1); h_nvnDepthStencilStateSetStencilFunc(&ds, NVN_FACE_FRONT_AND_BACK, NVN_STENCIL_FUNC_ALWAYS);
    h_nvnDepthStencilStateSetStencilOp(&ds, NVN_FACE_FRONT_AND_BACK, NVN_STENCIL_OP_KEEP, NVN_STENCIL_OP_KEEP, NVN_STENCIL_OP_REPLACE);
    h_nvnCommandBufferBindDepthStencilState(&cmd, &ds);
    h_nvnCommandBufferSetStencilRef(&cmd, NVN_FACE_FRONT_AND_BACK, 0x55);
    h_nvnCommandBufferSetScissor(&cmd, 0, 0, 4, 4);
    h_nvnCommandBufferBindProgram(&cmd, (const NVNprogram *)&pg[sc == UNKNOWN_BEFORE ? 2 : 0], 3);
    h_nvnCommandBufferDrawArrays(&cmd, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3);
    NVNcommandHandle first = h_nvnCommandBufferEndRecording(&cmd);
    h_nvnCommandBufferBeginRecording(&cmd);
    if (sc == BARRIER_BEFORE) h_nvnCommandBufferBarrier(&cmd, 1);
    if (sc == COPY) h_nvnCommandBufferCopyTextureToBuffer(&cmd, (const NVNtexture *)&color, NULL, &(NVNcopyRegion){0, 0, 0, 8, 4, 1}, (NVNbufferAddress)(uintptr_t)mem + 65536, 0);
    NVNtextureView view; h_nvnTextureViewSetDefaults(&view); h_nvnTextureViewSetLevels(&view, sc == OTHER_LEVEL, 1); const NVNtextureView *vw[] = {&view};
    h_nvnCommandBufferSetRenderTargets(&cmd, 1, tg, sc == VIEW_CHANGE || sc == OTHER_LEVEL ? vw : NULL, (const NVNtexture *)&depth, NULL);
    if (sc == BARRIER_AFTER) h_nvnCommandBufferBarrier(&cmd, 1);
    NVNdepthStencilState off; h_nvnDepthStencilStateSetDefaults(&off);
    if (sc == OTHER_LEVEL) { h_nvnCommandBufferSetViewports(&cmd, 0, 1, (float[]){0, 0, 4, 2}); h_nvnCommandBufferBindDepthStencilState(&cmd, &off); }
    h_nvnCommandBufferSetScissor(&cmd, 0, 0, sc == OTHER_LEVEL ? 4 : 8, sc == OTHER_LEVEL ? 2 : 4);
    if (sc == CLEAR) { h_nvnCommandBufferClearColor(&cmd, 0, (float[]){0, 0, 1, 1}, 15); h_nvnCommandBufferClearDepthStencil(&cmd, 0.875f, 1, 0x2d, 255); }
    h_nvnCommandBufferSetStencilRef(&cmd, NVN_FACE_FRONT_AND_BACK, 0x66);
    h_nvnCommandBufferBindProgram(&cmd, (const NVNprogram *)&pg[sc == UNKNOWN_AFTER ? 3 : sc == SAMPLE_ATTACHMENT ? 4 : 1], 3);
    if (sc == SAMPLE_ATTACHMENT) h_nvnCommandBufferBindTexture(&cmd, NVN_SHADER_STAGE_FRAGMENT, 0, 1900);
    h_nvnCommandBufferDrawArrays(&cmd, NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3);
    NVNcommandHandle second = h_nvnCommandBufferEndRecording(&cmd);
    // the submission as nvnQueueSubmitCommands makes it, the texture ID registered again between the two for that case
    state_reset(); memset(&R, 0, sizeof R);
    uint64_t passes = g_stat_pass, draws = g_stat_draw, loads = g_bw_load, h[2] = {(uint64_t)first, (uint64_t)second};
    mtl_sub_begin(); mtl_sub_index(h, 2); mtl_sub_snapshot(h, 2);
    mtl_exec_rest(h + 1, 1); mtl_exec((const uint8_t *)(uintptr_t)h[0]);
    if (sc == REGISTRY_CHANGE) mtl_reg_tex(1900, &color, NULL);
    mtl_exec_rest(NULL, 0); mtl_exec((const uint8_t *)(uintptr_t)h[1]);
    int done = mtl_wait(mtl_sub_commit(), 5000000000ull) && !mtl_last_commit_error();
    char why[160] = "";
    int ok = done && planes_ok(&color, &depth, sc == CLEAR ? M_CLEAR : sc == SAMPLE_ATTACHMENT ? M_SAMPLED : sc == OTHER_LEVEL ? M_FIRST_ONLY : M_NORMAL, why, sizeof why);
    if (ok && sc == OTHER_LEVEL) ok = level1_green(&color, why, sizeof why);
    if (ok && sc == COPY) for (int y = 0; y < 4 && ok; y++) for (int x = 0; x < 8 && ok; x++) {   // the copy holds the first command buffer's result
        const uint8_t want[4] = {x < 4 ? 255 : 0, 0, 0, 255};
        if (memcmp((uint8_t *)mem + 65536 + (y * 8 + x) * 4, want, 4)) { ok = 0; snprintf(why, sizeof why, "the copy (%d,%d)", x, y); }
    }
    uint64_t np = g_stat_pass - passes, nl = g_bw_load - loads;
    int one = tbdr && (sc == SAME || sc == BARRIER_BEFORE || sc == BARRIER_AFTER || sc == UNKNOWN_BEFORE || sc == UNKNOWN_AFTER || sc == VIEW_CHANGE || sc == REGISTRY_CHANGE);
    // loaded: nothing in one pass; in two, the second loads the colour (128 bytes) and the depth-stencil (160) the first stored, but after a clear;
    // the other level loads the depth-stencil and its colour level (4x2: 32 bytes), which with the rewrites on its first draw covers without tests
    // or blending (first_draw_covers: DontCare)
    uint64_t want_load = one || sc == CLEAR ? 0 : sc == OTHER_LEVEL ? (tbdr ? 160 : 192) : 288;
    CHECK(ok && g_stat_draw - draws == 2 && np == (uint64_t)(one ? 1 : 2) && nl == want_load, "%-17s pixels exact, %llu pass%s (%d expected), %llu bytes loaded (%llu expected)%s%s",
          names[sc], (unsigned long long)np, np == 1 ? "" : "es", one ? 1 : 2, (unsigned long long)nl, (unsigned long long)want_load, *why ? ": " : "", why);
    h_nvnCommandBufferFinalize(&cmd); mtl_pool_del(mem); free(mem);
}

int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); mtl_init();
    int tbdr = tbdr_on(); printf("TBDR rewrites %s\n", tbdr ? "on" : "off (DM_NVN_TBDR=0)");
    NSString *src = @"#include <metal_stdlib>\nusing namespace metal;\n"
        "vertex float4 v(uint n [[vertex_id]]) { float2 p[3] = {float2(-1,-1), float2(3,-1), float2(-1,3)}; return float4(p[n], .5, 1); }\n"
        "vertex float4 w(uint n [[vertex_id]]) { float2 p[3] = {float2(-1,-1), float2(3,-1), float2(-1,3)}; return float4(p[n], .75, 1); }\n"
        "fragment float4 red() { return float4(1, 0, 0, 1); }\nfragment float4 green() { return float4(0, 1, 0, 1); }\n"
        "struct A { texture2d<float> t [[id(0)]]; };\nfragment float4 sampled(constant A &a [[buffer(0)]]) { return a.t.read(uint2(0), 1); }\n";
    NSError *e = nil; id<MTLLibrary> lib = [g_dev newLibraryWithSource:src options:nil error:&e];
    if (!lib) { printf("%s\n", e.localizedDescription.UTF8String); return 2; }
    static prog_t pg[5]; static NvProg *keep[5];
    for (int k = 0; k < 5; k++) {   // 0 red, 1 green, 2 / 3 the same as other programs, 4 sampling level 1 of its texture
        pg[k] = (prog_t){.serial = 2100000 + k}; NvProg *p = [NvProg new];
        p->sh[0] = [NvShader new]; p->sh[0]->fn = [lib newFunctionWithName:k == 0 || k == 2 ? @"v" : @"w"];
        p->sh[1] = [NvShader new]; p->sh[1]->fn = [lib newFunctionWithName:k == 4 ? @"sampled" : k == 0 || k == 2 ? @"red" : @"green"];
        p->sh[1]->stage = 1; p->sh[1]->rf.color_mask = 1;
        if (k == 4) { p->sh[1]->rf.nb = 1; p->sh[1]->rf.b[0] = (nvn_rbind){.kind = NVN_RB_TEX, .reg = 0, .dtt = 2, .decl_tt = MTLTextureType2D}; p->sh[1]->ae = [p->sh[1]->fn newArgumentEncoderWithBufferIndex:0]; }
        serial_put(pg[k].serial, p); keep[k] = p;
    }
    for (int k = 0; k < NCASE; k++) run_case(k, pg, tbdr);
    printf("== %d checks, %d failed ==\n", count, fail); return fail != 0;
} }
