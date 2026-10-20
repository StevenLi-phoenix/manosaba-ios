// Automatic block height of block-linear textures (nvn_tex.c): NVN truncates height in blocks × 1.5 to whole GOB rows before it picks the power of
// two; deko3d's heuristic, which this followed, rounds up. The 40 sizes and block heights below are the original SDK's CPU queries (storage size
// and block height of every mip) for RGBA8 and BC3 boundary cases, as MacNativeGames recorded them (cd85f5b, MIT; no game data); 16 of them
// differed with the rounded-up choice, among them Tears of the Kingdom's 160×160 BC3 shrine warp pad, half of which read padding (black).
// Then 15 vectors the SDK's TextureBuilder gave run in emulation (nxnative PR #11, digitalhivehub bacf2cb nvn_layout_diff.py, MIT): cube array depth =
// faces, MINIMAL_LAYOUT, the packaged block depth, LINEAR_RENDER_TARGET as linear, a linear size in pixel rows. Each kind is in Tears of the Kingdom
// (DM_NVN_TEXCENSUS), the cube array in manosaba too. Then totk-mobile's eight sizes from TotK 1.4.3's SDK. MINIMAL_LAYOUT ignored
// (-DDM_TEST_TEX_NO_MINIMAL) fails 4 of PR #11's vectors and 6 of the eight.
#include "../nvn/nvn_tex.h"
#include "../tex/nvn_deswizzle.h"
#include <stdio.h>
#include <stdint.h>
typedef struct { unsigned format, width, height, levels, h; uint64_t size; } tcase;
static const tcase cases[] = {
    {0x25,40,1,6,0,4608}, {0x25,40,8,6,0,4608}, {0x25,40,9,6,0,6144}, {0x25,40,10,6,0,6144}, {0x25,40,11,6,1,6144}, {0x25,40,16,6,1,6144},
    {0x25,40,17,6,1,9216}, {0x25,40,21,6,1,10240}, {0x25,40,22,6,2,10240}, {0x25,40,32,6,2,10240}, {0x25,40,33,6,2,16384}, {0x25,40,40,6,2,18944},
    {0x25,40,42,6,2,18944}, {0x25,40,43,6,3,18944}, {0x25,40,64,7,3,19456}, {0x25,40,65,7,3,31744}, {0x25,40,85,7,3,37376}, {0x25,40,86,7,4,37376},
    {0x25,40,128,8,4,37888}, {0x25,40,129,8,4,62464},
    {0x48,160,4,8,0,12288}, {0x48,160,32,8,0,12288}, {0x48,160,36,8,0,17408}, {0x48,160,40,8,0,17408}, {0x48,160,44,8,1,17408},
    {0x48,160,64,8,1,17408}, {0x48,160,68,8,1,30208}, {0x48,160,84,8,1,30208}, {0x48,160,88,8,2,30208}, {0x48,160,128,8,2,30208},
    {0x48,160,132,8,2,57344}, {0x48,160,160,8,2,57344}, {0x48,160,168,8,2,57344}, {0x48,160,172,8,3,57344}, {0x48,160,256,9,3,57856},
    {0x48,160,260,9,3,112128}, {0x48,160,340,9,3,113152}, {0x48,160,344,9,4,113152}, {0x48,160,512,10,4,113664}, {0x48,160,516,10,4,222208},
};
static int n, fail;
static void expect(const char *what, unsigned long long want, unsigned long long got) { n++; fail += want != got; if (want != got) printf("%-60s expected %llu → got %llu ✗\n", what, want, got); }
int main(void) {
    int bad = 0;
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        const tcase *c = &cases[i]; char what[96];
        nvn_texdesc d = {.target = 1, .format = (int)c->format, .samples = 1, .width = c->width, .height = c->height, .depth = 1, .levels = c->levels};
        nvn_texlayout l; nvn_tex_layout(&d, &l); int f0 = fail;
        snprintf(what, sizeof what, "format %#x %ux%u, %u levels: block height", c->format, c->width, c->height, c->levels); expect(what, c->h, l.tileH);
        snprintf(what, sizeof what, "format %#x %ux%u, %u levels: size", c->format, c->width, c->height, c->levels); expect(what, c->size, l.size);
        bad += fail != f0;
    }
    printf("SDK layout cases: %d of 40 differ\n", bad);
    nvn_texdesc pad = {.target = 1, .format = 0x48, .samples = 1, .width = 160, .height = 160, .depth = 1, .levels = 8};
    unsigned h, dd; nvn_tex_level_tiles(&pad, 0, &h, &dd);
    expect("160x160 BC3 base level: four-GOB blocks (log2 2)", 2, h);
    expect("its block (80, 0) lies in the second GOB column", 10240, nvn_bl_offset(320, 0, 0, 640, 40, h, dd));
    expect("its last block (156, 39) comes before the padding", 39408, nvn_bl_offset(624, 39, 0, 640, 40, h, dd));
    pad.pkg_layout = 0x103; nvn_tex_level_tiles(&pad, 0, &h, &dd);
    expect("a packaged layout's block height (log2 3) still wins", 3, h);
    struct { const char *what; nvn_texdesc d; uint64_t size, layer, off1; uint32_t align; unsigned th, td; } G[] = {
        {"cube array RGBA8 4x4 depth 6 (Tears of the Kingdom): one cube",     {9, 0x25, 0, 0, 4, 4, 6, 1, 0, 0},           3072, 512, 0, 512, 0, 0},
        {"cube array RGBA8 64x64 depth 12, 7 levels",                       {9, 0x25, 0, 0, 64, 64, 12, 7, 0, 0},        294912, 24576, 16384, 512, 3, 0},
        {"minimal RG8 260x260, 9 levels (Tears of the Kingdom)",              {1, 0x0d, 0x200, 0, 260, 260, 1, 9, 0, 0},   214528, 214528, 152064, 512, 0, 0},
        {"minimal RGB565 320x320 (Tears of the Kingdom)",                     {1, 0x3c, 0x200, 0, 320, 320, 1, 1, 0, 0},   204800, 204800, 0, 512, 0, 0},
        {"minimal RGBA8 64x128 (no padding: the automatic block kept)",       {1, 0x25, 0x200, 0, 64, 128, 1, 1, 0, 0},    32768, 32768, 0, 512, 4, 0},
        {"minimal R8 8x1920, 11 levels (level 1 would be padded)",            {1, 0x01, 0x200, 0, 8, 1920, 1, 11, 0, 0},   246784, 246784, 122880, 512, 0, 0},
        {"minimal 3D RGBA8 64x64x8",                                         {2, 0x25, 0x200, 0, 64, 64, 8, 1, 0, 0},     131072, 131072, 0, 512, 0, 3},
        {"minimal 3D RGBA8 64x64x9",                                         {2, 0x25, 0x200, 0, 64, 64, 9, 1, 0, 0},     147456, 147456, 0, 512, 0, 0},
        {"packaged 3D BC6H 64^3, 7 levels, byte 0 0x40 (Tears of the Kingdom)", {2, 0x49, 0, 0, 64, 64, 64, 7, 0, 0x140},  163328, 163328, 131072, 512, 0, 4},
        {"packaged 3D RGBA8 32^3, byte 0 0x00",                              {2, 0x25, 0, 0, 32, 32, 32, 1, 0, 0x100},    131072, 131072, 0, 512, 0, 0},
        {"packaged 3D RGBA8 32^3, byte 0 0x12",                              {2, 0x25, 0, 0, 32, 32, 32, 1, 0, 0x112},    131072, 131072, 0, 512, 2, 1},
        {"packaged 2D RGBA8 64x64, byte 0 0x13",                             {1, 0x25, 0, 0, 64, 64, 1, 1, 0, 0x113},     16384, 16384, 0, 512, 3, 0},
        {"linear render target RGBA8 260x260 stride 1152 (Tears of the Kingdom)", {1, 0x25, 0x40, 0, 260, 260, 1, 1, 1152, 0}, 299520, 299520, 0, 128, 0, 0},
        {"linear render target RGBA8 1x1 stride 128 (Tears of the Kingdom)",  {1, 0x25, 0x40, 0, 1, 1, 1, 1, 128, 0},      128, 128, 0, 128, 0, 0},
        {"linear BC1 64x64 stride 128: pixel rows",                          {1, 0x42, 0x10, 0, 64, 64, 1, 1, 128, 0},    8192, 8192, 0, 32, 0, 0},
    };
    for (unsigned i = 0; i < sizeof G / sizeof *G; i++) {
        nvn_texlayout L; nvn_tex_layout(&G[i].d, &L); char what[160];
        uint64_t off1 = G[i].d.levels > 1 ? nvn_tex_level_offset(&G[i].d, 1) : 0;
        snprintf(what, sizeof what, "%s: size", G[i].what); expect(what, G[i].size, L.size);
        snprintf(what, sizeof what, "%s: layer", G[i].what); expect(what, G[i].layer, L.layer_size);
        snprintf(what, sizeof what, "%s: level 1 offset", G[i].what); expect(what, G[i].off1, off1);
        snprintf(what, sizeof what, "%s: alignment", G[i].what); expect(what, G[i].align, L.align);
        snprintf(what, sizeof what, "%s: block height / depth", G[i].what); expect(what, G[i].th * 16 + G[i].td, L.tileH * 16 + L.tileD);
    }
    printf("driver vectors: %u checked\n", (unsigned)(sizeof G / sizeof *G));
    // Eight MINIMAL_LAYOUT sizes and block shifts from Tears of the Kingdom 1.4.3's SDK as totk-mobile recorded them (tests/test_nvn_objects.c,
    // Apache-2.0, the numbers only), a packaged layout over MINIMAL_LAYOUT, and its grass height map (R16 260x260, LINEAR_RENDER_TARGET, stride 640)
    struct { unsigned target, w, h, depth, levels, format, th, td; uint64_t size; } M[] = {
        {1, 320, 320, 1, 1, 0x3b, 0, 0, 204800}, {1, 260, 260, 1, 9, 0x0d, 0, 0, 214528}, {1, 128, 128, 1, 1, 0x3b, 4, 0, 32768}, {1, 256, 320, 1, 1, 0x3b, 0, 0, 163840},
        {1, 321, 321, 1, 1, 0x3b, 0, 0, 230912}, {2, 64, 64, 13, 1, 0x25, 0, 0, 212992}, {4, 320, 320, 6, 1, 0x3b, 0, 0, 1228800}, {1, 9, 5, 1, 1, 0x01, 0, 0, 512},
    };
    for (unsigned i = 0; i < sizeof M / sizeof *M; i++) {
        nvn_texdesc d = {.target = (int)M[i].target, .format = (int)M[i].format, .flags = 0x200, .width = M[i].w, .height = M[i].h, .depth = M[i].depth, .levels = M[i].levels};
        nvn_texlayout L; nvn_tex_layout(&d, &L); char what[120];
        snprintf(what, sizeof what, "TotK SDK minimal %ux%ux%u format %#x target %u: size", M[i].w, M[i].h, M[i].depth, M[i].format, M[i].target); expect(what, M[i].size, L.size);
        snprintf(what, sizeof what, "TotK SDK minimal %ux%ux%u format %#x target %u: block", M[i].w, M[i].h, M[i].depth, M[i].format, M[i].target); expect(what, M[i].th * 16 + M[i].td, L.tileH * 16 + L.tileD);
    }
    { nvn_texdesc d = {.target = 1, .format = 0x3b, .flags = 0x200, .width = 320, .height = 320, .depth = 1, .levels = 1, .pkg_layout = 0x104}; nvn_texlayout L; nvn_tex_layout(&d, &L);
      expect("a packaged layout (block 16) wins over MINIMAL_LAYOUT: size", 245760, L.size); expect("... its block height", 4, L.tileH);
      nvn_texdesc g = {.target = 1, .format = 0x06, .flags = 0x40, .width = 260, .height = 260, .depth = 1, .levels = 1, .stride = 640}; nvn_tex_layout(&g, &L);
      expect("grass height map R16 260x260 LINEAR_RENDER_TARGET stride 640: size", 166400, L.size); expect("... alignment", 128, L.align); }
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
