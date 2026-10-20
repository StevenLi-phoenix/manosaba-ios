// BC1 kept as ASTC 4x4 on GPUs without BC (plan item 62c; gfx/tex/nvn_bc1astc.c, the kernel b1a in nvn_mtl.m). Random BC1 blocks of each kind,
// transcoded on the CPU and decoded by Metal's own ASTC decoder, against bcdec's decode of the BC1 block (nvn_bc_decode):
//   four colours (c0 > c1): every channel within 2 (the palette is ASTC's 2-bit weight set; bcdec rounds a 5-bit 3 to 25 where ASTC's bit
//   replication gives 24, and the weight 21/64 stands for 1/3)
//   three colours, index 3 unused: within 1 (the midpoint is the trit 32/64)
//   transparent (RGBA_DXT1, index 3 used): alpha exact; blocks with one or two opaque colours used: within 1
//   RGB_DXT1 with index 3 (opaque black): alpha 255 everywhere
// The kernel against the CPU transcoder: the same 16 bytes for 4096 blocks of all kinds. The upload path (DM_TEST_NO_BC=1): a 64x64 RGBA_DXT1
// texture in guest memory (block-linear) becomes an ASTC_4x4_LDR texture whose texels are those of the CPU transcoder's blocks; RGBA_DXT1 sRGB an
// ASTC_4x4_sRGB one. With DM_NVN_BC1_ASTC=0 (argument "off"): RGBA8, as before. The four-colour palette in index order (-DDM_TEST_B1A_FOUR_ORDER,
// the CPU transcoder only): 3 fail.
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int n, fail;
static void check(const char *what, int ok) { n++; fail += !ok; printf("%-104s %s\n", what, ok ? "✓" : "✗"); }
static uint32_t g_rng = 12345;
static uint32_t rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
enum { FOUR, THREE, TRANS, TRANS2, RGB3, NKIND };
// A BC1 block of the kind: FOUR c0 > c1; THREE c0 <= c1, indices 0-2; TRANS index 3 among them; TRANS2 the same with one or two opaque colours
static void make(int kind, uint8_t *b) {
    uint16_t c0 = (uint16_t)rnd(), c1 = (uint16_t)rnd();
    if (kind == FOUR) { if (c0 == c1) c1 ^= 1; if (c0 < c1) { uint16_t t = c0; c0 = c1; c1 = t; } }
    else if (c0 > c1) { uint16_t t = c0; c0 = c1; c1 = t; }
    if (kind == THREE && (rnd() & 7) == 0) c1 = c0;   // now and then a flat block
    uint32_t ix = 0;
    for (int k = 0; k < 16; k++) {
        uint32_t i = kind == FOUR ? rnd() & 3 : kind == THREE ? rnd() % 3 : kind == TRANS2 ? ((rnd() & 1) ? 3 : 1) : rnd() & 3;
        if ((kind == TRANS || kind == RGB3 || kind == TRANS2) && k == 5) i = 3;
        ix |= i << (2 * k);
    }
    b[0] = (uint8_t)c0; b[1] = (uint8_t)(c0 >> 8); b[2] = (uint8_t)c1; b[3] = (uint8_t)(c1 >> 8); memcpy(b + 4, &ix, 4);
}
static id<MTLCommandQueue> g_tq; static id<MTLComputePipelineState> g_rd;
// RGBA8 texels (bw * 4 wide) of a texture, through a compute kernel
static void texels(id<MTLTexture> t, uint8_t *out) {
    size_t w = t.width, h = t.height; id<MTLBuffer> o = [g_dev newBufferWithLength:w * h * 4 options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> cb = [g_tq commandBuffer]; id<MTLComputeCommandEncoder> e = [cb computeCommandEncoder]; uint32_t ww = (uint32_t)w;
    [e setComputePipelineState:g_rd]; [e setTexture:t atIndex:0]; [e setBuffer:o offset:0 atIndex:0]; [e setBytes:&ww length:4 atIndex:1];
    [e dispatchThreads:MTLSizeMake(w, h, 1) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)]; [e endEncoding]; [cb commit]; [cb waitUntilCompleted];
    memcpy(out, o.contents, w * h * 4);
}
// Metal's decode of nb ASTC blocks laid out as a row of nb blocks (nb * 4 x 4 texels)
static void astc_decode(const uint8_t *astc, int nb, uint8_t *out) {
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatASTC_4x4_LDR width:(NSUInteger)nb * 4 height:4 mipmapped:NO];
    d.storageMode = MTLStorageModePrivate; d.usage = MTLTextureUsageShaderRead; id<MTLTexture> t = [g_dev newTextureWithDescriptor:d];
    id<MTLBuffer> src = [g_dev newBufferWithBytes:astc length:(NSUInteger)nb * 16 options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> cb = [g_tq commandBuffer]; id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
    [bl copyFromBuffer:src sourceOffset:0 sourceBytesPerRow:(NSUInteger)nb * 16 sourceBytesPerImage:(NSUInteger)nb * 16 sourceSize:MTLSizeMake((NSUInteger)nb * 4, 4, 1)
             toTexture:t destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
    [bl endEncoding]; [cb commit]; [cb waitUntilCompleted];
    texels(t, out);
}

int main(int argc, char **argv) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0);
    int off = argc > 1 && !strcmp(argv[1], "off");
    setenv("DM_TEST_NO_BC", "1", 1); if (off) setenv("DM_NVN_BC1_ASTC", "0", 1);
    mtl_init(); g_tq = [g_dev newCommandQueue]; NSError *e = nil;
    id<MTLLibrary> lib = [g_dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\n"
        "kernel void k(texture2d<float, access::read> t [[texture(0)]], device uchar4 *o [[buffer(0)]], constant uint &w [[buffer(1)]], uint2 g [[thread_position_in_grid]]) {\n"
        "  if (g.x < t.get_width() && g.y < t.get_height()) o[g.y * w + g.x] = uchar4(round(clamp(t.read(g), 0.0, 1.0) * 255.0)); }\n" options:nil error:&e];
    if (!lib) { printf("%s\n", e.localizedDescription.UTF8String); return 2; }
    g_rd = [g_dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"k"] error:&e];
    if (off) {
        tex_t t = {.serial = 7900}; t.d = (nvn_texdesc){.target = NVN_TEXTURE_TARGET_2D, .format = NVN_FORMAT_RGBA_DXT1, .width = 16, .height = 16, .depth = 1, .levels = 1, .samples = 1};
        check("DM_NVN_BC1_ASTC=0: an RGBA_DXT1 texture without BC is RGBA8, as before", tex_mtl(&t).pixelFormat == MTLPixelFormatRGBA8Unorm);
        printf("== %d checks, %d failed ==\n", n, fail); return fail != 0;
    }
    enum { NB = 512 };
    static uint8_t bc[NKIND][NB * 8], as[NKIND][NB * 16], got[NB * 64 * 4], ref[64 * 4];
    for (int k = 0; k < NKIND; k++) for (int i = 0; i < NB; i++) { make(k, bc[k] + i * 8); nvn_bc1_astc(bc[k] + i * 8, as[k] + i * 16, k != RGB3); }
    int worst[NKIND] = {0}, alpha_ok[NKIND]; for (int k = 0; k < NKIND; k++) alpha_ok[k] = 1;
    for (int k = 0; k < NKIND; k++) {
        astc_decode(as[k], NB, got);
        for (int i = 0; i < NB; i++) {
            nvn_bc_decode(k == RGB3 ? 0x41 : 0x42, bc[k] + i * 8, 8, 4, 4, ref, 16);
            for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {
                const uint8_t *r = ref + y * 16 + x * 4, *g = got + (y * NB * 4 + i * 4 + x) * 4;
                int a_want = k == RGB3 ? 255 : r[3];
                if (g[3] != a_want) alpha_ok[k] = 0;
                if (k == TRANS || k == RGB3 || (r[3] == 0 && k == TRANS2)) continue;   // colours: not of transparent texels, nor of the blocks where two endpoints cannot hold them
                for (int c = 0; c < 3; c++) { int d = abs((int)g[c] - (int)r[c]); if (d > worst[k]) worst[k] = d; }
                if (k != TRANS2) { int d = abs((int)g[3] - (int)r[3]); if (d > worst[k]) worst[k] = d; }
            }
        }
    }
    char line[160];
    snprintf(line, sizeof line, "four colours (c0 > c1): every channel within 2 of bcdec (largest %d)", worst[FOUR]); check(line, worst[FOUR] <= 2);
    snprintf(line, sizeof line, "three colours, index 3 unused: within 1 (largest %d)", worst[THREE]); check(line, worst[THREE] <= 1);
    check("transparent texels (RGBA_DXT1, index 3): alpha exactly bcdec's", alpha_ok[TRANS] && alpha_ok[TRANS2]);
    snprintf(line, sizeof line, "... a transparent block with one opaque colour: its colour within 1 (largest %d)", worst[TRANS2]); check(line, worst[TRANS2] <= 1);
    check("RGB_DXT1 with index 3 (opaque black): alpha 255 everywhere", alpha_ok[RGB3]);

    id<MTLComputePipelineState> ps = b1a_pso(); int same = ps != nil;
    for (int k = 0; k < NKIND && same; k++) {
        id<MTLBuffer> stg = [g_dev newBufferWithLength:NB * 24 options:MTLResourceStorageModeShared]; memcpy(stg.contents, bc[k], NB * 8);
        uint32_t p[6] = {0, NB * 8, NB, 1, NB * 8, k != RGB3};
        id<MTLCommandBuffer> cb = [g_tq commandBuffer]; id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
        [ce setComputePipelineState:ps]; [ce setBuffer:stg offset:0 atIndex:0]; [ce setBytes:p length:sizeof p atIndex:1];
        [ce dispatchThreads:MTLSizeMake(NB, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)]; [ce endEncoding]; [cb commit]; [cb waitUntilCompleted];
        uint8_t cpu[NB * 16]; for (int i = 0; i < NB; i++) nvn_bc1_astc(bc[k] + i * 8, cpu + i * 16, k != RGB3);   // (the CPU transcoder as built here)
        same &= !memcmp((uint8_t *)stg.contents + NB * 8, cpu, NB * 16);
    }
    check("the kernel b1a: the CPU transcoder's 16 bytes for every block of every kind", same);

    static uint8_t mem[1 << 20] __attribute__((aligned(16384))); mtl_pool_add(mem, sizeof mem); pool_t pl = {.mem = mem, .size = sizeof mem, .flags = NVN_MEMORY_POOL_FLAGS_CPU_CACHED | NVN_MEMORY_POOL_FLAGS_GPU_CACHED};
    static uint8_t lin[16 * 16 * 8], want[256 * 16]; for (int i = 0; i < 256; i++) make(i % NKIND == RGB3 ? FOUR : i % NKIND, lin + i * 8);
    for (int f = 0; f < 2; f++) {
        tex_t t = {.serial = 7800 + (uint64_t)f}; t.d = (nvn_texdesc){.target = NVN_TEXTURE_TARGET_2D, .format = f ? NVN_FORMAT_RGBA_DXT1_SRGB : NVN_FORMAT_RGBA_DXT1, .width = 64, .height = 64, .depth = 1, .levels = 1, .samples = 1};
        nvn_texlayout L; nvn_tex_layout(&t.d, &L); t.size = (uint32_t)L.size; t.mem = mem + f * 65536; t.pool = &pl;
        nvn_swizzle(&t.d, 0, 0, lin, 16 * 8, t.mem); mtl_tex_live(&t); mtl_reg_tex(60 + f, &t, NULL);
        mtl_sub_begin(); pthread_mutex_lock(&g_rmu); id<MTLTexture> mt = tex_by_id(60 + f); pthread_mutex_unlock(&g_rmu); mtl_wait(mtl_sub_commit(), 5000000000ull);
        if (!f) {
            check("the upload path (DM_TEST_NO_BC=1): an RGBA_DXT1 texture is ASTC_4x4_LDR", mt.pixelFormat == MTLPixelFormatASTC_4x4_LDR);
            static uint8_t up[64 * 64 * 4], cpu[64 * 64 * 4]; texels(mt, up);
            for (int by = 0; by < 16; by++) for (int bx = 0; bx < 16; bx++) nvn_bc1_astc(lin + (by * 16 + bx) * 8, want + (by * 16 + bx) * 16, 1);
            MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatASTC_4x4_LDR width:64 height:64 mipmapped:NO]; d.storageMode = MTLStorageModePrivate;
            id<MTLTexture> rt = [g_dev newTextureWithDescriptor:d]; id<MTLBuffer> sb = [g_dev newBufferWithBytes:want length:sizeof want options:MTLResourceStorageModeShared];
            id<MTLCommandBuffer> cb = [g_tq commandBuffer]; id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
            [bl copyFromBuffer:sb sourceOffset:0 sourceBytesPerRow:16 * 16 sourceBytesPerImage:sizeof want sourceSize:MTLSizeMake(64, 64, 1) toTexture:rt destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
            [bl endEncoding]; [cb commit]; [cb waitUntilCompleted]; texels(rt, cpu);
            check("... its texels are those of the CPU transcoder's blocks, all 4,096", !memcmp(up, cpu, sizeof up));
        } else check("... an RGBA_DXT1 sRGB texture is ASTC_4x4_sRGB", mt.pixelFormat == MTLPixelFormatASTC_4x4_sRGB);
    }
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
} }
