// A clear drawn as a quad (a scissored or masked clear: nvn_mtl.m clear_quad) writes its attachment like a draw (plan item 62j). With the TBDR
// rewrites on, a pass that loads its attachment and writes it only with such a clear used to store DontCare (unwritten-DontCare), losing the clear:
// Tears of the Kingdom's prologue glowed over half the picture and Zelda's torch left a trail.
//   q1 T drawn C, then a pass on T with only a scissored clear (0.9 in pixels 0..3): inside 0.9, outside still C
//   q2 the same with a draw on another attachment of the pass after the clear: T keeps the clear
// -DDM_TEST_CLEARQUAD_UNNOTED (the clear not noted): q1's inside check fails (1); q2 holds either way (the draw's channel mask marks
// attachment 1 written)
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define EXPECT(name, want, got) do { uint64_t w = (want), g = (got); count++; printf("%-86s expected %-4llu → got %-4llu %s\n", name, (unsigned long long)w, (unsigned long long)g, w == g ? "✓" : "✗"); fail += w != g; } while (0)
typedef struct { uint8_t *p; size_t n; } chain;
static chain chain_new(void) { chain c = {0}; if (posix_memalign((void **)&c.p, getpagesize(), 65536)) abort(); memset(c.p, 0, 65536); mtl_pool_add(c.p, 65536); return c; }
static void emit(chain *c, unsigned op, const void *a, size_t n) { size_t z = (8 + n + 7) & ~7ull; op_hdr h = {op, (uint32_t)z}; memcpy(c->p + c->n, &h, 8); if (n) memcpy(c->p + c->n + 8, a, n); c->n += z; }
static tex_t tex(void) { static uint64_t s = 9850; tex_t t = {.serial = s++}; t.d.width = t.d.height = 8; t.d.depth = t.d.samples = 1; t.d.levels = 2; t.d.target = NVN_TEXTURE_TARGET_2D; t.d.format = NVN_FORMAT_RGBA8; return t; }
static int view(const tex_t *t, int lv) { static int id = 1850; view_t v = {.set_levels = 1, .base_level = lv, .nlevels = 1}; mtl_reg_tex(id, t, &v); return id++; }
static void set_rt(chain *c, const tex_t *a, int lv) {
    p_set_rt r = {.n = 1}; r.color[0] = a; r.has_cview[0] = 1; r.cview[0].set_levels = 1; r.cview[0].base_level = lv; r.cview[0].nlevels = 1; emit(c, OP_SET_RT, &r, sizeof r);
    int w = 8 >> lv; p_viewports v = {.n = 1}; v.v[0][2] = v.v[0][3] = w; emit(c, OP_VIEWPORTS, &v, sizeof v); p_scissor sc = {0, 0, w, w}; emit(c, OP_SCISSOR, &sc, sizeof sc);
}
static void set_rt2(chain *c, const tex_t *a, const tex_t *b) {   // two colour targets, level 0
    p_set_rt r = {.n = 2}; r.color[0] = a; r.color[1] = b;
    for (int k = 0; k < 2; k++) { r.has_cview[k] = 1; r.cview[k].set_levels = 1; r.cview[k].base_level = 0; r.cview[k].nlevels = 1; }
    emit(c, OP_SET_RT, &r, sizeof r); p_viewports v = {.n = 1}; v.v[0][2] = v.v[0][3] = 8; emit(c, OP_VIEWPORTS, &v, sizeof v); p_scissor sc = {0, 0, 8, 8}; emit(c, OP_SCISSOR, &sc, sizeof sc);
}
static void corner(chain *c) { p_scissor sc = {0, 0, 2, 2}; emit(c, OP_SCISSOR, &sc, sizeof sc); }   // draws reach only pixels (0..1, 0..1)
static void clear_color(chain *c, float x) { p_clear_color k = {.index = 0, .mask = 15, .rgba = {x, x, x, 1}}; emit(c, OP_CLEAR_COLOR, &k, sizeof k); }
static void bind_prog(chain *c, prog_t *p) { p_bind_program b = {p, 3}; emit(c, OP_BIND_PROGRAM, &b, sizeof b); }
static void bind_tex(chain *c, int id) { p_bind_handles b = {.stage = 1, .first = 0, .n = 1}; b.h[0] = id; emit(c, OP_BIND_TEX, &b, sizeof b); }
static void draw_cmd(chain *c) { p_draw d = {NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3}; emit(c, OP_DRAW, &d, sizeof d); }
static void ds_off(chain *c) { ds_st d = {0, 0, NVN_DEPTH_FUNC_ALWAYS, 0, NVN_STENCIL_FUNC_ALWAYS, NVN_STENCIL_FUNC_ALWAYS, NVN_STENCIL_OP_KEEP, NVN_STENCIL_OP_KEEP, NVN_STENCIL_OP_KEEP, NVN_STENCIL_OP_KEEP, NVN_STENCIL_OP_KEEP, NVN_STENCIL_OP_KEEP};
    p_bind_state x = {.kind = 5}; memcpy(x.bytes, &d, sizeof d); emit(c, OP_BIND_STATE, &x, sizeof x); }
static prog_t g_pC, g_pAdd; static NvProg *g_tcq_keep[2];
static void setup(void) {
    NSError *e = nil;
    id<MTLLibrary> l = [g_dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\n"
        "vertex float4 v(uint n [[vertex_id]]) { float2 p[3] = {float2(-1,-1), float2(3,-1), float2(-1,3)}; return float4(p[n], 0, 1); }\n"
        "fragment float4 cC() { return float4(0.2, 0.4, 0.6, 1); }\n"
        "struct A { texture2d<float> t [[id(0)]]; };\n"
        "fragment float4 add(float4 pos [[position]], constant A &a [[buffer(0)]]) { return float4(a.t.read(uint2(pos.xy)).rgb + 0.25, 1); }\n" options:nil error:&e];
    if (!l) { printf("%s\n", e.localizedDescription.UTF8String); abort(); }
    prog_t *pg[2] = {&g_pC, &g_pAdd}; NSString *fn[2] = {@"cC", @"add"};
    for (int i = 0; i < 2; i++) {
        NvProg *p = [NvProg new]; p->sh[0] = [NvShader new]; p->sh[0]->fn = [l newFunctionWithName:@"v"];
        NvShader *f = p->sh[1] = [NvShader new]; f->stage = 1; f->fn = [l newFunctionWithName:fn[i]]; snprintf(f->key, sizeof f->key, "tcqd%d_b", i);
        if (i == 1) { f->rf.nb = 1; f->rf.b[0] = (nvn_rbind){.kind = NVN_RB_TEX, .reg = 0, .dtt = 2}; f->ae = [f->fn newArgumentEncoderWithBufferIndex:0]; }
        f->rf.color_mask = 1; f->rf.color_known = 1;   // (colour 0 only: a second target keeps its pixels)
        pg[i]->serial = 9840 + i; serial_put(pg[i]->serial, p); g_tcq_keep[i] = p;
    }
}
// Runs the chain, then pixel (3,3) of each texture's level as 0xRRGGBB
static void run(chain *c, int n, const tex_t *const *t, const int *lv, uint32_t *out) {
    emit(c, OP_END, NULL, 0); state_reset(); memset(&R, 0, sizeof R);
    mtl_sub_begin(); uint64_t hh = (uint64_t)c->p; mtl_sub_index(&hh, 1); mtl_exec(c->p);
    id<MTLBuffer> b[4];
    for (int i = 0; i < n; i++) { int w = 8 >> lv[i]; b[i] = [g_dev newBufferWithLength:256 options:MTLResourceStorageModeShared];
        [blit() copyFromTexture:tex_mtl(t[i]) sourceSlice:0 sourceLevel:(NSUInteger)lv[i] sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(w, w, 1) toBuffer:b[i] destinationOffset:0
            destinationBytesPerRow:(NSUInteger)w * 4 destinationBytesPerImage:(NSUInteger)w * w * 4]; }
    if (!mtl_wait(mtl_sub_commit(), 5000000000ull) || mtl_last_commit_error()) { printf("GPU did not complete\n"); abort(); }
    for (int i = 0; i < n; i++) { int w = 8 >> lv[i]; const uint8_t *px = (const uint8_t *)b[i].contents + (3 * w + 3) * 4; out[i] = (uint32_t)px[0] << 16 | (uint32_t)px[1] << 8 | px[2]; }
}
static int near(uint32_t got, float r, float g, float b) {   // within 2 of each 8-bit channel
    int want[3] = {(int)lroundf(fminf(r, 1) * 255), (int)lroundf(fminf(g, 1) * 255), (int)lroundf(fminf(b, 1) * 255)};
    for (int k = 0; k < 3; k++) if (abs((int)(got >> (16 - 8 * k) & 255) - want[k]) > 2) return 0;
    return 1;
}

static void scissor(chain *c, int x, int y, int w, int h) { p_scissor sc = {x, y, w, h}; emit(c, OP_SCISSOR, &sc, sizeof sc); }
int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); mtl_init(); setup();
    { tex_t T = tex(), U = tex(); chain c = chain_new();
      set_rt(&c, &T, 0); clear_color(&c, 0.3f); ds_off(&c); bind_prog(&c, &g_pC); draw_cmd(&c);
      set_rt(&c, &U, 0); clear_color(&c, 0.6f); draw_cmd(&c);
      set_rt(&c, &T, 0); scissor(&c, 0, 0, 4, 4); clear_color(&c, 0.9f);
      set_rt(&c, &U, 0); draw_cmd(&c);
      const tex_t *ts[1] = {&T}; int lv[1] = {0}; uint32_t in[1], out[1];
      emit(&c, OP_END, NULL, 0); state_reset(); memset(&R, 0, sizeof R);
      mtl_sub_begin(); uint64_t hh = (uint64_t)c.p; mtl_sub_index(&hh, 1); mtl_exec(c.p);
      id<MTLBuffer> b = [g_dev newBufferWithLength:256 options:MTLResourceStorageModeShared];
      [blit() copyFromTexture:tex_mtl(ts[0]) sourceSlice:0 sourceLevel:(NSUInteger)lv[0] sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(8, 8, 1) toBuffer:b destinationOffset:0 destinationBytesPerRow:32 destinationBytesPerImage:256];
      if (!mtl_wait(mtl_sub_commit(), 5000000000ull) || mtl_last_commit_error()) { printf("GPU did not complete\n"); abort(); }
      const uint8_t *p = b.contents; in[0] = (uint32_t)p[(1 * 8 + 1) * 4] << 16 | (uint32_t)p[(1 * 8 + 1) * 4 + 1] << 8 | p[(1 * 8 + 1) * 4 + 2];
      out[0] = (uint32_t)p[(6 * 8 + 6) * 4] << 16 | (uint32_t)p[(6 * 8 + 6) * 4 + 1] << 8 | p[(6 * 8 + 6) * 4 + 2];
      printf("     q1: in %06x out %06x\n", in[0], out[0]);
      EXPECT("q1  a pass with only a scissored clear: inside the clear (0.9)", 1, near(in[0], 0.9f, 0.9f, 0.9f));
      EXPECT("q1  ... outside what the earlier draw left (C)", 1, near(out[0], 0.2f, 0.4f, 0.6f)); }
    { tex_t T = tex(), U = tex(); chain c = chain_new();
      set_rt(&c, &T, 0); clear_color(&c, 0.3f); ds_off(&c); bind_prog(&c, &g_pC); draw_cmd(&c);
      set_rt(&c, &U, 0); clear_color(&c, 0.6f); draw_cmd(&c);
      set_rt2(&c, &U, &T); scissor(&c, 0, 0, 4, 4); p_clear_color k = {.index = 1, .mask = 15, .rgba = {0.9f, 0.9f, 0.9f, 1}}; emit(&c, OP_CLEAR_COLOR, &k, sizeof k);
      scissor(&c, 0, 0, 8, 8); draw_cmd(&c);   // writes colour 0 (U) only
      set_rt(&c, &U, 0); draw_cmd(&c);
      const tex_t *ts[1] = {&T}; int lv[1] = {0}; uint32_t px[1]; run(&c, 1, ts, lv, px);
      uint32_t in = px[0]; printf("     q2: %06x\n", in);
      EXPECT("q2  a scissored clear of attachment 1, a draw writing attachment 0: attachment 1 keeps the clear (0.9)", 1, near(in, 0.9f, 0.9f, 0.9f)); }
    printf("== %d checks, %d failed ==\n", count, fail); return fail != 0;
} }
