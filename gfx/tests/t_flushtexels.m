// FlushTexels as a CPU write of the region's storage (plan item 64j). An 8x8 RGBA8 texture in a CPU-cached pool that the GPU has written
// (tex_gpu_write: its MTLTexture is the content):
//   WriteTexels red over all of it, then FlushTexels of all of it: still red (its storage, zeros, is not taken: ours wrote the MTLTexture)
//   the CPU writes green into its storage over a 4x4 region at (2, 2) and flushes that region: the region green, the rest still red
//   a packaged texture (its storage is its content, watched by pages): FlushTexels takes nothing
// Flush ignored (-DDM_TEST_FLUSH_IGNORED): 1 fails; the region taken after a WriteTexels too (-DDM_TEST_FLUSH_AFTER_WT): 2 fail.
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int n, fail;
#define CHECK(c, ...) do { n++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
static pool_t g_ft_pl;
static tex_t mk(uint8_t *mem, int w, int h) {
    static uint64_t ser = 99500; tex_t t; memset(&t, 0, sizeof t); t.serial = ser++; t.mem = mem; t.pool = &g_ft_pl;
    t.d.target = NVN_TEXTURE_TARGET_2D; t.d.format = NVN_FORMAT_RGBA8; t.d.width = w; t.d.height = h; t.d.depth = 1; t.d.levels = 1; t.d.samples = 1;
    nvn_texlayout L; nvn_tex_layout(&t.d, &L); t.size = (uint32_t)L.size; return t;
}
static void read_back(id<MTLTexture> mt, uint8_t *out) {   // 8x8 RGBA8
    id<MTLBuffer> b = [g_dev newBufferWithLength:256 options:MTLResourceStorageModeShared]; id<MTLCommandBuffer> cb = [g_q commandBuffer]; id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
    [bl copyFromTexture:mt sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(8, 8, 1) toBuffer:b destinationOffset:0 destinationBytesPerRow:32 destinationBytesPerImage:256];
    [bl endEncoding]; [cb commit]; [cb waitUntilCompleted]; memcpy(out, b.contents, 256);
}
static int is(const uint8_t *px, uint8_t r, uint8_t g) { return px[0] == r && px[1] == g && px[2] == 0 && px[3] == 255; }

int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); mtl_init();
    static _Alignas(16384) uint8_t mem[1 << 16]; g_ft_pl.mem = mem; g_ft_pl.size = sizeof mem; g_ft_pl.flags = NVN_MEMORY_POOL_FLAGS_CPU_CACHED | NVN_MEMORY_POOL_FLAGS_GPU_CACHED; mtl_pool_add(mem, sizeof mem);
    tex_t t = mk(mem, 8, 8); tex_gpu_write(&t); id<MTLTexture> mt = tex_mtl(&t);
    if (!mt) { printf("no texture\n"); return 2; }
    uint8_t red[256], lin[256], px[256]; for (int i = 0; i < 64; i++) { red[i * 4] = 255; red[i * 4 + 1] = 0; red[i * 4 + 2] = 0; red[i * 4 + 3] = 255; }
    NVNcopyRegion all = {.width = 8, .height = 8, .depth = 1};
    mtl_write_texels(&t, 0, 0, &all, red, 32, 256); mtl_flush_texels(&t, 0, 0, &all);
    read_back(mt, px); int ok = 1; for (int i = 0; i < 64; i++) ok &= is(px + i * 4, 255, 0);
    CHECK(ok, "WriteTexels red, then FlushTexels of it all: still red (the storage, zeros, not taken)");
    memset(lin, 0, sizeof lin); for (int y = 2; y < 6; y++) for (int x = 2; x < 6; x++) { uint8_t *p = lin + y * 32 + x * 4; p[1] = 255; p[3] = 255; }
    if (nvn_swizzle(&t.d, 0, 0, lin, 32, t.mem)) { printf("swizzle failed\n"); return 2; }
    NVNcopyRegion reg = {.xoffset = 2, .yoffset = 2, .width = 4, .height = 4, .depth = 1}; mtl_flush_texels(&t, 0, 0, &reg);
    read_back(mt, px); ok = 1;
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) { int in = x >= 2 && x < 6 && y >= 2 && y < 6; ok &= in ? is(px + y * 32 + x * 4, 0, 255) : is(px + y * 32 + x * 4, 255, 0); }
    CHECK(ok, "the CPU writes green into the storage of a 4x4 region at (2, 2) and flushes it: the region green, the rest red");
    tex_t pk = mk(mem + 0x8000, 8, 8); pk.has_layout = 1; memcpy(lin, red, 256); nvn_swizzle(&pk.d, 0, 0, lin, 32, pk.mem); tex_mtl(&pk);
    uint64_t before = g_flush_up; mtl_flush_texels(&pk, 0, 0, &all);
    CHECK(g_flush_up == before, "a packaged texture (its storage the content, watched by pages): FlushTexels takes nothing");
    printf("FLUSHTEXELS %s %d/%d\n", fail ? "FAIL" : "OK", n - fail, n);
    return fail != 0;
} }
