// One pixel shader bound to float and to integer targets (nvn_mtl.m pso_desc / pso_int_target / fout_fn, plan item 162): a shader whose float library
// writes float4 and whose integer-output variant writes uint4 at colour 0 gets the float function on an RGBA8 target and the variant on an RGBA16Uint
// one, and both pipelines are made; a shader with no variant keeps the float function; an integer format in a slot the shader does not write does not
// count. The float function whatever the target (-DDM_TEST_FOUT_NO_VARIANT): 1 fails
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define CHECK(c, ...) do { count++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
static NSString *const SRC = @"#include <metal_stdlib>\nusing namespace metal;\n"
    "vertex float4 v(uint i [[vertex_id]]) { return float4(float(i & 1), float(i >> 1), 0, 1); }\n"
    "struct F { float4 c [[color(0)]]; }; fragment F ff() { return {float4(1.0)}; }\n"
    "struct U { uint4 c [[color(0)]]; }; fragment U fu() { return {uint4(1u)}; }\n";
static id built(NvShader *vs, NvShader *fs, MTLPixelFormat f0, MTLPixelFormat f1, id<MTLFunction> *used) {
    state_reset(); uint32_t cfmt[8] = {(uint32_t)f0, (uint32_t)f1};
    pso_key k; pso_key_fill(&k, vs, fs, cfmt, 0, 0, 1, 0);
    MTLRenderPipelineDescriptor *d = pso_desc(&k, vs, fs, nil, 0); *used = d.fragmentFunction;
    NSError *e = nil; return [g_dev newRenderPipelineStateWithDescriptor:d error:&e];
}
int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); mtl_init();
    NSError *e = nil; id<MTLLibrary> lib = [g_dev newLibraryWithSource:SRC options:nil error:&e];
    if (!lib) { printf("%s\n", e.localizedDescription.UTF8String); return 2; }
    NvShader *vs = [NvShader new], *fs = [NvShader new], *plain = [NvShader new];
    vs->fn = [lib newFunctionWithName:@"v"]; strlcpy(vs->key, "00000000000000aa_b", sizeof vs->key);
    for (NvShader *f in @[fs, plain]) { f->fn = [lib newFunctionWithName:@"ff"]; f->stage = 1; f->rf.color_known = 1; f->rf.color_mask = 1; f->ifn_tried = 1; }
    fs->ifn = [lib newFunctionWithName:@"fu"]; strlcpy(fs->key, "00000000000000bb_b", sizeof fs->key); strlcpy(plain->key, "00000000000000cc_b", sizeof plain->key);
    id<MTLFunction> used; id ps;
    ps = built(vs, fs, MTLPixelFormatRGBA8Unorm, 0, &used);
    CHECK(ps && used == fs->fn, "an RGBA8 target: the float function, the pipeline made");
    ps = built(vs, fs, MTLPixelFormatRGBA16Uint, 0, &used);
    CHECK(ps && used == fs->ifn, "an RGBA16Uint target: the integer-output variant, the pipeline made");
    ps = built(vs, plain, MTLPixelFormatRGBA16Uint, 0, &used);
    CHECK(used == plain->fn, "a shader with no variant: the float function (the pipeline fails as before: %s)", ps ? "made" : "not made");
    ps = built(vs, fs, MTLPixelFormatRGBA8Unorm, MTLPixelFormatRGBA16Uint, &used);
    CHECK(used == fs->fn, "an integer format in a slot the shader does not write: the float function");
    printf("== %d checks, %d failed ==\n", count, fail); return fail != 0;
} }
