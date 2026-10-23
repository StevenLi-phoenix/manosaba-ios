// Draws that sample what their own pass draws (nvn_mtl.m fbx_prepare / fbx_sub, plan item 42), on the GPU through the production replay: an 8x8
// RGBA8 target, programs that write a constant (B, C) and one that reads the target at its pixel and adds 0.25.
// Default (a copy only after a texture-invalidating barrier that follows draws of the pass, as NVN defines it):
//   a   clear 0.1, B, barrier, the reading draw: B + 0.25, one copy
//   a2  C in an earlier pass; then B and the reading draw without a barrier: C + 0.25 (what the pass started with, as the console shows), no copy
//   c1  B, barrier, two reading draws: the second sees what its reopened pass started with, B + 0.25; one copy
//   c2  the same with a barrier between the reading draws: B + 0.5, two copies
//   e   drawing level 1 while sampling a view of level 0 only: C + 0.25 in level 1, no copy
// DM_NVN_FBX=all (every such draw; tomodachi-native's choice): a and c without barriers (B + 0.25, B + 0.5), b (the first draw of a pass reads
// what earlier passes left), f (a clear still pending when the reading draw opens the pass is in the copy), e.
// DM_NVN_FBX=split (plan item 165, after MacNativeGames PR #67): no copies, the pass stored and reopened before a draw that samples an attachment
// cleared or written by a plain draw of its pass: sa (B, the reading draw: B + 0.25, one split), sc (two reading draws in a row share one split:
// B + 0.25), sd (B, reading, C, reading: C + 0.25, two splits), sb (the first draw of a pass: what earlier passes left, no split), sf (a clear
// pending when the reading draw opens the pass: flushed first, 0.3 + 0.25), sg (reading, a texture barrier, reading: the second sees the first,
// C + 0.5, one split), e. A split before every reading draw (-DDM_TEST_FBX_SPLIT_EACH) fails sc.
// After tomodachi-native's attachment copies (4dbd30f / da08d11, MIT), written for this replay. Negative control: DM_NVN_FBX=0 fails a, c2
// (and in the all set a, c, f).
// Reading from the tile (fbx_fetch): a decal program whose vertex shader writes ((w + x) / 2, (w - y) / 2, 0, w) of its position (w = 2) to locn1 and
// whose fragment shader samples the target at that varying's xy / w, with its readable-tile version (as dmsh_fetch.py writes one): fa (B, barrier,
// decal: B + 0.25, no copy, read from the tile), fb (B, barrier, decal, barrier, decal: B + 0.5, the second sees the first, no copy), fc (the same
// with the vertex side not proven: one copy), fd (a viewport over part of the target: one copy). -DDM_TEST_FBX_NO_FETCH fails fa, fb and fr (4).
// fr: an R32Float target sampled through the swizzle (red, zero, zero, one), its format's own expansion, read from the tile: (0.5, 0, 0, 1), no copy;
// fs: the swizzle (red, red, red, one) changes what a sample returns: a copy, (0.5, 0.5, 0.5). -DDM_TEST_FBX_SWIZZLE_ANY fails fs (2).
// bk: bf_fn's binary-keyed results (plan item 185): the same inputs give the very function the first call made, from the fast table; another
// attachment map, colour mask or constant another one. -DDM_TEST_BF_KEY_NO_LMAP (the key without the map) fails the map check (1).
// ar: arena_alloc hands its buffer back unretained (plan item 185): 100 allocations leave the buffer's retain count as it was.
// -DDM_TEST_ARENA_AUTORELEASING (the autoreleasing out parameter, as before) fails it (+100).
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define EXPECT(name, want, got) do { uint64_t w = (want), g = (got); count++; printf("%-78s expected %-4llu → got %-4llu %s\n", name, (unsigned long long)w, (unsigned long long)g, w == g ? "✓" : "✗"); fail += w != g; } while (0)
typedef struct { uint8_t *p; size_t n; } chain;
static chain chain_new(void) { chain c = {0}; if (posix_memalign((void **)&c.p, getpagesize(), 65536)) abort(); memset(c.p, 0, 65536); mtl_pool_add(c.p, 65536); return c; }
static void emit(chain *c, unsigned op, const void *a, size_t n) { size_t z = (8 + n + 7) & ~7ull; op_hdr h = {op, (uint32_t)z}; memcpy(c->p + c->n, &h, 8); if (n) memcpy(c->p + c->n + 8, a, n); c->n += z; }
static tex_t tex(void) { static uint64_t s = 9600; tex_t t = {.serial = s++}; t.d.width = t.d.height = 8; t.d.depth = t.d.samples = 1; t.d.levels = 2; t.d.target = NVN_TEXTURE_TARGET_2D; t.d.format = NVN_FORMAT_RGBA8; return t; }
static int view(const tex_t *t, int lv) { static int id = 1600; view_t v = {.set_levels = 1, .base_level = lv, .nlevels = 1}; mtl_reg_tex(id, t, &v); return id++; }
static void set_rt(chain *c, const tex_t *a, int lv) {
    p_set_rt r = {.n = 1}; r.color[0] = a; r.has_cview[0] = 1; r.cview[0].set_levels = 1; r.cview[0].base_level = lv; r.cview[0].nlevels = 1; emit(c, OP_SET_RT, &r, sizeof r);
    int w = 8 >> lv; p_viewports v = {.n = 1}; v.v[0][2] = v.v[0][3] = w; emit(c, OP_VIEWPORTS, &v, sizeof v); p_scissor sc = {0, 0, w, w}; emit(c, OP_SCISSOR, &sc, sizeof sc);
}
static void clear_color(chain *c, float x) { p_clear_color k = {.index = 0, .mask = 15, .rgba = {x, x, x, 1}}; emit(c, OP_CLEAR_COLOR, &k, sizeof k); }
static void bind_prog(chain *c, prog_t *p) { p_bind_program b = {p, 3}; emit(c, OP_BIND_PROGRAM, &b, sizeof b); }
static void bind_tex(chain *c, int id) { p_bind_handles b = {.stage = 1, .first = 0, .n = 1}; b.h[0] = id; emit(c, OP_BIND_TEX, &b, sizeof b); }
static void draw_cmd(chain *c) { p_draw d = {NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3}; emit(c, OP_DRAW, &d, sizeof d); }
static void ds_off(chain *c) { ds_st d = {0, 0, NVN_DEPTH_FUNC_ALWAYS, 0, NVN_STENCIL_FUNC_ALWAYS, NVN_STENCIL_FUNC_ALWAYS, NVN_STENCIL_OP_KEEP, NVN_STENCIL_OP_KEEP, NVN_STENCIL_OP_KEEP, NVN_STENCIL_OP_KEEP, NVN_STENCIL_OP_KEEP, NVN_STENCIL_OP_KEEP};
    p_bind_state x = {.kind = 5}; memcpy(x.bytes, &d, sizeof d); emit(c, OP_BIND_STATE, &x, sizeof x); }
static prog_t g_pB, g_pC, g_pAdd, g_pDecal, g_pDecalU, g_pDecalR; static NvProg *g_tfbx_keep[6];
static void setup(void) {
    NSError *e = nil;
    id<MTLLibrary> l = [g_dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\n"
        "vertex float4 v(uint n [[vertex_id]]) { float2 p[3] = {float2(-1,-1), float2(3,-1), float2(-1,3)}; return float4(p[n], 0, 1); }\n"
        "fragment float4 cB() { return float4(0.5, 0.25, 0.125, 1); }\n"
        "fragment float4 cC() { return float4(0.2, 0.4, 0.6, 1); }\n"
        "struct A { texture2d<float> t [[id(0)]]; };\n"
        "fragment float4 add(float4 pos [[position]], constant A &a [[buffer(0)]]) { return float4(a.t.read(uint2(pos.xy)).rgb + 0.25, 1); }\n" options:nil error:&e];
    if (!l) { printf("%s\n", e.localizedDescription.UTF8String); abort(); }
    prog_t *pg[3] = {&g_pB, &g_pC, &g_pAdd}; NSString *fn[3] = {@"cB", @"cC", @"add"};
    for (int i = 0; i < 3; i++) {
        NvProg *p = [NvProg new]; p->sh[0] = [NvShader new]; p->sh[0]->fn = [l newFunctionWithName:@"v"];
        p->sh[1] = [NvShader new]; p->sh[1]->stage = 1; p->sh[1]->fn = [l newFunctionWithName:fn[i]];
        if (i == 2) { p->sh[1]->rf.nb = 1; p->sh[1]->rf.b[0] = (nvn_rbind){.kind = NVN_RB_TEX, .reg = 0, .dtt = 2}; p->sh[1]->ae = [p->sh[1]->fn newArgumentEncoderWithBufferIndex:0]; }
        p->sh[1]->rf.color_mask = 1; p->sh[1]->rf.color_known = 1;   // (colour 0 only: a second target keeps its pixels)
        pg[i]->serial = 9700 + i; serial_put(pg[i]->serial, p); g_tfbx_keep[i] = p;
    }
    // The decal: the original version and its readable-tile one (function constants FX_0 = 0, FB_k = 32 + k, OM_0_k = 64 + k as dmsh_fetch.py numbers them)
    NSMutableString *fc = [NSMutableString stringWithString:@"#include <metal_stdlib>\nusing namespace metal;\nconstant int FX_0 [[function_constant(0)]];\n"];
    for (int k = 0; k < 8; k++) [fc appendFormat:@"constant bool FB_%d [[function_constant(%d)]];\nconstant bool OM_0_%d [[function_constant(%d)]];\n", k, 32 + k, k, 64 + k];
    NSMutableString *outs = [NSMutableString string], *ins = [NSMutableString string], *sel = [NSMutableString string], *put = [NSMutableString string];
    for (int k = 0; k < 8; k++) { [outs appendFormat:@"    float4 o_at%d [[color(%d), function_constant(OM_0_%d)]];\n", k, k, k]; [ins appendFormat:@", float4 fb%d [[color(%d), function_constant(FB_%d)]]", k, k, k];
        [sel appendFormat:@"FX_0 == %d ? fb%d : ", k, k]; [put appendFormat:@"    if (OM_0_%d) o.o_at%d = r;\n", k, k]; }
    NSString *common = @"struct A { texture2d<float> t [[id(0)]]; };\nstruct DI { float4 m [[user(locn1)]]; };\nstruct VO { float4 p [[position]]; float4 m [[user(locn1)]]; };\n"
        "vertex VO vproj(uint n [[vertex_id]]) { float2 q[3] = {float2(-1,-1), float2(3,-1), float2(-1,3)}; float w = 2.0; float4 p = float4(q[n] * w, 0, w);\n"
        "    VO o; o.p = p; o.m = float4((p.w + p.x) * 0.5, (p.w - p.y) * 0.5, 0, p.w); return o; }\n";
    NSString *orig = [NSString stringWithFormat:@"#include <metal_stdlib>\nusing namespace metal;\n%@fragment float4 decal(DI in [[stage_in]], constant A &a [[buffer(0)]]) {\n"
        "    constexpr sampler s(filter::nearest); float2 uv = in.m.xy * (1.0 / in.m.w); return float4(a.t.sample(s, uv).rgb + 0.25, 1); }\n"
        "fragment float4 decalr(DI in [[stage_in]], constant A &a [[buffer(0)]]) {\n"
        "    constexpr sampler s(filter::nearest); float2 uv = in.m.xy * (1.0 / in.m.w); return a.t.sample(s, uv); }\n", common];
    NSString *bf = [NSString stringWithFormat:@"%@%@struct DO {\n%@};\nfragment DO main0(DI in [[stage_in]], constant A &a [[buffer(0)]]%@) {\n"
        "    constexpr sampler s(filter::nearest); float2 uv = in.m.xy * (1.0 / in.m.w); float4 v = (%@a.t.sample(s, uv)); float4 r = float4(v.rgb + 0.25, 1);\n"
        "    DO o;\n%@    return o; }\n", fc, common, outs, ins, sel, put];
    id<MTLLibrary> lo = [g_dev newLibraryWithSource:orig options:nil error:&e]; if (!lo) { printf("%s\n", e.localizedDescription.UTF8String); abort(); }
    id<MTLLibrary> lb = [g_dev newLibraryWithSource:bf options:nil error:&e]; if (!lb) { printf("%s\n", e.localizedDescription.UTF8String); abort(); }
    NSString *bfr = [bf stringByReplacingOccurrencesOfString:@"float4 r = float4(v.rgb + 0.25, 1);" withString:@"float4 r = v;"];
    id<MTLLibrary> lbr = [g_dev newLibraryWithSource:bfr options:nil error:&e]; if (!lbr) { printf("%s\n", e.localizedDescription.UTF8String); abort(); }
    if (!g_bf_fn_cache) { g_bf_fn_cache = [NSMutableDictionary dictionary]; g_bf_lib_cache = [NSMutableDictionary dictionary]; }
    prog_t *dp[3] = {&g_pDecal, &g_pDecalU, &g_pDecalR};
    for (int i = 0; i < 3; i++) {
        NvProg *p = [NvProg new]; p->sh[0] = [NvShader new]; p->sh[0]->fn = [lo newFunctionWithName:@"vproj"]; p->sh[0]->rf.scr_locn = i == 1 ? 0 : 1u << 1;
        NvShader *f = p->sh[1] = [NvShader new]; f->stage = 1; f->fn = [lo newFunctionWithName:i == 2 ? @"decalr" : @"decal"]; snprintf(f->key, sizeof f->key, "tdecal%d_b", i);
        f->rf.nb = 1; f->rf.b[0] = (nvn_rbind){.kind = NVN_RB_TEX, .reg = 0, .dtt = 2}; f->ae = [f->fn newArgumentEncoderWithBufferIndex:0];
        f->rf.nfc = 1; f->rf.fc_reg[0] = 0; f->rf.fc_locn[0] = 1; f->rf.fc_proj = 1; f->rf.nfo = 1; f->rf.fo_color[0] = 0; f->rf.color_mask = 1; f->rf.color_known = 1;
        g_bf_lib_cache[[NSString stringWithFormat:@"tdecal%d", i]] = i == 2 ? lbr : lb;
        dp[i]->serial = 9703 + i; serial_put(dp[i]->serial, p); g_tfbx_keep[3 + i] = p;
    }
}
// Runs the chain, returns pixel (3,3) of the level as 0xRRGGBB
static uint32_t run(chain *c, const tex_t *t, int lv) {
    emit(c, OP_END, NULL, 0); state_reset(); memset(&R, 0, sizeof R);
    mtl_sub_begin(); uint64_t hh = (uint64_t)c->p; mtl_sub_index(&hh, 1); nvn_exec(c->p);
    int w = 8 >> lv; id<MTLBuffer> b = [g_dev newBufferWithLength:256 options:MTLResourceStorageModeShared];
    [blit() copyFromTexture:tex_mtl(t) sourceSlice:0 sourceLevel:(NSUInteger)lv sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(w, w, 1) toBuffer:b destinationOffset:0 destinationBytesPerRow:(NSUInteger)w * 4 destinationBytesPerImage:(NSUInteger)w * w * 4];
    if (!mtl_wait(mtl_sub_commit(), 5000000000ull) || mtl_last_commit_error()) { printf("GPU did not complete\n"); abort(); }
    const uint8_t *px = (const uint8_t *)b.contents + (3 * w + 3) * 4; return (uint32_t)px[0] << 16 | (uint32_t)px[1] << 8 | px[2];
}
static int near(uint32_t got, float r, float g, float b) {   // within 2 of each 8-bit channel
    int want[3] = {(int)lroundf(fminf(r, 1) * 255), (int)lroundf(fminf(g, 1) * 255), (int)lroundf(fminf(b, 1) * 255)};
    for (int k = 0; k < 3; k++) if (abs((int)(got >> (16 - 8 * k) & 255) - want[k]) > 2) return 0;
    return 1;
}
static uint64_t copies(void) { uint64_t s[3]; mtl_fbx_stats(s); return s[1]; }
static uint64_t splits(void) { uint64_t s[2]; mtl_fbx_split_stats(s); return s[0] + s[1]; }
static uint64_t fetches(void) { uint64_t s[1]; mtl_fbx_fetch_stats(s); return s[0]; }

static void barrier(chain *c) { int32_t b = NVN_BARRIER_ORDER_FRAGMENTS_BIT | NVN_BARRIER_INVALIDATE_TEXTURE_BIT; emit(c, OP_BARRIER, &b, 8); }
static void begin_b(chain *c, tex_t *T) { set_rt(c, T, 0); clear_color(c, 0.1f); ds_off(c); bind_prog(c, &g_pB); draw_cmd(c); }
static void reading(chain *c, int v0) { bind_prog(c, &g_pAdd); bind_tex(c, v0); draw_cmd(c); }

int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); mtl_init(); setup();
    const char *m = getenv("DM_NVN_FBX"); int all = m && !strcmp(m, "all"), split = m && !strcmp(m, "split");
    printf("attachment copies: %s\n", !m ? "after a texture barrier (default)" : m);
    if (split) {
        { tex_t T = tex(); int v0 = view(&T, 0); chain c = chain_new(); uint64_t s0 = splits(), c0 = copies(); begin_b(&c, &T); reading(&c, v0);
          uint32_t px = run(&c, &T, 0); printf("     sa: %06x\n", px);
          EXPECT("sa  (split) the reading draw after B in its pass sees B (B + 0.25)", 1, near(px, 0.75f, 0.5f, 0.375f));
          EXPECT("sa  ... one split, no copy", 1, (splits() - s0) * 10 + copies() - c0 == 10); }
        { tex_t T = tex(); int v0 = view(&T, 0); chain c = chain_new(); uint64_t s0 = splits(); begin_b(&c, &T); reading(&c, v0); reading(&c, v0);
          uint32_t px = run(&c, &T, 0); printf("     sc: %06x\n", px);
          EXPECT("sc  (split) two reading draws in a row share one split: B + 0.25", 1, near(px, 0.75f, 0.5f, 0.375f));
          EXPECT("sc  ... one split", 1, splits() - s0); }
        { tex_t T = tex(); int v0 = view(&T, 0); chain c = chain_new(); uint64_t s0 = splits(); begin_b(&c, &T); reading(&c, v0); bind_prog(&c, &g_pC); draw_cmd(&c); reading(&c, v0);
          uint32_t px = run(&c, &T, 0); printf("     sd: %06x\n", px);
          EXPECT("sd  (split) B, reading, C, reading: the second reads C (C + 0.25)", 1, near(px, 0.45f, 0.65f, 0.85f));
          EXPECT("sd  ... two splits", 2, splits() - s0); }
        { tex_t T = tex(), U = tex(); int v0 = view(&T, 0); chain c = chain_new(); uint64_t s0 = splits();
          set_rt(&c, &T, 0); clear_color(&c, 0); ds_off(&c); bind_prog(&c, &g_pC); draw_cmd(&c);
          set_rt(&c, &U, 0); clear_color(&c, 0); draw_cmd(&c);
          set_rt(&c, &T, 0); reading(&c, v0);
          uint32_t px = run(&c, &T, 0); printf("     sb: %06x\n", px);
          EXPECT("sb  (split) the first draw of a pass reads what earlier passes left (C + 0.25)", 1, near(px, 0.45f, 0.65f, 0.85f));
          EXPECT("sb  ... no split", 0, splits() - s0); }
        { tex_t T = tex(), U = tex(); int v0 = view(&T, 0); chain c = chain_new(); uint64_t s0 = splits();
          set_rt(&c, &T, 0); clear_color(&c, 0); ds_off(&c); bind_prog(&c, &g_pC); draw_cmd(&c);
          set_rt(&c, &U, 0); clear_color(&c, 0); draw_cmd(&c);
          set_rt(&c, &T, 0); clear_color(&c, 0.3f); reading(&c, v0);
          uint32_t px = run(&c, &T, 0); printf("     sf: %06x\n", px);
          EXPECT("sf  (split) a clear pending when the reading draw opens the pass: flushed first (0.3 + 0.25)", 1, near(px, 0.55f, 0.55f, 0.55f));
          EXPECT("sf  ... one flush", 1, splits() - s0); }
        { tex_t T = tex(), U = tex(); int v0 = view(&T, 0); chain c = chain_new(); uint64_t s0 = splits();
          set_rt(&c, &T, 0); clear_color(&c, 0); ds_off(&c); bind_prog(&c, &g_pC); draw_cmd(&c);
          set_rt(&c, &U, 0); clear_color(&c, 0); draw_cmd(&c);
          set_rt(&c, &T, 0); reading(&c, v0); barrier(&c); reading(&c, v0);
          uint32_t px = run(&c, &T, 0); printf("     sg: %06x\n", px);
          EXPECT("sg  (split) reading, barrier, reading: the second sees the first (C + 0.5)", 1, near(px, 0.7f, 0.9f, 1.0f));
          EXPECT("sg  ... one split", 1, splits() - s0); }
    } else if (!all) {
        { tex_t T = tex(); int v0 = view(&T, 0); chain c = chain_new(); uint64_t c0 = copies(); begin_b(&c, &T); barrier(&c); reading(&c, v0);
          uint32_t px = run(&c, &T, 0); printf("     a: %06x\n", px);
          EXPECT("a   after a barrier the reading draw sees the earlier draw (B + 0.25)", 1, near(px, 0.75f, 0.5f, 0.375f));
          EXPECT("a   ... one copy", 1, copies() - c0); }
        { tex_t T = tex(), U = tex(); int v0 = view(&T, 0); chain c = chain_new(); uint64_t c0 = copies();   // (a pass on U between: the two passes on T are not merged)
          set_rt(&c, &T, 0); clear_color(&c, 0); ds_off(&c); bind_prog(&c, &g_pC); draw_cmd(&c);
          set_rt(&c, &U, 0); clear_color(&c, 0); draw_cmd(&c);
          set_rt(&c, &T, 0); bind_prog(&c, &g_pB); draw_cmd(&c); reading(&c, v0);
          uint32_t px = run(&c, &T, 0); printf("     a2: %06x\n", px);
          EXPECT("a2  without a barrier it reads what the pass started with (C + 0.25)", 1, near(px, 0.45f, 0.65f, 0.85f));
          EXPECT("a2  ... no copy", 0, copies() - c0); }
        { tex_t T = tex(); int v0 = view(&T, 0); chain c = chain_new(); uint64_t c0 = copies(); begin_b(&c, &T); barrier(&c); reading(&c, v0); reading(&c, v0);
          uint32_t px = run(&c, &T, 0); printf("     c1: %06x\n", px);
          EXPECT("c1  a second reading draw with no barrier before it: B + 0.25", 1, near(px, 0.75f, 0.5f, 0.375f));
          EXPECT("c1  ... one copy", 1, copies() - c0); }
        { tex_t T = tex(); int v0 = view(&T, 0); chain c = chain_new(); uint64_t c0 = copies(); begin_b(&c, &T); barrier(&c); reading(&c, v0); barrier(&c); reading(&c, v0);
          uint32_t px = run(&c, &T, 0); printf("     c2: %06x\n", px);
          EXPECT("c2  a barrier between two reading draws: B + 0.5", 1, near(px, 1.0f, 0.75f, 0.625f));
          EXPECT("c2  ... two copies", 2, copies() - c0); }
        { tex_t T = tex(); int v0 = view(&T, 0); chain c = chain_new(); uint64_t c0 = copies(), f0 = fetches(); begin_b(&c, &T); barrier(&c); bind_prog(&c, &g_pDecal); bind_tex(&c, v0); draw_cmd(&c);
          uint32_t px = run(&c, &T, 0); printf("     fa: %06x\n", px);
          EXPECT("fa  a decal after a barrier reads B from the tile (B + 0.25)", 1, near(px, 0.75f, 0.5f, 0.375f));
          EXPECT("fa  ... no copy", 0, copies() - c0);
          EXPECT("fa  ... read from the tile once", 1, fetches() - f0); }
        { tex_t T = tex(); int v0 = view(&T, 0); chain c = chain_new(); uint64_t c0 = copies(), f0 = fetches(); begin_b(&c, &T); barrier(&c); bind_prog(&c, &g_pDecal); bind_tex(&c, v0); draw_cmd(&c);
          barrier(&c); draw_cmd(&c);
          uint32_t px = run(&c, &T, 0); printf("     fb: %06x\n", px);
          EXPECT("fb  two decals with barriers: the second sees the first (B + 0.5)", 1, near(px, 1.0f, 0.75f, 0.625f));
          EXPECT("fb  ... no copy, two tile reads", 1, copies() - c0 == 0 && fetches() - f0 == 2); }
        { tex_t T = tex(); int v0 = view(&T, 0); chain c = chain_new(); uint64_t c0 = copies(), f0 = fetches(); begin_b(&c, &T); barrier(&c); bind_prog(&c, &g_pDecalU); bind_tex(&c, v0); draw_cmd(&c);
          uint32_t px = run(&c, &T, 0); printf("     fc: %06x\n", px);
          EXPECT("fc  the vertex side not proven: a copy (B + 0.25)", 1, near(px, 0.75f, 0.5f, 0.375f));
          EXPECT("fc  ... one copy, no tile read", 1, copies() - c0 == 1 && fetches() == f0); }
        { tex_t T = tex(); int v0 = view(&T, 0); chain c = chain_new(); uint64_t c0 = copies(), f0 = fetches(); begin_b(&c, &T); barrier(&c);
          p_viewports v = {.n = 1}; v.v[0][2] = v.v[0][3] = 4; emit(&c, OP_VIEWPORTS, &v, sizeof v); bind_prog(&c, &g_pDecal); bind_tex(&c, v0); draw_cmd(&c);
          run(&c, &T, 0);
          EXPECT("fd  a viewport over part of the target: a copy, no tile read", 1, copies() - c0 == 1 && fetches() == f0); }
        for (int k = 0; k < 2; k++) {   // fr / fs: B in colour 0 and 0.5 in an R32Float colour 1; the decal samples colour 1 through a swizzled view into colour 0
            tex_t T = tex(), U = tex(); U.d.format = NVN_FORMAT_R32F; U.d.levels = 1;
            view_t uv = {.set_levels = 1, .base_level = 0, .nlevels = 1, .set_swz = 1, .swz_r = NVN_TEXTURE_SWIZZLE_R, .swz_g = k ? NVN_TEXTURE_SWIZZLE_R : NVN_TEXTURE_SWIZZLE_ZERO,
                         .swz_b = k ? NVN_TEXTURE_SWIZZLE_R : NVN_TEXTURE_SWIZZLE_ZERO, .swz_a = NVN_TEXTURE_SWIZZLE_ONE};
            static int vid = 1700; int vu = vid++; mtl_reg_tex(vu, &U, &uv);
            chain c = chain_new(); uint64_t c0 = copies(), f0 = fetches();
            p_set_rt r = {.n = 2}; r.color[0] = &T; r.color[1] = &U; for (int q = 0; q < 2; q++) { r.has_cview[q] = 1; r.cview[q].set_levels = 1; r.cview[q].nlevels = 1; }
            emit(&c, OP_SET_RT, &r, sizeof r); p_viewports v = {.n = 1}; v.v[0][2] = v.v[0][3] = 8; emit(&c, OP_VIEWPORTS, &v, sizeof v); p_scissor sc = {0, 0, 8, 8}; emit(&c, OP_SCISSOR, &sc, sizeof sc);
            clear_color(&c, 0.1f); p_clear_color k1 = {.index = 1, .mask = 15, .rgba = {0.5f, 0.5f, 0.5f, 0.5f}}; emit(&c, OP_CLEAR_COLOR, &k1, sizeof k1);
            ds_off(&c); bind_prog(&c, &g_pB); draw_cmd(&c); barrier(&c); bind_prog(&c, &g_pDecalR); bind_tex(&c, vu); draw_cmd(&c);
            uint32_t px = run(&c, &T, 0); printf("     %s: %06x\n", k ? "fs" : "fr", px);
            if (!k) { EXPECT("fr  R32Float through (red, zero, zero, one) read from the tile: (0.5, 0, 0)", 1, near(px, 0.5f, 0, 0));
                      EXPECT("fr  ... no copy, one tile read", 1, copies() - c0 == 0 && fetches() - f0 == 1); }
            else { EXPECT("fs  through (red, red, red, one): a copy samples (0.5, 0.5, 0.5)", 1, near(px, 0.5f, 0.5f, 0.5f));
                   EXPECT("fs  ... one copy, no tile read", 1, copies() - c0 == 1 && fetches() == f0); } }
    } else {
        { tex_t T = tex(); int v0 = view(&T, 0); chain c = chain_new(); uint64_t c0 = copies(); begin_b(&c, &T); reading(&c, v0);
          uint32_t px = run(&c, &T, 0); printf("     a: %06x\n", px);
          EXPECT("a   (all) the reading draw after a draw in its pass sees that draw (B + 0.25)", 1, near(px, 0.75f, 0.5f, 0.375f));
          EXPECT("a   ... one copy", 1, copies() - c0); }
        { tex_t T = tex(); int v0 = view(&T, 0); chain c = chain_new(); uint64_t c0 = copies(); begin_b(&c, &T); reading(&c, v0); reading(&c, v0);
          uint32_t px = run(&c, &T, 0); printf("     c: %06x\n", px);
          EXPECT("c   (all) two reading draws in a row: the second sees the first (B + 0.5)", 1, near(px, 1.0f, 0.75f, 0.625f));
          EXPECT("c   ... two copies", 2, copies() - c0); }
        { tex_t T = tex(); int v0 = view(&T, 0); chain c = chain_new(); uint64_t c0 = copies();
          set_rt(&c, &T, 0); clear_color(&c, 0); ds_off(&c); bind_prog(&c, &g_pC); draw_cmd(&c); set_rt(&c, &T, 0); reading(&c, v0);
          uint32_t px = run(&c, &T, 0); printf("     b: %06x\n", px);
          EXPECT("b   (all) the first draw of a pass reads what earlier passes left (C + 0.25)", 1, near(px, 0.45f, 0.65f, 0.85f));
          EXPECT("b   ... one copy", 1, copies() - c0); }
        { tex_t T = tex(); int v0 = view(&T, 0); chain c = chain_new(); uint64_t c0 = copies(); set_rt(&c, &T, 0); clear_color(&c, 0.3f); ds_off(&c); reading(&c, v0);
          uint32_t px = run(&c, &T, 0); printf("     f: %06x\n", px);
          EXPECT("f   (all) a pending clear: the reading draw sees it (0.3 + 0.25)", 1, near(px, 0.55f, 0.55f, 0.55f));
          EXPECT("f   ... one copy", 1, copies() - c0); }
    }
    { tex_t T = tex(); int v0 = view(&T, 0); chain c = chain_new(); uint64_t c0 = copies();
      set_rt(&c, &T, 0); clear_color(&c, 0); ds_off(&c); bind_prog(&c, &g_pC); draw_cmd(&c); barrier(&c);
      set_rt(&c, &T, 1); reading(&c, v0);
      uint32_t px = run(&c, &T, 1); printf("     e: %06x\n", px);
      EXPECT("e   drawing level 1, sampling level 0: C + 0.25 in level 1", 1, near(px, 0.45f, 0.65f, 0.85f));
      EXPECT("e   ... no copy (the level it reads is not the one it draws)", 0, copies() - c0); }
    { NvShader *f = g_tfbx_keep[3]->sh[1]; int fx0[1] = {0}, fx1[1] = {1};
      uint8_t lm0[8] = {0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}, lm1[8] = {0xff, 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
      id<MTLFunction> a = bf_fn(f, fx0, 1, lm0); uint64_t h0 = g_bf_fast_hits; id<MTLFunction> b = bf_fn(f, fx0, 1, lm0);
      EXPECT("bk  the same inputs again: the same function, from the binary-keyed table", 1, a && a == b && g_bf_fast_hits == h0 + 1);
      id<MTLFunction> m = bf_fn(f, fx0, 1, lm1), k = bf_fn(f, fx0, 3, lm0), x = bf_fn(f, fx1, 1, lm0);
      EXPECT("bk  ... another attachment map: another function", 1, m && m != a);
      EXPECT("bk  ... another colour mask or constant: other functions", 1, k && x && k != a && x != a && k != x); }
    @autoreleasepool { ARENA_OUT id<MTLBuffer> b = nil; arena_alloc(256, &b); CFIndex r0 = CFGetRetainCount((__bridge CFTypeRef)b);
      for (int i = 0; i < 100; i++) arena_alloc(256, &b);
      EXPECT("ar  100 arena allocations: the buffer's retain count as it was (nothing autoreleased)", 0, (long)(CFGetRetainCount((__bridge CFTypeRef)b) - r0)); }
    printf("== %d checks, %d failed ==\n", count, fail); return fail != 0;
} }
