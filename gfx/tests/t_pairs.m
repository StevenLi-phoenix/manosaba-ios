// Pairs noted at bind time (nvn_mtl.m pair_note, plan item 137), through the replay's own pipeline builder pso_get with DM_SHDUMP set to a
// fresh directory: a vertex shader writing location 0 with a pixel shader reading it builds, and its pair is written to pairs.tsv as
// ProgramSetShaders writes it (two 16-digit keys and a tab: what dmsh_build.py reads); the same pipeline again (cached) and the same pair under
// another colour format (another pipeline) add nothing; a pixel shader reading location 3 too fails to link ("not written by vertex shader") and its
// pair is written, so the next shader build pads the vertex shader for it. Every draw's pipeline passes here, whether the program came from
// ProgramSetShaders or from BindProgram's stage bits. Not noted (-DDM_TEST_PAIRS_NO_BIND): 3 fail
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define CHECK(c, ...) do { count++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)

static const char *SRC =
    "#include <metal_stdlib>\nusing namespace metal;\n"
    "struct VO { float4 pos [[position]]; float4 m0 [[user(locn0)]]; };\n"
    "vertex VO v0(uint vid [[vertex_id]]) { VO o; o.pos = float4(float(vid & 1u), float(vid >> 1u), 0.0, 1.0); o.m0 = float4(1.0); return o; }\n"
    "struct FI { float4 m0 [[user(locn0)]]; };\n"
    "fragment float4 f_ok(FI i [[stage_in]]) { return i.m0; }\n"
    "struct FI3 { float4 m0 [[user(locn0)]]; float4 m3 [[user(locn3)]]; };\n"
    "fragment float4 f_bad(FI3 i [[stage_in]]) { return i.m0 + i.m3; }\n";
static NvShader *shader(id<MTLLibrary> lib, NSString *fn, int stage, const char *key) {
    NvShader *s = [NvShader new]; s->fn = [lib newFunctionWithName:fn]; s->stage = stage; strlcpy(s->key, key, sizeof s->key); return s;
}
static int lines(const char *path, char out[8][64]) {
    FILE *f = fopen(path, "r"); int n = 0; char l[256];
    if (!f) return 0;
    while (fgets(l, sizeof l, f)) { if (n < 8) strlcpy(out[n], l, sizeof out[n]); n++; }
    fclose(f); return n;
}
int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0);
    char dir[512]; snprintf(dir, sizeof dir, "%s/t_pairs_XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 2; }
    setenv("DM_SHDUMP", dir, 1);
    char path[600]; snprintf(path, sizeof path, "%s/pairs.tsv", dir);
    mtl_init(); state_reset();
    NSError *e = nil; id<MTLLibrary> lib = [g_dev newLibraryWithSource:@(SRC) options:nil error:&e];
    if (!lib) { printf("library: %s\n", e.localizedDescription.UTF8String); return 2; }
    NvShader *vs = shader(lib, @"v0", 0, "00112233445566aa_b"), *ok = shader(lib, @"f_ok", 1, "00112233445566bb_b"), *bad = shader(lib, @"f_bad", 1, "00112233445566cc_b");
    NvProg *p_ok = [NvProg new]; p_ok->sh[0] = vs; p_ok->sh[1] = ok;
    NvProg *p_bad = [NvProg new]; p_bad->sh[0] = vs; p_bad->sh[1] = bad;
    char l[8][64];
    memset(&g_pass, 0, sizeof g_pass); g_pass.cfmt[0] = MTLPixelFormatRGBA8Unorm; g_pass.samples = 1;
    id ps = pso_get(p_ok, nil);
    int n = lines(path, l);
    CHECK(ps != nil && n == 1 && !strcmp(l[0], "00112233445566aa\t00112233445566bb\n"), "a pair that links: built, written as \"vertex<TAB>pixel\" (%d line%s)", n, n == 1 ? "" : "s");
    pso_get(p_ok, nil);
    g_pass.cfmt[0] = MTLPixelFormatBGRA8Unorm; id ps2 = pso_get(p_ok, nil);
    n = lines(path, l);
    CHECK(ps2 != nil && ps2 != ps && n == 1, "the same pipeline again and the same pair under another colour format: still the one line (%d line%s)", n, n == 1 ? "" : "s");
    id psb = pso_get(p_bad, nil);
    n = lines(path, l);
    CHECK(psb == nil && n == 2 && !strcmp(l[1], "00112233445566aa\t00112233445566cc\n"), "a pixel shader reading location 3 the vertex shader does not write: no pipeline, its pair written (%d lines)", n);
    CHECK(g_pair_bind_new == (uint64_t)n, "the count of pairs written matches the file (%llu)", (unsigned long long)g_pair_bind_new);
    unlink(path); rmdir(dir);
    printf("== %d checks, %d failed ==\n", count, fail);
    return fail != 0;
} }
