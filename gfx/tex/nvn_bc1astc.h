// BC1 blocks as ASTC 4x4 blocks, for GPUs without BC (plan item 62c; the method of MacNativeGames gfx/tex/nvn_bc1astc.c, MIT, written anew).
// Decompressed, a BC1 texture takes 8 times its size (RGBA8); as ASTC 4x4 twice. Tears of the Kingdom's colour textures are mostly BC1.
//   four colours (c0 > c1): the palette 0, 1/3, 2/3, 1 is ASTC's 2-bit weight set, so the block comes out the same up to the endpoints'
//     rounding (RGB direct endpoints, CEM 8)
//   three colours, index 3 unused: the palette 0, 1/2, 1 is ASTC's three-level (trit) weight set, so these come out the same too
//   three colours with index 3 (transparent black in the RGBA formats, opaque black in the RGB ones): dual plane RGBA with 1-bit weights,
//     alpha exact, the colour the nearer of the two endpoints picked from the block's colours with the least error
// The same arithmetic runs on the GPU (nvn_mtl.m k_b1a_msl); gfx/tests/t_bc1astc.m holds both to each other and to bcdec through Metal's ASTC
// decoder.
#pragma once
#include <stdint.h>

// One block: 8 bytes of BC1 to 16 bytes of ASTC. rgba: 1 for RGBA_DXT1 (index 3 of a three-colour block is transparent), 0 for RGB_DXT1
void nvn_bc1_astc(const uint8_t bc1[8], uint8_t astc[16], int rgba);
// bw x bh blocks: rows of BC1 blocks pitch bytes apart to tightly packed ASTC blocks
void nvn_bc1_astc_image(const uint8_t *src, uint64_t pitch, uint32_t bw, uint32_t bh, uint8_t *dst, int rgba);
// The NVN format is BC1 (0x41 / 0x45 RGB, 0x42 / 0x46 RGBA): 1, *rgba and *srgb set
int nvn_bc1_astc_fmt(uint32_t nvn_fmt, int *rgba, int *srgb);
