// Sampler border colours Metal lacks (nvn_mtl.m samp_border_custom / samp_by_id / border_src, plan item 161) on the GPU: a sampler registered with
// clamp to border (0.5, 0.5, 0.5, 0.5), the grass map's, through the production sampler path, and a kernel in the translator's style (the texture and
// its sampler in spvDescriptorSet0) sampling a red 4x4 texture at u = -0.5, 0.5 and 1.5. Plain: outside the texture it reads the edge (red; it was
// transparent black); rewritten by border_src (one call): 0.5 outside, red inside; a 2D array sample with its layer rewritten too compiles; samplers
// with Metal's own border colours are not taken. The nearest of Metal's three (-DDM_TEST_BORDER_OFF): 1 fails
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define CHECK(c, ...) do { count++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
static NSString *const KSRC = @"#include <metal_stdlib>\nusing namespace metal;\n"
    "struct spvDescriptorSetBuffer0 { texture2d<float> tex [[id(0)]]; sampler texSmplr [[id(1)]]; };\n"
    "kernel void k(constant spvDescriptorSetBuffer0& spvDescriptorSet0 [[buffer(0)]], device float4 *out [[buffer(1)]], uint i [[thread_position_in_grid]])\n"
    "{ float2 uv = float2(-0.5 + float(i), 0.5); out[i] = spvDescriptorSet0.tex.sample(spvDescriptorSet0.texSmplr, uv, level(0.0)); }\n";
static NSString *const ASRC = @"#include <metal_stdlib>\nusing namespace metal;\n"
    "struct spvDescriptorSetBuffer0 { texture2d_array<float> arr [[id(0)]]; sampler arrSmplr [[id(1)]]; };\n"
    "fragment float4 f(constant spvDescriptorSetBuffer0& spvDescriptorSet0 [[buffer(0)]], float4 p [[position]])\n"
    "{ float3 c = float3(p.xy, 1.0); return spvDescriptorSet0.arr.sample(spvDescriptorSet0.arrSmplr, c.xy, uint(rint(c.z))); }\n";
static id<MTLCommandQueue> g_q3;
static void run(NSString *src, id<MTLTexture> t, id<MTLSamplerState> s, float out[3][4]) {
    NSError *e = nil; id<MTLLibrary> lib = [g_dev newLibraryWithSource:src options:nil error:&e];
    if (!lib) { printf("compile: %s\n", e.localizedDescription.UTF8String); exit(2); }
    id<MTLComputePipelineState> ps = [g_dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"k"] error:&e];
    id<MTLBuffer> ab = [g_dev newBufferWithLength:16 options:MTLResourceStorageModeShared], ob = [g_dev newBufferWithLength:48 options:MTLResourceStorageModeShared];
    MTLResourceID ids[2] = {t.gpuResourceID, s.gpuResourceID}; memcpy(ab.contents, ids, sizeof ids);
    id<MTLCommandBuffer> cb = [g_q3 commandBuffer]; id<MTLComputeCommandEncoder> en = [cb computeCommandEncoder];
    [en setComputePipelineState:ps]; [en setBuffer:ab offset:0 atIndex:0]; [en setBuffer:ob offset:0 atIndex:1]; [en useResource:t usage:MTLResourceUsageRead];
    [en dispatchThreads:MTLSizeMake(3, 1, 1) threadsPerThreadgroup:MTLSizeMake(3, 1, 1)]; [en endEncoding]; [cb commit]; [cb waitUntilCompleted];
    memcpy(out, ob.contents, 48);
}
int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0);
    mtl_init(); g_q3 = [g_dev newCommandQueue];
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:4 height:4 mipmapped:NO]; td.storageMode = MTLStorageModeShared;
    id<MTLTexture> red = [g_dev newTextureWithDescriptor:td]; uint32_t px[16]; for (int i = 0; i < 16; i++) px[i] = 0xff0000ff;
    [red replaceRegion:MTLRegionMake2D(0, 0, 4, 4) mipmapLevel:0 withBytes:px bytesPerRow:16];
    samp_par sp = {.minf = 1, .magf = 1, .ws = 5, .wt = 5, .wr = 5, .lod_max = 16, .border = {0.5f, 0.5f, 0.5f, 0.5f}};
    samp_par black = sp; memset(black.border, 0, sizeof black.border);
    mtl_reg_samp(7, &sp); mtl_reg_samp(8, &black);
    pthread_mutex_lock(&g_rmu); id<MTLSamplerState> s = samp_by_id(7); int custom = g_sreg_ok[7] == 2, plain_black = g_sreg_ok[8] == 1; pthread_mutex_unlock(&g_rmu);
    CHECK(custom && plain_black && g_border_any, "(0.5, 0.5, 0.5, 0.5) taken as a colour Metal lacks, transparent black not");
    float o[3][4]; run(KSRC, red, s, o);
    CHECK(o[0][0] == 1 && o[0][3] == 1 && o[1][0] == 1, "plain sample through it: the edge (red) outside the texture, not transparent black (%.2f %.2f)", o[0][0], o[0][3]);
    int cnt = 0; float col[1][4] = {{0.5f, 0.5f, 0.5f, 0.5f}};
    NSString *rw = border_src(KSRC, @[@"texSmplr"], (const float (*)[4])col, &cnt);
    run(rw, red, s, o);
    CHECK(cnt == 1 && fabsf(o[0][0] - 0.5f) < 1e-6 && fabsf(o[0][3] - 0.5f) < 1e-6 && o[1][0] == 1 && fabsf(o[2][1] - 0.5f) < 1e-6,
          "rewritten (%d call): 0.5 outside on both sides, red inside (%.2f / %.2f / %.2f)", cnt, o[0][0], o[1][0], o[2][1]);
    int acnt = 0; NSString *arw = border_src(ASRC, @[@"arrSmplr"], (const float (*)[4])col, &acnt); NSError *e = nil;
    CHECK(acnt == 1 && [g_dev newLibraryWithSource:arw options:nil error:&e] != nil, "a 2D array sample with its layer argument rewritten compiles%s%s", e ? ": " : "", e ? e.localizedDescription.UTF8String : "");
    printf("== %d checks, %d failed ==\n", count, fail); return fail != 0;
} }
