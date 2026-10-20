// WriteTexels on textures in CPU-visible memory (nvn_mtl.m mtl_write_texels, the deswizzle - patch - swizzle path; plan item 128), with totk-mobile's
// tests/test_nvn_objects.c regions (Apache-2.0, as data): a 4x1 RGBA8 row reads back; a 4x4 cube's six faces written at once each read back; a
// 2x1 region at (1, 2) of face 3 lands there and leaves its neighbours (bytes 35 and 44 of the face still 4); a BC4 region of 8x6 texels at (4, 4),
// ending inside a block row, writes its four whole blocks and no other; two 320x320 RGB565-sized (format 0x3b) MINIMAL_LAYOUT textures back to back
// (the layout of item 82: 204,800 bytes each) each read back and nothing past the second is touched. A write whose rows are placed at the
// region's width instead of the level's (-DDM_TEST_WT_ROWS_AT_REGION) fails 2.
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int n, fail;
#define CHECK(c, ...) do { n++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
static pool_t g_pl;
static tex_t mk(uint8_t *mem, int target, int format, int w, int h, int d, int flags) {
    static uint64_t ser = 99000; tex_t t; memset(&t, 0, sizeof t); t.serial = ser++; t.mem = mem; t.pool = &g_pl;
    t.d.target = target; t.d.format = format; t.d.width = w; t.d.height = h; t.d.depth = d; t.d.levels = 1; t.d.samples = 1; t.d.flags = flags;
    nvn_texlayout L; nvn_tex_layout(&t.d, &L); t.size = (uint32_t)L.size; return t;
}
int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); mtl_init();
    static uint8_t mem[1 << 20]; g_pl.mem = mem; g_pl.size = sizeof mem; g_pl.flags = NVN_MEMORY_POOL_FLAGS_CPU_CACHED | NVN_MEMORY_POOL_FLAGS_GPU_CACHED;
    uint8_t pixels[16], back[64 * 6]; for (int i = 0; i < 16; i++) pixels[i] = (uint8_t)(i * 7);
    tex_t t = mk(mem, NVN_TEXTURE_TARGET_1D, NVN_FORMAT_RGBA8, 4, 1, 1, 0);
    NVNcopyRegion r = {.width = 4, .height = 1, .depth = 1}; mtl_write_texels(&t, 0, 0, &r, pixels, 16, 16);
    CHECK(!nvn_deswizzle(&t.d, 0, 0, t.mem, back, 16) && !memcmp(back, pixels, 16), "a 4x1 RGBA8 row reads back");
    uint8_t faces[6 * 64]; for (int i = 0; i < (int)sizeof faces; i++) faces[i] = (uint8_t)(i / 64 + 1);
    memset(mem + 0x10000, 0, 0x4000); tex_t c = mk(mem + 0x10000, NVN_TEXTURE_TARGET_CUBEMAP, NVN_FORMAT_RGBA8, 4, 4, 6, 0);
    r = (NVNcopyRegion){.width = 4, .height = 4, .depth = 6}; mtl_write_texels(&c, 0, 0, &r, faces, 16, 64);
    int ok = 1; for (int f = 0; f < 6; f++) ok &= !nvn_deswizzle(&c.d, 0, f, c.mem, back, 16) && !memcmp(back, faces + f * 64, 64);
    CHECK(ok, "a 4x4 cube's six faces written at once each read back");
    r = (NVNcopyRegion){.xoffset = 1, .yoffset = 2, .zoffset = 3, .width = 2, .height = 1, .depth = 1}; mtl_write_texels(&c, 0, 0, &r, pixels, 8, 8);
    CHECK(!nvn_deswizzle(&c.d, 0, 3, c.mem, back, 16) && !memcmp(back + 36, pixels, 8) && back[35] == 4 && back[44] == 4, "a 2x1 region at (1, 2) of face 3 lands there, its neighbours stay");
    uint8_t blocks[2 * 2 * 8], out[4 * 4 * 8]; for (int i = 0; i < (int)sizeof blocks; i++) blocks[i] = (uint8_t)(i + 1);
    memset(mem + 0x20000, 0, 0x4000); tex_t b = mk(mem + 0x20000, NVN_TEXTURE_TARGET_2D, 0x49, 16, 16, 1, 0);
    r = (NVNcopyRegion){.xoffset = 4, .yoffset = 4, .width = 8, .height = 6, .depth = 1}; mtl_write_texels(&b, 0, 0, &r, blocks, 16, 32);
    ok = !nvn_deswizzle(&b.d, 0, 0, b.mem, out, 32);
    for (int by = 0; by < 4; by++) for (int bx = 0; bx < 4; bx++) { int in = bx >= 1 && bx <= 2 && by >= 1 && by <= 2;
        for (int i = 0; i < 8; i++) ok &= out[(by * 4 + bx) * 8 + i] == (in ? blocks[((by - 1) * 2 + bx - 1) * 8 + i] : 0); }
    CHECK(ok, "a BC4 region 8x6 at (4, 4), ending inside a block row: its four whole blocks, no other");
    const size_t bytes = 320 * 320 * 2; uint8_t *two = mem + 0x40000; memset(two, 0xa5, 2 * bytes + 65536);
    uint8_t *lin = malloc(bytes), *rb = malloc(bytes); for (size_t i = 0; i < bytes; i++) lin[i] = (uint8_t)(i * 37 + i / 640);
    ok = 1; int inside = 1;
    for (int i = 0; i < 2; i++) {
        tex_t m = mk(two + i * bytes, NVN_TEXTURE_TARGET_2D, 0x3b + i, 320, 320, 1, NVN_TEXTURE_FLAGS_MINIMAL_LAYOUT);
        ok &= m.size == bytes; r = (NVNcopyRegion){.width = 320, .height = 320, .depth = 1}; mtl_write_texels(&m, 0, 0, &r, lin, 640, bytes);
        ok &= !nvn_deswizzle(&m.d, 0, 0, m.mem, rb, 640) && !memcmp(lin, rb, bytes);
        for (size_t j = (size_t)(i + 1) * bytes; j < 2 * bytes + 65536; j++) inside &= two[j] == 0xa5;
    }
    CHECK(ok && inside, "two 320x320 MINIMAL_LAYOUT textures back to back (204,800 bytes each): each reads back, nothing past them touched");
    free(lin); free(rb);
    printf("WRITETEXELS %s %d/%d\n", fail ? "FAIL" : "OK", n - fail, n);
    return fail != 0;
} }
