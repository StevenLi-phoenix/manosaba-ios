// See nvn_bc1astc.h. The ASTC blocks written: one partition, a 4x4 weight grid, 8-bit endpoints (with these weight counts the endpoints always
// get 8 bits, so their integer sequence is plain bits):
//   bits 0-10   block mode: 0x042 2-bit weights (4 levels), 0x051 weights in 0..2 (3 levels, trits), 0x441 dual plane with 1-bit weights
//   bits 11-12  partitions - 1 = 0;   bits 13-16  colour endpoint mode: 8 RGB direct, 12 RGBA direct
//   bits 17-    the endpoint values r0 r1 g0 g1 b0 b1 (a0 a1), 8 bits each
//   from bit 127 down: the weights, texel y * 4 + x (dual plane: plane 0 then plane 1 for each texel), each weight's bits from its lowest; trits
//     five to 8 bits (k_trit: the inverse of the specification's trit decoding), the last one of 16 in 2 bits; dual plane: the 2-bit colour
//     component selector (3: alpha on plane 1) just below them
// Unquantised, 2-bit weights are 0, 21, 43, 64 and trits 0, 32, 64: BC1's four-colour palette (0, 1/3, 2/3, 1) on the first, its three-colour
// one (0, 1/2, 1) on the second. When the second endpoint's r + g + b is below the first's, the decoder uses blue contraction: the endpoints are
// kept in the other order, the weights turned over.
#include "nvn_bc1astc.h"
#include <string.h>

static const uint8_t k_trit[243] = {0,1,2,4,5,6,8,9,10,16,17,18,20,21,22,24,25,26,3,7,11,19,23,27,12,13,14,32,33,34,36,37,38,40,41,42,48,49,50,52,53,54,56,57,58,35,39,43,51,55,59,44,45,46,64,65,66,68,69,70,72,73,74,80,81,82,84,85,86,88,89,90,67,71,75,83,87,91,76,77,78,128,129,130,132,133,134,136,137,138,144,145,146,148,149,150,152,153,154,131,135,139,147,151,155,140,141,142,160,161,162,164,165,166,168,169,170,176,177,178,180,181,182,184,185,186,163,167,171,179,183,187,172,173,174,192,193,194,196,197,198,200,201,202,208,209,210,212,213,214,216,217,218,195,199,203,211,215,219,204,205,206,96,97,98,100,101,102,104,105,106,112,113,114,116,117,118,120,121,122,99,103,107,115,119,123,108,109,110,224,225,226,228,229,230,232,233,234,240,241,242,244,245,246,248,249,250,227,231,235,243,247,251,236,237,238,28,29,30,60,61,62,92,93,94,156,157,158,188,189,190,220,221,222,31,63,95,159,191,223,124,125,126};
static void bits(uint8_t *o, unsigned at, unsigned n, unsigned v) { for (unsigned i = 0; i < n; i++) if (v >> i & 1) o[(at + i) >> 3] |= (uint8_t)(1u << ((at + i) & 7)); }
static void wbits(uint8_t *o, unsigned at, unsigned n, unsigned v) { for (unsigned i = 0; i < n; i++) if (v >> i & 1) { unsigned b = 127 - (at + i); o[b >> 3] |= (uint8_t)(1u << (b & 7)); } }
// lv: weight levels, 2 (1 bit, dual plane), 3 (trits) or 4 (2 bits)
static void block(uint8_t *o, int cem, int dual, int lv, int *e0, int *e1, int *w) {
    int nw = dual ? 32 : 16, nc = cem == 12 ? 4 : 3;
    if (e1[0] + e1[1] + e1[2] < e0[0] + e0[1] + e0[2]) {
        for (int c = 0; c < 4; c++) { int t = e0[c]; e0[c] = e1[c]; e1[c] = t; }
        for (int i = 0; i < nw; i++) w[i] = lv - 1 - w[i];
    }
    memset(o, 0, 16);
    bits(o, 0, 11, dual ? 0x441u : lv == 4 ? 0x042u : 0x051u);
    bits(o, 13, 4, (unsigned)cem);
    for (int c = 0; c < nc; c++) { bits(o, 17 + 16 * (unsigned)c, 8, (unsigned)e0[c]); bits(o, 25 + 16 * (unsigned)c, 8, (unsigned)e1[c]); }
    if (lv == 3) for (int g = 0; g * 5 < nw; g++) {
        int k = 0, m = 1, left = nw - g * 5;
        for (int j = 0; j < 5; j++, m *= 3) k += (j < left ? w[g * 5 + j] : 0) * m;
        wbits(o, 8 * (unsigned)g, left >= 5 ? 8 : (unsigned)(8 * left + 4) / 5, k_trit[k]);
    }
    else for (int i = 0; i < nw; i++) wbits(o, (unsigned)(i * (lv == 4 ? 2 : 1)), lv == 4 ? 2 : 1, (unsigned)w[i]);
    if (dual) bits(o, 128 - (unsigned)nw - 2, 2, 3);
}
static int x5(int v) { return v << 3 | v >> 2; }
static int x6(int v) { return v << 2 | v >> 4; }

void nvn_bc1_astc(const uint8_t *b, uint8_t *o, int rgba) {
    int c0 = b[0] | b[1] << 8, c1 = b[2] | b[3] << 8, idx[16], used = 0, w[32];
    unsigned ix = (unsigned)b[4] | (unsigned)b[5] << 8 | (unsigned)b[6] << 16 | (unsigned)b[7] << 24;
    int p[4][3] = {{x5(c0 >> 11), x6(c0 >> 5 & 63), x5(c0 & 31)}, {x5(c1 >> 11), x6(c1 >> 5 & 63), x5(c1 & 31)}, {0, 0, 0}, {0, 0, 0}};
    for (int k = 0; k < 16; k++) { idx[k] = (int)(ix >> (2 * k) & 3); used |= 1 << idx[k]; }
#ifndef DM_TEST_B1A_FOUR_ORDER   // negative control of t_bc1astc only: the four-colour palette taken in index order
    static const int four[4] = {0, 3, 1, 2};   // c0, c1, 2/3 c0 + 1/3 c1, 1/3 c0 + 2/3 c1 on the weights 0, 3, 1, 2
#else
    static const int four[4] = {0, 1, 2, 3};
#endif
    static const int three[3] = {0, 2, 1};     // c0, c1, the midpoint on the trits 0, 2, 1
    int e0[4] = {p[0][0], p[0][1], p[0][2], 255}, e1[4] = {p[1][0], p[1][1], p[1][2], 255};
    if (c0 > c1 || !(used & 8)) {
        for (int k = 0; k < 16; k++) w[k] = c0 > c1 ? four[idx[k]] : three[idx[k]];
        block(o, 8, 0, c0 > c1 ? 4 : 3, e0, e1, w); return;
    }
    // index 3 used: two of the block's colours as the colour endpoints (black among them in the RGB formats), alpha on its own plane
    for (int c = 0; c < 3; c++) p[2][c] = (p[0][c] + p[1][c] + 1) / 2;
    int colours = used & (rgba ? 7 : 15), best = -1, ea = 0, eb = 0;
    for (int a = 0; a < 4; a++) for (int c = a; c < 4; c++) {
        if (!(colours >> a & 1) || !(colours >> c & 1)) continue;
        int err = 0;
        for (int k = 0; k < 16; k++) {
            int i = idx[k], da = 0, dc = 0; if (!(colours >> i & 1)) continue;
            for (int ch = 0; ch < 3; ch++) { int x = p[i][ch] - p[a][ch], y = p[i][ch] - p[c][ch]; da += x * x; dc += y * y; }
            err += da < dc ? da : dc;
        }
        if (best < 0 || err < best) { best = err; ea = a; eb = c; }
    }
    int f0[4] = {p[ea][0], p[ea][1], p[ea][2], 255}, f1[4] = {p[eb][0], p[eb][1], p[eb][2], rgba ? 0 : 255};
    for (int k = 0; k < 16; k++) {
        int i = idx[k], near1 = 0;
        if (colours >> i & 1) { int da = 0, dc = 0; for (int ch = 0; ch < 3; ch++) { int x = p[i][ch] - f0[ch], y = p[i][ch] - f1[ch]; da += x * x; dc += y * y; } near1 = dc < da; }
        w[2 * k] = near1; w[2 * k + 1] = rgba && i == 3;
    }
    block(o, 12, 1, 2, f0, f1, w);
}
void nvn_bc1_astc_image(const uint8_t *src, uint64_t pitch, uint32_t bw, uint32_t bh, uint8_t *dst, int rgba) {
    for (uint32_t y = 0; y < bh; y++) for (uint32_t x = 0; x < bw; x++) nvn_bc1_astc(src + y * pitch + x * 8, dst + ((uint64_t)y * bw + x) * 16, rgba);
}
int nvn_bc1_astc_fmt(uint32_t f, int *rgba, int *srgb) {
    if (f != 0x41 && f != 0x42 && f != 0x45 && f != 0x46) return 0;
    *rgba = f == 0x42 || f == 0x46; *srgb = f >= 0x45; return 1;
}
