// Vertex streams bound once per encoder (nvn_mtl.m draw, g_es.vb; plan item 225): in one pass four draws read their colour from vertex stream 0
// (buffer index 16), bound to X (red), Y (green), X, X: the third binds X again, the fourth reuses it; a second pass (a new encoder) binds X again.
// Negative control -DDM_TEST_VB_ELIDE_ANY (a stream bound on the encoder never set again, whatever its address): the second quarter reads X, 3 fail.
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define EXPECT(name, want, got) do { long long w=(want), g=(got); count++; printf("%-74s expected %-10llx → got %-10llx %s\n", name, (unsigned long long)w, (unsigned long long)g, w==g?"✓":"✗"); fail += w != g; } while (0)
typedef struct { uint8_t *p; size_t n; } chain;
static void emit(chain *c, unsigned op, const void *a, size_t n) { size_t z=(8+n+7)&~7ull; op_hdr h={op,(uint32_t)z}; memcpy(c->p+c->n,&h,8); if(n) memcpy(c->p+c->n+8,a,n); c->n+=z; }
static uint32_t px(id<MTLBuffer> b, int x) { uint32_t v; memcpy(&v, (uint8_t *)b.contents + 4 * 32 + x * 4, 4); return v; }
int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); mtl_init();
    NSError *e = nil; id<MTLLibrary> l = [g_dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\n"
        "struct V { float4 p [[position]]; float4 c; };\n"
        "vertex V v(uint n [[vertex_id]], const device float4 *vb [[buffer(16)]]) { float2 p[3] = {float2(-1,-1), float2(3,-1), float2(-1,3)}; V o; o.p = float4(p[n], 0, 1); o.c = vb[0]; return o; }\n"
        "fragment float4 f(V i [[stage_in]]) { return i.c; }\n" options:nil error:&e];
    if (!l) { printf("%s\n", e.localizedDescription.UTF8String); return 1; }
    static prog_t pg = {.serial = 8960}; NvProg *p = [NvProg new]; p->sh[0] = [NvShader new]; p->sh[0]->fn = [l newFunctionWithName:@"v"];
    p->sh[1] = [NvShader new]; p->sh[1]->stage = 1; p->sh[1]->fn = [l newFunctionWithName:@"f"]; serial_put(pg.serial, p);
    uint8_t *cm; if (posix_memalign((void **)&cm, getpagesize(), 1 << 20)) abort(); memset(cm, 0, 1 << 20); mtl_pool_add(cm, 1 << 20); chain c = {cm, 0};
    const float red[4] = {1, 0, 0, 1}, green[4] = {0, 1, 0, 1};
    uint64_t X = (uint64_t)(uintptr_t)(cm + 0x80000), Y = (uint64_t)(uintptr_t)(cm + 0x80100); memcpy(cm + 0x80000, red, 16); memcpy(cm + 0x80100, green, 16);
    tex_t a = {.serial = 9860}, b = {.serial = 9861};
    for (tex_t *t = &a; t; t = t == &a ? &b : NULL) { t->d.width = t->d.height = 8; t->d.depth = t->d.samples = t->d.levels = 1; t->d.target = NVN_TEXTURE_TARGET_2D; t->d.format = NVN_FORMAT_RGBA8; }
    p_bind_program bp = {&pg, 3}; emit(&c, OP_BIND_PROGRAM, &bp, sizeof bp);
    ds_st d = {0, 0, NVN_DEPTH_FUNC_ALWAYS, 0, NVN_STENCIL_FUNC_ALWAYS, NVN_STENCIL_FUNC_ALWAYS, 1, 1, 1, 1, 1, 1}; p_bind_state x = {.kind = 5}; memcpy(x.bytes, &d, sizeof d); emit(&c, OP_BIND_STATE, &x, sizeof x);
    const uint64_t bufs[2][4] = {{X, Y, X, X}, {X, 0, 0, 0}};
    for (int k = 0; k < 2; k++) {   // pass 0: four quarters (2 columns each) from X, Y, X, X; pass 1: the whole target from X
        p_set_rt rt = {.n = 1}; rt.color[0] = k ? &b : &a; emit(&c, OP_SET_RT, &rt, sizeof rt);
        p_viewports v = {.n = 1}; v.v[0][2] = v.v[0][3] = 8; emit(&c, OP_VIEWPORTS, &v, sizeof v);
        p_scissor full = {0, 0, 8, 8}; emit(&c, OP_SCISSOR, &full, sizeof full);
        p_clear_color cc = {.index = 0, .mask = 15, .rgba = {0, 0, 0, 0}}; emit(&c, OP_CLEAR_COLOR, &cc, sizeof cc);
        for (int q = 0; q < (k ? 1 : 4); q++) {
            p_bind_vbuf vb = {.first = 0, .n = 1}; vb.r[0].address = bufs[k][q]; vb.r[0].size = 16; emit(&c, OP_BIND_VBUF, &vb, sizeof vb);
            p_scissor sc = {k ? 0 : 2 * q, 0, k ? 8 : 2, 8}; emit(&c, OP_SCISSOR, &sc, sizeof sc);
            p_draw dr = {NVN_DRAW_PRIMITIVE_TRIANGLES, 0, 3}; emit(&c, OP_DRAW, &dr, sizeof dr);
        }
        p_set_rt none = {.n = 0}; emit(&c, OP_SET_RT, &none, sizeof none);
    }
    emit(&c, OP_END, NULL, 0);
    setenv("DM_NVN_TBDR", "0", 1);   // two separate passes
    uint64_t s0, k0; mtl_vb_stats(&s0, &k0);
    state_reset(); memset(&R, 0, sizeof R); mtl_sub_begin(); uint64_t hh = (uint64_t)c.p; mtl_sub_index(&hh, 1); nvn_exec(c.p);
    id<MTLBuffer> ra = [g_dev newBufferWithLength:256 options:MTLResourceStorageModeShared], rb = [g_dev newBufferWithLength:256 options:MTLResourceStorageModeShared]; res_add(ra); res_add(rb);
    [blit() copyFromTexture:tex_mtl(&a) sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(8,8,1) toBuffer:ra destinationOffset:0 destinationBytesPerRow:32 destinationBytesPerImage:256];
    [blit() copyFromTexture:tex_mtl(&b) sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(8,8,1) toBuffer:rb destinationOffset:0 destinationBytesPerRow:32 destinationBytesPerImage:256];
    mtl_wait(mtl_sub_commit(), 5000000000ull);
    uint64_t s1, k1; mtl_vb_stats(&s1, &k1);
    const uint32_t R_ = 0xff0000ff, G_ = 0xff00ff00;
    EXPECT("pass 0, first quarter: X (red)", R_, px(ra, 0));
    EXPECT("... second quarter: Y (green) [-DDM_TEST_VB_ELIDE_ANY: X, red]", G_, px(ra, 2));
    EXPECT("... third quarter: X bound again (red)", R_, px(ra, 4));
    EXPECT("... fourth quarter: X reused (red)", R_, px(ra, 6));
    EXPECT("pass 1 (a new encoder): X (red)", R_, px(rb, 4));
    EXPECT("stream 0 set four times: X, Y, X and X on the new encoder [-DDM_TEST_VB_ELIDE_ANY: 2]", 4, s1 - s0);
    EXPECT("... and skipped once (the fourth quarter)", 1, k1 - k0);
    printf("== %d checks, %d failed ==\n", count, fail); return fail != 0;
} }
