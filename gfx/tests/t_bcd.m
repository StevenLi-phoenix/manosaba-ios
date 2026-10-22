// The GPU decompression of BC1-5 on GPUs without BC (the kernel bcd in nvn_mtl.m, plan item 191 after aerolithink 4e98460) against the CPU decoder
// (nvn_bc_decode, bcdec): 1,024 blocks of random bytes per format (so the index words' top bytes are 0x80 or more about half the time), decoded into
// the format the upload path gives the texture (nvn_bc_unpacked), every texel and channel within 1 of the CPU's (the kernel interpolates in float,
// bcdec in integers). RGB_DXT1, RGBA_DXT1, DXT3, DXT5, BC4 and BC5 unsigned and signed. The index word without its top byte
// (-DDM_TEST_BCD_HIGH_BYTE_LOST, as a compiler losing the shifted-in high byte would leave it): the four BC1-3 checks fail.
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int n, fail;
static void check(const char *what, int ok) { n++; fail += !ok; printf("%-104s %s\n", what, ok ? "✓" : "✗"); }
static uint32_t g_rng = 4242;
static uint32_t rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0);
    setenv("DM_TEST_NO_BC", "1", 1); mtl_init();
    id<MTLComputePipelineState> ps = bcd_pso(); id<MTLCommandQueue> q = [g_dev newCommandQueue];
    check("the kernel bcd compiles", ps != nil);
    if (!ps) { printf("== %d checks, %d failed ==\n", n, fail); return 1; }
    enum { BW = 32, BH = 32, W = BW * 4, H = BH * 4 };
    static const struct { uint32_t f; const char *name; } fm[] = {{0x41, "RGB_DXT1"}, {0x42, "RGBA_DXT1"}, {0x43, "DXT3"}, {0x44, "DXT5"},
        {0x49, "BC4 unsigned"}, {0x4a, "BC4 signed"}, {0x4b, "BC5 unsigned"}, {0x4c, "BC5 signed"}};
    for (size_t i = 0; i < sizeof fm / sizeof *fm; i++) {
        uint32_t f = fm[i].f, pf = 0; int bpp = 0; nvn_bc_unpacked(f, &pf, &bpp);
        int bb = f == 0x41 || f == 0x42 || f == 0x49 || f == 0x4a ? 8 : 16, sn = f == 0x4a || f == 0x4c;
        size_t pitch = (size_t)BW * bb; uint8_t *src = malloc(pitch * BH), *ref = malloc((size_t)W * H * bpp), *got = malloc((size_t)W * H * bpp);
        for (size_t k = 0; k < pitch * BH; k++) src[k] = (uint8_t)rnd();
        nvn_bc_decode(f, src, pitch, W, H, ref, (size_t)W * bpp);
        MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:(MTLPixelFormat)pf width:W height:H mipmapped:NO];
        d.storageMode = MTLStorageModeShared; d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite; id<MTLTexture> t = [g_dev newTextureWithDescriptor:d];
        struct { uint32_t off, pitch, w, h, fmt, z; } p = {0, (uint32_t)pitch, W, H, (uint32_t)bcd_fmt(f), 0};
        id<MTLBuffer> sb = [g_dev newBufferWithBytes:src length:pitch * BH options:MTLResourceStorageModeShared];
        id<MTLCommandBuffer> cb = [q commandBuffer]; id<MTLComputeCommandEncoder> e = [cb computeCommandEncoder];
        [e setComputePipelineState:ps]; [e setBuffer:sb offset:0 atIndex:0]; [e setBytes:&p length:sizeof p atIndex:1]; [e setTexture:t atIndex:0];
        [e dispatchThreadgroups:MTLSizeMake((BW + 7) / 8, (BH + 7) / 8, 1) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)]; [e endEncoding];
        [cb commit]; [cb waitUntilCompleted];
        [t getBytes:got bytesPerRow:(NSUInteger)W * bpp fromRegion:MTLRegionMake2D(0, 0, W, H) mipmapLevel:0];
        int worst = 0; size_t at = 0;
        for (size_t k = 0; k < (size_t)W * H * bpp; k++) {
            int a = sn ? (int8_t)got[k] : got[k], b = sn ? (int8_t)ref[k] : ref[k], dd = abs(a - b);
            if (sn && a >= -127 && b >= -127 && dd > worst) { worst = dd; at = k; }   // -128 and -127 are both -1.0 in snorm
            else if (!sn && dd > worst) { worst = dd; at = k; }
        }
        char what[160]; snprintf(what, sizeof what, "%s (format %#x, Metal %u): %d texels, the largest difference %d (byte %zu)", fm[i].name, f, pf, W * H, worst, at);
        check(what, worst <= 1);
        free(src); free(ref); free(got);
    }
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
} }
