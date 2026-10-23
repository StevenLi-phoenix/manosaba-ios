// Single-draw passes as kernels through the production replay (plan item 217, nvn_mtl.m kp_draw, DM_NVN_KPASS): a pass of one full-screen draw on
// an 8x8 target runs as the kernels and gives the render pass's pixels; a pass of two draws, one with a pending clear, and one while the shaders
// have no kernels draw as passes; a samples-passed count over a kernel pass (8x4 scissor) reports 32 as the render pass does. The kernels are
// gfx/tools/dmsh_kernel.py's of kpass/nvs.metal and kpass/nfs.metal (the Makefile builds t_kpnvn_lib/). Metal 4 only: the older-API build skips.
// Broken variant: -DDM_TEST_KPASS_NO_SCAN (every pass taken as one draw) fails 1.
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define EXPECT(name, want, got) do { uint64_t w = (want), g = (got); count++; printf("%-96s expected %-4llu → got %-4llu %s\n", name, (unsigned long long)w, (unsigned long long)g, w == g ? "✓" : "✗"); fail += w != g; } while (0)
#if !DM_NVN_M3
typedef struct { uint8_t *p; size_t n; } chain;
static chain chain_new(void) { chain c = {0}; if (posix_memalign((void **)&c.p, getpagesize(), 65536)) abort(); memset(c.p, 0, 65536); mtl_pool_add(c.p, 65536); return c; }
static void emit(chain *c, unsigned op, const void *a, size_t n) { size_t z = (8 + n + 7) & ~7ull; op_hdr h = {op, (uint32_t)z}; memcpy(c->p + c->n, &h, 8); if (n) memcpy(c->p + c->n + 8, a, n); c->n += z; }
static tex_t tex(void) { static uint64_t s = 19800; tex_t t = {.serial = s++}; t.d.width = t.d.height = 8; t.d.depth = t.d.samples = t.d.levels = 1; t.d.target = NVN_TEXTURE_TARGET_2D; t.d.format = NVN_FORMAT_RGBA8; return t; }
static void scissor(chain *c, int x, int y, int w, int h) { p_scissor sc = {x, y, w, h}; emit(c, OP_SCISSOR, &sc, sizeof sc); }
static void set_rt(chain *c, const tex_t *a) { p_set_rt r = {.n = 1}; r.color[0] = a; emit(c, OP_SET_RT, &r, sizeof r);
    p_viewports v = {.n = 1}; v.v[0][2] = 8; v.v[0][3] = 8; v.v[0][5] = 1; emit(c, OP_VIEWPORTS, &v, sizeof v); scissor(c, 0, 0, 8, 8); }
static void draw_cmd(chain *c) { p_draw d = {NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3}; emit(c, OP_DRAW, &d, sizeof d); }
static prog_t g_kp_p = {.serial = 19900}, g_kp_q = {.serial = 19901}; static NvProg *g_kp_keep[2];
static NvShader *stage(const char *path, int st, const char *key) {
    NSError *e = nil; NSString *src = [NSString stringWithContentsOfFile:@(path) encoding:NSUTF8StringEncoding error:&e];
    id<MTLLibrary> l = src ? [g_dev newLibraryWithSource:src options:nil error:&e] : nil;
    if (!l) { printf("[red] %s: %s\n", path, e.localizedDescription.UTF8String); exit(1); }
    NvShader *s = [NvShader new]; s->fn = [l newFunctionWithName:@"main0"]; s->stage = st; strlcpy(s->key, key, sizeof s->key);
    if (st == 1) { s->rf.color_known = 1; s->rf.color_mask = 1; }
    return s;
}
static void setup(void) {
    g_msl_dir = @"t_kpnvn_lib";
    NvProg *p = [NvProg new]; p->sh[0] = stage("kpass/nvs.metal", 0, "kpnvs_b"); p->sh[1] = stage("kpass/nfs.metal", 1, "kpnfs_b"); serial_put(g_kp_p.serial, p); g_kp_keep[0] = p;
    NvProg *q = [NvProg new]; q->sh[0] = stage("kpass/nvs.metal", 0, "nokernel_b"); q->sh[1] = stage("kpass/nfs.metal", 1, "nokernel2_b"); serial_put(g_kp_q.serial, q); g_kp_keep[1] = q;
}
static void readback(const tex_t *t, uint8_t out[256]) {
    id<MTLBuffer> b = [g_dev newBufferWithLength:256 options:MTLResourceStorageModeShared];
    [blit() copyFromTexture:tex_mtl(t) sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(8, 8, 1) toBuffer:b destinationOffset:0 destinationBytesPerRow:32 destinationBytesPerImage:256];
    if (!mtl_wait(mtl_sub_commit(), 5000000000ull) || mtl_last_commit_error()) { printf("GPU did not complete\n"); abort(); }
    memcpy(out, b.contents, 256);
}
// case: 0 one draw, 1 two draws, 2 a clear then one draw, 3 one draw without kernels, 4 one draw counted under an 8x4 scissor (rep: the report)
static void run(int kase, int on, uint8_t px[256], NVNcounterData *rep) {
    tex_t T = tex(); chain c = chain_new(); set_rt(&c, &T);
    p_bind_program b = {kase == 3 ? &g_kp_q : &g_kp_p, 3}; emit(&c, OP_BIND_PROGRAM, &b, sizeof b);
    if (kase == 2) { p_clear_color k = {.index = 0, .mask = 15, .rgba = {0.2f, 0.4f, 0.6f, 1}}; emit(&c, OP_CLEAR_COLOR, &k, sizeof k); }
    if (kase == 4) { uint64_t t = NVN_COUNTER_TYPE_SAMPLES_PASSED; emit(&c, OP_RESET_COUNTER, &t, sizeof t); scissor(&c, 0, 0, 8, 4); }
    draw_cmd(&c);
    if (kase == 1) { scissor(&c, 2, 2, 3, 3); draw_cmd(&c); }
    if (kase == 4) { uint64_t r[2] = {NVN_COUNTER_TYPE_SAMPLES_PASSED, (uint64_t)(uintptr_t)rep}; emit(&c, OP_REPORT_COUNTER, r, sizeof r); }
    emit(&c, OP_END, NULL, 0); state_reset(); memset(&R, 0, sizeof R); vis_clear(); g_vis_on = g_vis_counting = 0;
    g_kp_on = on;
    mtl_sub_begin(); uint64_t hh = (uint64_t)c.p; mtl_sub_index(&hh, 1); mtl_exec(c.p); readback(&T, px);
}
#endif
int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0);
#if DM_NVN_M3
    printf("older-API build: single-draw passes as kernels are Metal 4 only\n== 0 checks, 0 failed ==\n"); return 0;
#else
    setenv("DM_NVN_KPASS", "1", 1); mtl_init(); setup();
    static const char *names[5] = {"one draw", "two draws", "a clear, then one draw", "one draw, no kernels", "one draw counted"};
    static const int taken[5] = {1, 0, 0, 0, 1};
    for (int k = 0; k < 5; k++) {
        uint8_t a[256], b[256]; NVNcounterData ra = {0}, rb = {0};
        uint64_t n0 = g_kp_n; run(k, 1, a, &ra); uint64_t n1 = g_kp_n; run(k, 0, b, &rb);
        char what[160];
        snprintf(what, sizeof what, "%s: %s [-DDM_TEST_KPASS_NO_SCAN: two draws taken]", names[k], taken[k] ? "taken as kernels" : "drawn as a pass"); EXPECT(what, taken[k] ? 1 : 0, n1 - n0);
        snprintf(what, sizeof what, "%s: the pixels of the render pass", names[k]); EXPECT(what, 0, (uint64_t)memcmp(a, b, 256));
        if (k == 4) { EXPECT("one draw counted: the samples passed under an 8x4 scissor, kernels", 32, ra.counter); EXPECT("... render pass", 32, rb.counter); }
    }
    printf("== %d checks, %d failed ==\n", count, fail); return fail != 0;
#endif
} }
