// SPIRV-Cross's NoContraction helper inlined (gfx/tools/msl_helpers.py) computes the same bits as the optnone helper it replaces: a product added to a
// third value, for 65,536 random triples and for triples whose fused result differs from the separately rounded one (checked: the fused kernel gives
// other bits there, so a helper that fused would show). Both compiled as dmsh_build.py compiles the game's shaders (safe math, invariance kept).
// Usage: t_fpcontract <helper MSL> <rewritten MSL>. Measured on the way: under the safe math mode even a plain a * b + c is not fused (the helper's
// optnone guards against a fusion this compile never makes), and the rewrite stays unfused under fast math too (DM_TEST_FPC_FAST=1 compiles it so).
// A rewrite to a plain l + r (msl_helpers.py --plain) compiled with fast math, the broken variant, fuses: 1 fails.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
static int fail, total;
static void check(const char *name, int ok) { total++; fail += !ok; printf("%-72s %s\n", name, ok ? "✓" : "✗"); }
static id<MTLDevice> dev; static id<MTLCommandQueue> q;
static void run(id<MTLLibrary> lib, NSString *fn, id<MTLBuffer> a, id<MTLBuffer> b, id<MTLBuffer> c, id<MTLBuffer> o, NSUInteger n) {
    NSError *e = nil; id<MTLComputePipelineState> ps = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:fn] error:&e];
    if (!ps) { printf("pipeline %s: %s\n", fn.UTF8String, e.localizedDescription.UTF8String); exit(2); }
    id<MTLCommandBuffer> cb = [q commandBuffer]; id<MTLComputeCommandEncoder> en = [cb computeCommandEncoder];
    [en setComputePipelineState:ps]; [en setBuffer:a offset:0 atIndex:0]; [en setBuffer:b offset:0 atIndex:1]; [en setBuffer:c offset:0 atIndex:2]; [en setBuffer:o offset:0 atIndex:3];
    [en dispatchThreads:MTLSizeMake(n, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)]; [en endEncoding]; [cb commit]; [cb waitUntilCompleted];
}
static id<MTLLibrary> lib_of(const char *path, int fast) {
    NSString *src = [NSString stringWithContentsOfFile:@(path) encoding:NSUTF8StringEncoding error:nil];
    MTLCompileOptions *opt = [MTLCompileOptions new]; opt.mathMode = fast ? MTLMathModeFast : MTLMathModeSafe; opt.preserveInvariance = YES;   // dmsh_build.py: -fmetal-math-mode=safe -fpreserve-invariance
    NSError *e = nil; id<MTLLibrary> l = src ? [dev newLibraryWithSource:src options:opt error:&e] : nil;
    if (!l) { printf("%s: %s\n", path, e ? e.localizedDescription.UTF8String : "unreadable"); exit(2); }
    return l;
}
int main(int argc, char **argv) { @autoreleasepool {
    if (argc != 3) { fprintf(stderr, "usage: t_fpcontract <helper MSL> <rewritten MSL>\n"); return 2; }
    dev = MTLCreateSystemDefaultDevice(); q = [dev newCommandQueue];
    id<MTLLibrary> helper = lib_of(argv[1], 0), inl = lib_of(argv[2], getenv("DM_TEST_FPC_FAST") != NULL);   // the broken variant: the rewrite compiled with fast math
    enum { N = 65536 + 1024 };
    id<MTLBuffer> A = [dev newBufferWithLength:N * 4 options:MTLResourceStorageModeShared], B = [dev newBufferWithLength:N * 4 options:MTLResourceStorageModeShared],
                  C = [dev newBufferWithLength:N * 4 options:MTLResourceStorageModeShared], O1 = [dev newBufferWithLength:N * 4 options:MTLResourceStorageModeShared],
                  O2 = [dev newBufferWithLength:N * 4 options:MTLResourceStorageModeShared], O3 = [dev newBufferWithLength:N * 4 options:MTLResourceStorageModeShared];
    float *a = A.contents, *b = B.contents, *c = C.contents; uint64_t s = 0x9e3779b97f4a7c15ull;
    for (int i = 0; i < 65536; i++) {   // random normal floats over a wide range
        uint32_t w[3]; for (int k = 0; k < 3; k++) { s ^= s << 13; s ^= s >> 7; s ^= s << 17; w[k] = (uint32_t)(s >> 9 & 0x007fffff) | (uint32_t)((96 + (s >> 40) % 64) << 23) | (uint32_t)(s >> 63 << 31); }
        memcpy(&a[i], &w[0], 4); memcpy(&b[i], &w[1], 4); memcpy(&c[i], &w[2], 4);
    }
    for (int i = 65536; i < N; i++) {   // (1 + e)(1 - e) - 1: the product rounds to 1, so the separate sum is 0 and the fused one -e²
        float e = ldexpf(1.0f + (float)(i - 65536) / 1024.0f, -12 - (i % 8)); a[i] = 1.0f + e; b[i] = 1.0f - e; c[i] = -1.0f;
    }
    run(helper, @"k_helper", A, B, C, O1, N); run(inl, @"k_helper", A, B, C, O2, N); run(helper, @"k_fused", A, B, C, O3, N);
    int same = !memcmp(O1.contents, O2.contents, N * 4), differ = 0, crafted = 0;
    for (int i = 65536; i < N; i++) { uint32_t x, y; memcpy(&x, (float *)O1.contents + i, 4); memcpy(&y, (float *)O3.contents + i, 4); crafted++; differ += x != y; }
    int rdiff = 0; for (int i = 0; i < 65536; i++) { uint32_t x, y; memcpy(&x, (float *)O1.contents + i, 4); memcpy(&y, (float *)O2.contents + i, 4); rdiff += x != y; }
    printf("random triples differing: %d of 65536; crafted triples where fusing changes the bits: %d of %d\n", rdiff, differ, crafted);
    check("the crafted triples tell a fused sum from the separate one", differ >= crafted - 8);
    check("the inlined helper gives the optnone helper's bits for every triple", same);
    printf("FPCONTRACT %s %d/%d\n", fail ? "FAIL" : "OK", total - fail, total);
    return fail != 0;
} }
