// A single-draw render pass as two compute dispatches (plan item 217): the host's half of gfx/tools/dmsh_kernel.py's kernels, shared by nvn_mtl.m
// (DM_NVN_KPASS) and gfx/tests/t_kpass.m. The parameter blocks below have the layout of tk_V / tk_F in the kernels' MSL (the test checks the
// kernels against Metal's own rasterization with them); helpers turn a draw's primitive, viewport, scissor, target and colour state into them.
#pragma once
#include <stddef.h>
#include <stdint.h>

#define KP_TKP 29        // buffer index of the parameters
#define KP_TKR 28        // buffer index of the vertex records
#define KP_TKO 20        // texture index of colour output 0
#define KP_TKS 33        // float4 a vertex record: the position, then locations 0-31
#define KP_TKV 27        // buffer index of the samples-passed count (a uint added to while kp_f.vis)
#define KP_MAX_SIZE 256  // targets up to this many pixels each way

typedef struct { uint64_t p; uint32_t offset, stride, format, step; } kp_va;   // step 0 per vertex, 1 constant
typedef struct { kp_va a[16]; uint64_t ib; uint32_t itype, count, first, base_vertex, base_instance, pad[2]; } kp_v;
typedef struct {
    float vp[4], dr[2];
    uint32_t org[2], lim[2], qw, nq, ntri, cull, front_cw, zclip, lo[2], vis, pad;
    uint32_t tri[2][4], bl[8][4];
    float bc[4];
} kp_f;
_Static_assert(sizeof(kp_va) == 24 && offsetof(kp_v, ib) == 384 && sizeof(kp_v) == 424, "kp_v layout (tk_V)");
_Static_assert(offsetof(kp_f, org) == 24 && offsetof(kp_f, lo) == 64 && offsetof(kp_f, tri) == 80 && offsetof(kp_f, bl) == 112 && offsetof(kp_f, bc) == 240 && sizeof(kp_f) == 256, "kp_f layout (tk_F)");

// The triangles of a draw of count vertices (NVN / Metal primitive: 4 triangles, 5 triangle strip) as record indices, in an order that keeps
// each one's winding, and the provoking vertex (Metal's: the first of the triangle as drawn) in w; the number of triangles, 0 when the draw is
// none the kernels take (other primitives, more than two triangles)
static inline int kp_tris(int prim, uint32_t count, uint32_t tri[2][4]) {
    if (prim == 4 && (count == 3 || count == 6)) {
        for (uint32_t t = 0; t < count / 3; t++) { tri[t][0] = 3 * t; tri[t][1] = 3 * t + 1; tri[t][2] = 3 * t + 2; tri[t][3] = 3 * t; }
        return (int)count / 3;
    }
    if (prim == 5 && (count == 3 || count == 4)) {
        tri[0][0] = 0; tri[0][1] = 1; tri[0][2] = 2; tri[0][3] = 0;
        if (count == 4) {   // the strip's second triangle (1, 2, 3) has the other winding as listed: (2, 1, 3)
#ifndef DM_TEST_KPASS_STRIP_AS_LISTED   // negative control of t_kpass only
            tri[1][0] = 2; tri[1][1] = 1;
#else
            tri[1][0] = 1; tri[1][1] = 2;
#endif
            tri[1][2] = 3; tri[1][3] = 1;
        }
        return (int)count - 2;
    }
    return 0;
}
// The pixels a draw can write: the target's level (tw x th), the viewport's (pixel centres inside it) and the scissor's (x, y, w, h; NULL none);
// the dispatch rectangle starts at an even pixel and runs whole 2x2 quads. 0 when nothing is left
static inline int kp_rect(kp_f *f, uint32_t tw, uint32_t th, const float vp[4], const uint32_t *sc) {
    double x0 = 0, y0 = 0, x1 = tw, y1 = th;
    double vx0 = vp[0] - 0.5, vy0 = vp[1] - 0.5, vx1 = vp[0] + vp[2] - 0.5, vy1 = vp[1] + vp[3] - 0.5;   // centre px + 0.5 in [v, v + size)
    long ax0 = (long)__builtin_ceil(vx0), ay0 = (long)__builtin_ceil(vy0), ax1 = (long)__builtin_ceil(vx1), ay1 = (long)__builtin_ceil(vy1);
    if (ax0 > x0) x0 = ax0; if (ay0 > y0) y0 = ay0; if (ax1 < x1) x1 = ax1; if (ay1 < y1) y1 = ay1;
    if (sc) { if (sc[0] > x0) x0 = sc[0]; if (sc[1] > y0) y0 = sc[1]; if ((double)sc[0] + sc[2] < x1) x1 = (double)sc[0] + sc[2]; if ((double)sc[1] + sc[3] < y1) y1 = (double)sc[1] + sc[3]; }
    if (x1 <= x0 || y1 <= y0) return 0;
    f->lo[0] = (uint32_t)x0; f->lo[1] = (uint32_t)y0; f->lim[0] = (uint32_t)x1; f->lim[1] = (uint32_t)y1;
    f->org[0] = f->lo[0] & ~1u; f->org[1] = f->lo[1] & ~1u;   // (the quads start at even pixels; those before lo run as helpers)
    f->qw = (f->lim[0] - f->org[0] + 1) / 2; f->nq = f->qw * ((f->lim[1] - f->org[1] + 1) / 2);
    return 1;
}
// MTLPixelFormat → how a kernel writes it: 1 unorm (clamped to 0..1), 2 snorm, 0 float, -1 not taken (integer, sRGB, compressed, depth, unknown)
static inline int kp_fmt_kind(unsigned long pf) {
    switch (pf) {
    case 10: case 20: case 30: case 60: case 70: case 80: case 90: case 110: return 1;                                       // R8, R16, RG8, RG16, RGBA8, BGRA8, RGB10A2, RGBA16 unorm
    case 12: case 22: case 32: case 62: case 72: case 112: return 2;                                                           // snorm
    case 25: case 55: case 65: case 92: case 105: case 115: case 125: return 0;                                                // R16F, R32F, RG16F, RG11B10F, RG32F, RGBA16F, RGBA32F
    default: return -1;
    }
}
// MTLVertexFormat values the vertex kernel fetches (tk_fetch_f for float inputs, tk_fetch_u for integer ones: unsigned and 32-bit formats only)
static inline int kp_vfmt_ok(unsigned long vf, int int_input) {
    if (int_input) switch (vf) { case 45: case 1: case 3: case 49: case 13: case 15: case 36: case 32: case 37: case 33: case 38: case 34: case 39: case 35: return 1; default: return 0; }
    switch (vf) { case 28: case 29: case 30: case 31: case 53: case 25: case 26: case 27: case 47: case 7: case 8: case 9: case 42: case 48: case 10: case 12:
                  case 51: case 19: case 21: case 52: case 22: case 24: case 41: case 45: case 1: case 3: case 49: case 13: case 15: case 36: case 37: case 39: return 1;
                  default: return 0; }
}
// Formats a kernel may read and write (MTLReadWriteTextureTier 1 and 2, Apple's documentation): blending and partial write masks need them
static inline int kp_fmt_rw(unsigned long pf) {
    switch (pf) { case 125: case 123: case 124: case 115: case 113: case 114: case 70: case 73: case 74: case 25: case 23: case 24: case 10: case 13: case 14:
                  case 55: case 53: case 54: return 1; default: return 0; }
}
// Output's blend word set: blend factors / operations as MTLBlendFactor / MTLBlendOperation values, mask 4 bits (r g b a), kind kp_fmt_kind's
static inline void kp_blend(uint32_t b[4], int on, unsigned src_rgb, unsigned dst_rgb, unsigned src_a, unsigned dst_a, unsigned op_rgb, unsigned op_a, unsigned mask, int kind) {
    b[0] = src_rgb | dst_rgb << 8 | src_a << 16 | dst_a << 24; b[1] = op_rgb | op_a << 8; b[2] = (on ? 1u : 0u) | (mask & 15) << 4; b[3] = kind > 0 ? (uint32_t)kind : 0;
}
