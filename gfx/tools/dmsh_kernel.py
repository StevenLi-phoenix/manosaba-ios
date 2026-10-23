#!/usr/bin/env python3
# Compute versions of translated vertex / fragment stages (plan item 217, after nxport's crates/build/src/tiny_kernels.rs, MIT, written anew): a render
# pass costs a fixed 16-23 µs on the M5 and more on an iPhone, a dispatch about a tenth of that, and Tears of the Kingdom draws about 70 single-draw
# passes of up to 256x256 a frame. For such a pass the host (gfx/nvn/nvn_mtl.m, DM_NVN_KPASS) runs the vertex stage's kernel for the draw's few
# vertices, which writes them to a buffer, then the fragment stage's kernel over the target's rectangle: coverage and interpolation from those
# vertices, the stage's own code, blending and write masks. The entry point becomes an inline function the kernel calls (resources keep their types
# and lose their attributes), so the stage's code is the chain's MSL as it is. Output: <key>k.metal beside the Mac library and <key>k.metallib for
# the Mac and, with an iOS directory, for iOS (ios_metal.py's flags); kernels tk_vert (vertex) or tk_frag_w / tk_frag_rw (fragment; outputs written,
# or read too for blending and write masks).
#
# Fragment kernel: one thread a pixel, dispatched as quads: each four threads of a quadgroup (thread_index_in_quadgroup) shade a 2x2 block, so an
# implicit-LOD sample (sample / sample_compare without lod options) gets gradients from the block's coordinates (quad_shuffle_xor), as a fragment
# quad's would be; pixels the triangles do not cover in a covered block run as helpers on the extrapolated interpolants, writing nothing. Coverage:
# vertices snapped to 1/256 pixel and integer edge functions with a tie rule (TK_TIE: the rule gfx/tests/t_kpass.m finds Metal to use), the
# viewport rectangle, the scissor and, with depth clipping, 0 <= z <= 1; a triangle with a vertex at w <= 0 is rasterized in homogeneous coordinates
# (no tie rule). Interpolation as the user(locnN) qualifiers say (perspective, center_no_perspective, flat from the provoking vertex).
# Declined (no file; the reason printed): discard, derivatives, depth / sample-mask output, framebuffer reads, quad / simd operations, atomics, device
# writes, clip distances, builtins other than position / vertex_id / instance_id / base_vertex / base_instance, function-constant inputs.
#
# Interface with the host (tk_V / tk_F below; the host fills them): buffer(TKP) the parameters, buffer(TKR) the vertex records (TKS float4 a vertex:
# [0] the position, [1 + L] location L as stored, bits for integers), texture(TKO + N) colour output N, buffer(TKV) the samples-passed count (a
# uint the pixels covered are added to while the game counts occlusion: tk_F.vis).
# Usage: dmsh_kernel.py <key list> <Mac b-V dir> [<iOS b-V dir>]    (keys one a line, # comments; exit 1 when a listed key has no source)
#        dmsh_kernel.py --msl <stage .metal> <kernel .metal>
import os, re, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

TKP, TKR, TKO, TKS, TKV = 29, 28, 20, 33, 27
TK_TIE = 1 if os.environ.get('DM_TEST_KERNEL_TIE') == '1' else 0   # 0: top-left in window coordinates, y down (D3D's): an edge at a pixel centre counts
# when it is a top edge or a left edge, which t_kpass finds Metal to do; 1 the opposite (the env: negative control of t_kpass only)

COMMON = r'''
#ifndef TK_COMMON
#define TK_COMMON
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-variable"
struct tk_VA { device const uchar *p; uint offset, stride, format, step; };   // a vertex attribute: step 0 per vertex, 1 constant
struct tk_V { tk_VA a[16]; device const uchar *ib; uint itype, count, first, base_vertex, base_instance, pad[2]; };
struct tk_F {
    float4 vp;            // viewport x, y, width, height
    float2 dr;            // depth range near, far
    uint2 org;            // the dispatch's first pixel (lo rounded down to even)
    uint2 lim;            // pixels written: lo <= px < lim (the target's level, the viewport and the scissor)
    uint qw, nq;          // quads a row, quads
    uint ntri, cull;      // triangles (1-2); cull 0 none, 1 front, 2 back
    uint front_cw, zclip; // front faces clockwise in window coordinates (y down); clip at 0 <= z <= 1
    uint2 lo;             // the first pixel written
    uint vis, pad;        // vis: add the pixels shaded to the samples-passed count at buffer(TKV) (occlusion counting)
    uint4 tri[2];         // the vertex records of each triangle, in the order that keeps its winding; w = the provoking vertex's record
    uint4 bl[8];          // per output: x = MTLBlendFactor src rgb | dst rgb << 8 | src a << 16 | dst a << 24, y = MTLBlendOperation rgb | a << 8,
                          // z = bit 0 blending, bits 4-7 the write mask (r g b a), w = 1 unorm (sources clamped to 0..1), 2 snorm (-1..1), 0 other
    float4 bc;            // blend colour
};
static inline uint tk_u8(device const uchar *p) { return p[0]; }
static inline uint tk_u16(device const uchar *p) { return uint(p[0]) | uint(p[1]) << 8; }
static inline uint tk_u32(device const uchar *p) { return uint(p[0]) | uint(p[1]) << 8 | uint(p[2]) << 16 | uint(p[3]) << 24; }
// One attribute as the vertex fetch would give it: float4 (normalized and float formats; integer formats as their values), missing components 0, 0, 0, 1;
// no format: zeros (the render path binds an unset attribute to its zero buffer)
static inline float4 tk_fetch_f(constant tk_VA &a, uint v) {
    float4 r = float4(0, 0, 0, 1);
    if (!a.format) return float4(0);
    device const uchar *p = a.p + a.offset + (a.step ? 0u : v * a.stride);
    switch (a.format) {
    case 28: r.x = as_type<float>(tk_u32(p)); break;
    case 29: r.xy = float2(as_type<float>(tk_u32(p)), as_type<float>(tk_u32(p + 4))); break;
    case 30: r.xyz = float3(as_type<float>(tk_u32(p)), as_type<float>(tk_u32(p + 4)), as_type<float>(tk_u32(p + 8))); break;
    case 31: r = float4(as_type<float>(tk_u32(p)), as_type<float>(tk_u32(p + 4)), as_type<float>(tk_u32(p + 8)), as_type<float>(tk_u32(p + 12))); break;
    case 53: r.x = float(as_type<half>(ushort(tk_u16(p)))); break;
    case 25: r.xy = float2(as_type<half>(ushort(tk_u16(p))), as_type<half>(ushort(tk_u16(p + 2)))); break;
    case 26: r.xyz = float3(as_type<half>(ushort(tk_u16(p))), as_type<half>(ushort(tk_u16(p + 2))), as_type<half>(ushort(tk_u16(p + 4)))); break;
    case 27: r = float4(as_type<half>(ushort(tk_u16(p))), as_type<half>(ushort(tk_u16(p + 2))), as_type<half>(ushort(tk_u16(p + 4))), as_type<half>(ushort(tk_u16(p + 6)))); break;
    case 47: r.x = tk_u8(p) / 255.0; break;
    case 7: r.xy = float2(tk_u8(p), tk_u8(p + 1)) / 255.0; break;
    case 8: r.xyz = float3(tk_u8(p), tk_u8(p + 1), tk_u8(p + 2)) / 255.0; break;
    case 9: r = float4(tk_u8(p), tk_u8(p + 1), tk_u8(p + 2), tk_u8(p + 3)) / 255.0; break;
    case 42: r = float4(tk_u8(p + 2), tk_u8(p + 1), tk_u8(p), tk_u8(p + 3)) / 255.0; break;
    case 48: r.x = max(float(as_type<char>(uchar(tk_u8(p)))) / 127.0, -1.0); break;
    case 10: r.xy = max(float2(as_type<char>(uchar(tk_u8(p))), as_type<char>(uchar(tk_u8(p + 1)))) / 127.0, -1.0); break;
    case 12: r = max(float4(as_type<char>(uchar(tk_u8(p))), as_type<char>(uchar(tk_u8(p + 1))), as_type<char>(uchar(tk_u8(p + 2))), as_type<char>(uchar(tk_u8(p + 3)))) / 127.0, -1.0); break;
    case 51: r.x = tk_u16(p) / 65535.0; break;
    case 19: r.xy = float2(tk_u16(p), tk_u16(p + 2)) / 65535.0; break;
    case 21: r = float4(tk_u16(p), tk_u16(p + 2), tk_u16(p + 4), tk_u16(p + 6)) / 65535.0; break;
    case 52: r.x = max(float(as_type<short>(ushort(tk_u16(p)))) / 32767.0, -1.0); break;
    case 22: r.xy = max(float2(as_type<short>(ushort(tk_u16(p))), as_type<short>(ushort(tk_u16(p + 2)))) / 32767.0, -1.0); break;
    case 24: r = max(float4(as_type<short>(ushort(tk_u16(p))), as_type<short>(ushort(tk_u16(p + 2))), as_type<short>(ushort(tk_u16(p + 4))), as_type<short>(ushort(tk_u16(p + 6)))) / 32767.0, -1.0); break;
    case 41: { uint w = tk_u32(p); r = float4(w & 1023, (w >> 10) & 1023, (w >> 20) & 1023, w >> 30) / float4(1023, 1023, 1023, 3); break; }
    case 45: r.x = tk_u8(p); break;
    case 1: r.xy = float2(tk_u8(p), tk_u8(p + 1)); break;
    case 3: r = float4(tk_u8(p), tk_u8(p + 1), tk_u8(p + 2), tk_u8(p + 3)); break;
    case 49: r.x = tk_u16(p); break;
    case 13: r.xy = float2(tk_u16(p), tk_u16(p + 2)); break;
    case 15: r = float4(tk_u16(p), tk_u16(p + 2), tk_u16(p + 4), tk_u16(p + 6)); break;
    case 36: r.x = tk_u32(p); break;
    case 37: r.xy = float2(tk_u32(p), tk_u32(p + 4)); break;
    case 39: r = float4(tk_u32(p), tk_u32(p + 4), tk_u32(p + 8), tk_u32(p + 12)); break;
    default: break;
    }
    return r;
}
static inline uint4 tk_fetch_u(constant tk_VA &a, uint v) {
    uint4 r = uint4(0, 0, 0, 1);
    if (!a.format) return uint4(0);
    device const uchar *p = a.p + a.offset + (a.step ? 0u : v * a.stride);
    switch (a.format) {
    case 45: r.x = tk_u8(p); break;
    case 1: r.xy = uint2(tk_u8(p), tk_u8(p + 1)); break;
    case 3: r = uint4(tk_u8(p), tk_u8(p + 1), tk_u8(p + 2), tk_u8(p + 3)); break;
    case 49: r.x = tk_u16(p); break;
    case 13: r.xy = uint2(tk_u16(p), tk_u16(p + 2)); break;
    case 15: r = uint4(tk_u16(p), tk_u16(p + 2), tk_u16(p + 4), tk_u16(p + 6)); break;
    case 36: case 32: r.x = tk_u32(p); break;
    case 37: case 33: r.xy = uint2(tk_u32(p), tk_u32(p + 4)); break;
    case 38: case 34: r.xyz = uint3(tk_u32(p), tk_u32(p + 4), tk_u32(p + 8)); break;
    case 39: case 35: r = uint4(tk_u32(p), tk_u32(p + 4), tk_u32(p + 8), tk_u32(p + 12)); break;
    default: break;
    }
    return r;
}
static inline uint tk_index(constant tk_V &P, uint i) {
    if (!P.ib) return P.first + i;
    uint k = P.first + i;
    uint x = P.itype == 0 ? tk_u8(P.ib + k) : P.itype == 1 ? tk_u16(P.ib + 2 * k) : tk_u32(P.ib + 4 * k);
    return x + P.base_vertex;
}
template<typename T> static inline T tk_ddx(T v, ushort l) { T o = quad_shuffle_xor(v, ushort(1)); return (l & 1) ? v - o : o - v; }
template<typename T> static inline T tk_ddy(T v, ushort l) { T o = quad_shuffle_xor(v, ushort(2)); return (l & 2) ? v - o : o - v; }
// Coverage and the perspective-correct weights of pixel centre p (window coordinates) in triangle t; z = window depth, iw = 1 / w (as the
// fragment stage's position.w), lin = screen-linear weights (for noperspective). Returns 1 inside, 0 outside (weights extrapolated)
static inline int tk_tri(constant tk_F &P, const device float4 *R, uint t, float2 p, thread float3 &pw, thread float3 &lin, thread float &z, thread float &iw) {
    float4 c0 = R[P.tri[t].x * TKS], c1 = R[P.tri[t].y * TKS], c2 = R[P.tri[t].z * TKS];
    if (c0.w > 0 && c1.w > 0 && c2.w > 0) {
        float3 nx = float3(c0.x / c0.w, c1.x / c1.w, c2.x / c2.w), ny = float3(c0.y / c0.w, c1.y / c1.w, c2.y / c2.w), nz = float3(c0.z / c0.w, c1.z / c1.w, c2.z / c2.w);
        float3 wx = (nx * 0.5 + 0.5) * P.vp.z + P.vp.x, wy = (0.5 - ny * 0.5) * P.vp.w + P.vp.y;
        long3 X = long3(rint(wx * 256.0)), Y = long3(rint(wy * 256.0));
        long px = long(rint(p.x * 256.0)), py = long(rint(p.y * 256.0));
        long A = (X.y - X.x) * (Y.z - Y.x) - (X.z - X.x) * (Y.y - Y.x);
        if (A == 0) return 0;
        long e0 = (X.z - X.y) * (py - Y.y) - (Y.z - Y.y) * (px - X.y);   // edge v1 -> v2, opposite v0
        long e1 = (X.x - X.z) * (py - Y.z) - (Y.x - Y.z) * (px - X.z);   // edge v2 -> v0, opposite v1
        long e2 = (X.y - X.x) * (py - Y.x) - (Y.y - Y.x) * (px - X.x);   // edge v0 -> v1, opposite v2
        float fa = float(A);
        lin = float3(float(e0), float(e1), float(e2)) / fa;
        float3 k = lin / float3(c0.w, c1.w, c2.w); iw = k.x + k.y + k.z; pw = k / iw;
        z = dot(lin, nz * (P.dr.y - P.dr.x) + P.dr.x);
        int cw = A > 0;   // clockwise as seen in window coordinates (y down)
        if (P.cull && (cw == int(P.front_cw)) == (P.cull == 1)) return 0;
        if (P.zclip) { float zn = dot(lin, nz); if (zn < 0 || zn > 1) return 0; }
        long s = A > 0 ? 1 : -1; e0 *= s; e1 *= s; e2 *= s;
        // the tie rule on an edge through the centre, by the edge's direction in the triangle made clockwise
        long2 d0 = s * long2(X.z - X.y, Y.z - Y.y), d1 = s * long2(X.x - X.z, Y.x - Y.z), d2 = s * long2(X.y - X.x, Y.y - Y.x);
        bool t0 = (d0.y == 0 && d0.x > 0) || d0.y < 0, t1 = (d1.y == 0 && d1.x > 0) || d1.y < 0, t2 = (d2.y == 0 && d2.x > 0) || d2.y < 0;
#if TK_TIE == 1
        t0 = !t0; t1 = !t1; t2 = !t2;
#endif
        return (e0 > 0 || (e0 == 0 && t0)) && (e1 > 0 || (e1 == 0 && t1)) && (e2 > 0 || (e2 == 0 && t2));
    }
    // a vertex at w <= 0: homogeneous rasterization (the pixel's normalized device position against the clip-space vertices)
    float2 n = float2((p.x - P.vp.x) / P.vp.z * 2.0 - 1.0, 1.0 - (p.y - P.vp.y) / P.vp.w * 2.0);
    float3x3 M = float3x3(float3(c0.x, c0.y, c0.w), float3(c1.x, c1.y, c1.w), float3(c2.x, c2.y, c2.w));
    float d = determinant(M);
    if (d == 0) return 0;
    float3 k = transpose(float3x3(cross(M[1], M[2]), cross(M[2], M[0]), cross(M[0], M[1]))) * float3(n, 1.0) / d;
    iw = k.x + k.y + k.z; if (iw <= 0) return 0;
    pw = k / iw; lin = pw;
    float zc = dot(pw, float3(c0.z, c1.z, c2.z)), wc = 1.0 / iw;
    z = zc / wc * (P.dr.y - P.dr.x) + P.dr.x;
    int cw = d < 0;
    if (P.cull && (cw == int(P.front_cw)) == (P.cull == 1)) return 0;
    if (P.zclip && (zc < 0 || zc > wc)) return 0;
    return k.x >= 0 && k.y >= 0 && k.z >= 0;
}
static inline float4 tk_factor(uint f, float4 s, float4 d, float4 k) {
    switch (f) {
    case 0: return float4(0); case 1: return float4(1); case 2: return s; case 3: return 1 - s; case 4: return float4(s.a); case 5: return float4(1 - s.a);
    case 6: return d; case 7: return 1 - d; case 8: return float4(d.a); case 9: return float4(1 - d.a);
    case 10: return float4(float3(min(s.a, 1 - d.a)), 1); case 11: return k; case 12: return 1 - k; case 13: return float4(k.a); case 14: return float4(1 - k.a);
    default: return float4(1);
    }
}
static inline float4 tk_op(uint op, float4 a, float4 b) {
    switch (op) { case 1: return a - b; case 2: return b - a; case 3: return min(a, b); case 4: return max(a, b); default: return a + b; }
}
// Output N's value s over the destination d as the colour attachment's blend state and write mask say (min / max ignore the factors, as Metal)
static inline float4 tk_blend(uint4 b, float4 s, float4 d, float4 k) {
    if (b.w == 1) s = saturate(s); else if (b.w == 2) s = clamp(s, -1.0, 1.0);
    float4 r = s;
    if (b.z & 1) {
        float4 fs = float4(tk_factor(b.x & 255, s, d, k).rgb, tk_factor((b.x >> 16) & 255, s, d, k).a);
        float4 fd = float4(tk_factor((b.x >> 8) & 255, s, d, k).rgb, tk_factor(b.x >> 24, s, d, k).a);
        uint orgb = b.y & 255, oa = (b.y >> 8) & 255;
        float3 crgb = orgb >= 3 ? tk_op(orgb, s, d).rgb : tk_op(orgb, s * fs, d * fd).rgb;
        float ca = oa >= 3 ? tk_op(oa, s, d).a : tk_op(oa, s * fs, d * fd).a;
        r = float4(crgb, ca);
    }
    uint m = (b.z >> 4) & 15;
    return select(d, r, bool4(m & 1, m & 2, m & 4, m & 8));
}
#endif
'''.replace('TKS', str(TKS))

DECLINE_BODY = [(r'\bdiscard_fragment\s*\(', 'discard'), (r'\b(dfdx|dfdy|fwidth)\s*\(', 'derivative'), (r'\bcalculate_(un)?clamped_lod\s*\(', 'lod query'),
                (r'\b(simd|quad)_\w+\s*\(', 'simd / quad operation'), (r'\batomic_\w+', 'atomic'), (r'\bthreadgroup\b', 'threadgroup memory')]
TEX_LEAD = {   # arguments before the lod options: sample, sample_compare
    'texture2d': (2, None), 'texture3d': (2, None), 'texturecube': (2, None), 'texture2d_array': (3, None), 'texturecube_array': (3, None),
    'depth2d': (2, 3), 'depthcube': (2, 3), 'depth2d_array': (3, 4), 'depthcube_array': (3, 4)}
GRAD = {'texture2d': 'gradient2d', 'texture2d_array': 'gradient2d', 'depth2d': 'gradient2d', 'depth2d_array': 'gradient2d', 'texture3d': 'gradient3d',
        'texturecube': 'gradientcube', 'texturecube_array': 'gradientcube', 'depthcube': 'gradientcube', 'depthcube_array': 'gradientcube'}
LOD_OPT = re.compile(r'^\s*(level|bias|gradient2d|gradient3d|gradientcube|min_lod_clamp)\s*\(')

class Decline(Exception): pass

def split_top(s):   # the comma-separated parts of s outside (), [], <> and {}
    out, depth, cur = [], 0, ''
    for ch in s:
        if ch in '([{<': depth += 1
        elif ch in ')]}>': depth -= 1
        if ch == ',' and depth == 0: out.append(cur); cur = ''
        else: cur += ch
    if cur.strip(): out.append(cur)
    return [x.strip() for x in out]

def struct_fields(src, name):   # [(type, name, attribute text)] of struct name, [] when absent
    m = re.search(r'^struct ' + re.escape(name) + r'\n\{\n(.*?)\n\};', src, re.S | re.M)
    if not m: return []
    out = []
    for l in m.group(1).split('\n'):
        f = re.match(r'\s*(.+?)\s+(\w+)(\s*\[\d+\])?\s*(\[\[(.*)\]\])?;\s*$', l)
        if not f: raise Decline(f'struct {name}: unread line {l.strip()!r}')
        if f.group(3): raise Decline(f'struct {name}: array field {f.group(2)}')
        out.append((f.group(1), f.group(2), f.group(5) or ''))
    return out

def entry(src):   # (kind, the signature's span, [(type, name, attribute)])
    m = re.search(r'^(vertex|fragment) main0_out main0\((.*)\)\n\{', src, re.M)
    if not m:
        if re.search(r'^(vertex|fragment) \w+ main0_vi\(', src, re.M): raise Decline('main0_vi entry (function-constant inputs)')
        raise Decline('no main0 entry')
    ps = []
    for p in split_top(m.group(2)):
        f = re.match(r'(.+?)\s*(&?)\s*(\w+)\s*\[\[(.*)\]\]$', p)
        if not f: raise Decline(f'parameter {p!r}')
        ps.append(((f.group(1) + f.group(2)).strip(), f.group(3), f.group(4).strip()))
    return m.group(1), (m.start(), m.end()), ps

def comp(t):   # (scalar kind, components) of a vector / scalar type
    m = re.fullmatch(r'(float|half|int|uint|short|ushort|bool)(\d?)', t)
    if not m: raise Decline(f'type {t}')
    return m.group(1), int(m.group(2) or 1)

def as_f4(expr, t):   # expression of type t stored as a float4 record slot (bits for integers)
    k, n = comp(t)
    sw = ', 0' * (4 - n)
    if k in ('float', 'half'): return f'float4(float{n if n > 1 else ""}({expr}){sw})'
    if k in ('int', 'short'): return f'as_type<float4>(int4(int{n if n > 1 else ""}({expr}){sw}))'
    if k in ('uint', 'ushort', 'bool'): return f'as_type<float4>(uint4(uint{n if n > 1 else ""}({expr}){sw}))'
    raise Decline(f'type {t}')

def from_f4(expr, t):   # a float4 record value as type t
    k, n = comp(t)
    sel = '.xyzw'[:n + 1] if n < 4 else ''
    if k in ('float', 'half'): return f'{t}(({expr}){sel})'
    if k in ('int', 'short'): return f'{t}(as_type<int4>({expr}){sel})'
    if k in ('uint', 'ushort', 'bool'): return f'{t}(as_type<uint4>({expr}){sel})'
    raise Decline(f'type {t}')

def tex_types(src, ps):   # resource name -> texture kind (texture2d, depth2d_array, ...) from the descriptor sets and the direct parameters
    out = {}
    for m in re.finditer(r'^struct (spvDescriptorSetBuffer\d+)\n\{\n(.*?)\n\};', src, re.S | re.M):
        for l in m.group(2).split('\n'):
            f = re.match(r'\s*(?:array<)?(texture\w+|depth\w+)<', l); n = re.search(r'(\w+)\s*(?:\[\d+\])?\s*\[\[id\(', l)
            if f and n: out[n.group(1)] = f.group(1)
    for t, n, a in ps:
        f = re.match(r'(texture\w+|depth\w+)<', t)
        if f: out[n] = f.group(1)
    return out

def grad_samples(body, texs):
    """body with every implicit-LOD sample / sample_compare given gradients from the quad (tk_ddx / tk_ddy of its coordinate); (body, count)"""
    out, i, n = '', 0, 0
    pat = re.compile(r'([\w.\[\]]+)\.(sample_compare|sample)\(')
    while True:
        m = pat.search(body, i)
        if not m: out += body[i:]; return out, n
        recv = re.sub(r'\[[^\]]*\]', '', m.group(1)).split('.')[-1]
        if recv not in texs: raise Decline(f'sample on {m.group(1)} of unknown type')
        kind = texs[recv]
        j, depth = m.end(), 1
        while depth:
            if j >= len(body): raise Decline('unbalanced sample call')
            depth += body[j] == '('; depth -= body[j] == ')'; j += 1
        args = split_top(body[m.end():j - 1])
        lead = TEX_LEAD[kind][1 if m.group(2) == 'sample_compare' else 0]
        if lead is None: raise Decline(f'{m.group(2)} on {kind}')
        if len(args) > lead and LOD_OPT.match(args[lead]):
            o = LOD_OPT.match(args[lead]).group(1)
            if o != 'bias': out += body[i:j]; i = j; continue
            b = args[lead][args[lead].index('(') + 1:args[lead].rindex(')')]   # bias(b): the quad's gradients scaled by 2^b
            c = args[1]; g = f'{GRAD[kind]}(tk_ddx({c}, tk_l) * exp2({b}), tk_ddy({c}, tk_l) * exp2({b}))'
            args = args[:lead] + [g] + args[lead + 1:]
        else:
            c = args[1]; g = f'{GRAD[kind]}(tk_ddx({c}, tk_l), tk_ddy({c}, tk_l))'
            args = args[:lead] + [g] + args[lead:]
        out += body[i:m.start()] + f'{m.group(1)}.{m.group(2)}(' + ', '.join(args) + ')'
        i = j; n += 1

def transform(src, refl):
    """The kernel MSL for one stage (the source with its entry point as tk_body and the kernel appended); raises Decline"""
    kind, (s0, s1), ps = entry(src)
    for pat, why in DECLINE_BODY:
        if re.search(pat, src): raise Decline(why)
    res, ins, call, bparams = [], None, [], []
    for t, n, a in ps:
        if a == 'stage_in': ins = (t, n); bparams.append(f'{t} {n}'); call.append(n); continue
        r = re.fullmatch(r'(buffer|texture|sampler)\((\d+)\)', a)
        if r:
            if r.group(1) == 'buffer' and int(r.group(2)) in (TKP, TKR, TKV): raise Decline(f'buffer index {r.group(2)} taken')
            if r.group(1) == 'texture' and TKO <= int(r.group(2)) < TKO + 8: raise Decline(f'texture index {r.group(2)} taken')
            if re.match(r'device\b', t) and 'const' not in t.split('&')[0].split('*')[0]: raise Decline(f'device write through {n}')
            res.append(f'{t} {n} [[{a}]]'); bparams.append(f'{t} {n}'); call.append(n); continue
        builtin = {'position': 'tk_fc', 'vertex_id': 'tk_vid', 'instance_id': 'tk_iid', 'base_vertex': 'tkP.base_vertex', 'base_instance': 'tkP.base_instance'}
        if a in builtin and (a == 'position') == (kind == 'fragment'):
            bparams.append(f'{t} {n}'); call.append(builtin[a]); continue
        raise Decline(f'parameter {n} [[{a}]]')
    body_sig = 'static inline main0_out tk_body(' + ', '.join(bparams + (['ushort tk_l'] if kind == 'fragment' else [])) + ')\n{'
    body = src[s1:]
    if kind == 'fragment':
        texs = tex_types(src, ps)
        body, ngrad = grad_samples(body, texs) if os.environ.get('DM_TEST_KERNEL_NO_GRAD') != '1' else (body, 0)   # (the env: negative control of t_dmsh_kernel only)
    pre = src[:s0]
    if 'using namespace metal;\n' not in pre: raise Decline('no using namespace metal')
    head = pre.replace('using namespace metal;\n', f'using namespace metal;\n#define TKS {TKS}\n#define TK_TIE {TK_TIE}\n' + COMMON, 1) + body_sig + body
    out_fields = struct_fields(src, 'main0_out'); in_fields = struct_fields(src, ins[0].split()[-1]) if ins else []
    if kind == 'vertex':
        lines = [f'    {ins[0]} {ins[1]} = {{}};'] if ins else []
        for t, n, a in in_fields:
            m = re.fullmatch(r'attribute\((\d+)\)', a)
            if not m: raise Decline(f'vertex input {n} [[{a}]]')
            k, cn = comp(t)
            f = 'tk_fetch_f' if k in ('float', 'half') else 'tk_fetch_u'
            v = f'{f}(tkP.a[{m.group(1)}], tk_vid)'
            if k in ('int', 'short'): v = f'as_type<int4>({v})'
            lines.append(f'    {ins[1]}.{n} = {t}(({v}){".xyzw"[:cn + 1] if cn < 4 else ""});')
        rec = []
        for t, n, a in out_fields:
            if a.startswith('position'): rec.append(f'    tkR[tkI * TKS] = o.{n};')
            elif a == 'point_size': continue
            elif re.fullmatch(r'user\(locn(\d+)\)', a): rec.append(f'    tkR[tkI * TKS + 1 + {re.fullmatch(r"user[(]locn(\d+)[)]", a).group(1)}] = {as_f4("o." + n, t)};')
            else: raise Decline(f'vertex output {n} [[{a}]]')
        if int(max([0] + [int(x) for x in re.findall(r'user\(locn(\d+)\)', ' '.join(a for _, _, a in out_fields))])) + 1 >= TKS: raise Decline('location past the record')
        k = (f'\nkernel void tk_vert({", ".join(res + [f"constant tk_V &tkP [[buffer({TKP})]]", f"device float4 *tkR [[buffer({TKR})]]", "uint tkI [[thread_position_in_grid]]"])})\n{{\n'
             f'    if (tkI >= tkP.count) return;\n    uint tk_vid = tk_index(tkP, tkI), tk_iid = tkP.base_instance;\n' + '\n'.join(lines) +
             f'\n    main0_out o = tk_body({", ".join(call)});\n' + '\n'.join(rec) + '\n}\n')
        return head + k
    # fragment: interpolants, then the body for every pixel of the quad, then the covered pixels' outputs
    interp = []
    for t, n, a in in_fields:
        m = re.fullmatch(r'user\(locn(\d+)\)(?:,\s*(\w+))?', a)
        if not m: raise Decline(f'fragment input {n} [[{a}]]')
        q, slot = m.group(2) or '', int(m.group(1)) + 1
        if q == 'flat' or comp(t)[0] not in ('float', 'half'): v = f'tkR[tkP.tri[tk_t].w * TKS + {slot}]'
        else:
            w = 'tk_lin' if 'no_perspective' in q else 'tk_pw'
            v = f'(tkR[tkP.tri[tk_t].x * TKS + {slot}] * {w}.x + tkR[tkP.tri[tk_t].y * TKS + {slot}] * {w}.y + tkR[tkP.tri[tk_t].z * TKS + {slot}] * {w}.z)'
        interp.append(f'    {ins[1]}.{n} = {from_f4(v, t)};')
    outs = []
    for t, n, a in out_fields:
        m = re.fullmatch(r'color\((\d+)\)', a)
        if not m: raise Decline(f'fragment output {n} [[{a}]]')
        if comp(t)[0] not in ('float', 'half'): raise Decline(f'integer output {n}')
        outs.append((int(m.group(1)), n, t))
    if not outs: raise Decline('no colour output')
    if max(o[0] for o in outs) > 7: raise Decline('output past 7')
    kernels = ''
    for acc in ('w', 'rw'):
        tex = [f'texture2d<float, access::{"read_write" if acc == "rw" else "write"}> tk_o{c} [[texture({TKO + c})]]' for c, _, _ in outs]
        wr = []
        for c, n, t in outs:
            v = f'float4(o.{n})'
            if acc == 'rw': wr.append(f'        if (tkP.bl[{c}].z & 0xf1) tk_o{c}.write(tk_blend(tkP.bl[{c}], {v}, tk_o{c}.read(tk_px), tkP.bc), tk_px);')
            else: wr.append(f'        if (tkP.bl[{c}].z & 0xf0) tk_o{c}.write(tkP.bl[{c}].w == 1 ? saturate({v}) : tkP.bl[{c}].w == 2 ? clamp({v}, -1.0, 1.0) : {v}, tk_px);')
        kernels += (f'\nkernel void tk_frag_{acc}({", ".join(res + [f"constant tk_F &tkP [[buffer({TKP})]]", f"const device float4 *tkR [[buffer({TKR})]]", f"device atomic_uint *tk_vis [[buffer({TKV})]]"] + tex + ["uint tkT [[thread_position_in_grid]]", "ushort tk_l [[thread_index_in_quadgroup]]"])})\n{{\n'
            '    uint tk_q = min(tkT >> 2, tkP.nq - 1);\n'
            '    uint2 tk_px = tkP.org + uint2((tk_q % tkP.qw) * 2 + (tk_l & 1), (tk_q / tkP.qw) * 2 + (tk_l >> 1));\n'
            '    float2 tk_p = float2(tk_px) + 0.5;\n'
            '    float3 tk_pw = float3(1, 0, 0), tk_lin = float3(1, 0, 0), tw, tl; float tk_z = 0, tk_iw = 1, tz, ti; uint tk_t = 0; int tk_in = 0;\n'
            '    for (uint t = 0; t < tkP.ntri; t++) { int c = tk_tri(tkP, tkR, t, tk_p, tw, tl, tz, ti);\n'
            '        if (c && !tk_in) { tk_in = 1; tk_t = t; tk_pw = tw; tk_lin = tl; tk_z = tz; tk_iw = ti; } }\n'
            '    // a pixel no triangle covers takes the triangle of a covered pixel of its quad (a helper), extrapolated\n'
            '    uint tk_c = tk_in ? tk_t : 255u; tk_c = min(tk_c, quad_shuffle_xor(tk_c, ushort(1))); tk_c = min(tk_c, quad_shuffle_xor(tk_c, ushort(2)));\n'
            '    if (!tk_in && tk_c != 255u) { tk_t = tk_c; tk_tri(tkP, tkR, tk_t, tk_p, tk_pw, tk_lin, tk_z, tk_iw); }\n'
            f'    {ins[0]} {ins[1]} = {{}};\n' + '\n'.join(interp) + '\n'
            '    float4 tk_fc = float4(tk_p, tk_z, tk_iw);\n'
            f'    main0_out o = tk_body({", ".join(call + ["tk_l"])});\n'
            '    bool tk_w = tk_in && (tkT >> 2) < tkP.nq && all(tk_px >= tkP.lo) && all(tk_px < tkP.lim);\n'
            '    if (tkP.vis) { uint n = simd_sum(uint(tk_w)); if (simd_is_first() && n) atomic_fetch_add_explicit(tk_vis, n, memory_order_relaxed); }\n'
            '    if (tk_w) {\n' + '\n'.join(wr) + '\n    }\n}\n')
    return head + kernels

def compile_lib(msl_path, lib, ios=False):
    import ios_metal
    with tempfile.TemporaryDirectory() as td:
        air = os.path.join(td, 'k.air')
        if ios: ios_metal.compile(msl_path, air, lib)
        else:
            subprocess.run(['xcrun', '-sdk', 'macosx', 'metal', '-c', '-fpreserve-invariance', '-fmetal-math-mode=safe', msl_path, '-o', air], check=True, capture_output=True)
            subprocess.run(['xcrun', '-sdk', 'macosx', 'metallib', air, '-o', lib], check=True, capture_output=True)

def main():
    if len(sys.argv) == 4 and sys.argv[1] == '--msl':   # one source to one kernel source (gfx/tests/t_kpass.m's shaders)
        try: open(sys.argv[3], 'w').write(transform(open(sys.argv[2]).read(), {})); return 0
        except Decline as e: print(f'[red] {sys.argv[2]}: declined ({e})'); return 1
    if len(sys.argv) < 3: print(open(__file__).read().split('\nimport')[0]); return 2
    keys = [l.strip() for l in open(sys.argv[1]) if l.strip() and not l.lstrip().startswith('#')]
    mac, ios = sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None
    import json
    made = declined = missing = 0
    for k in keys:
        src_p = os.path.join(mac, k + '.metal')
        if not os.path.exists(src_p): print(f'[red] {k}: no {src_p}'); missing += 1; continue
        try: refl = json.load(open(os.path.join(mac, k + '.json')))
        except OSError: refl = {}
        try: kmsl = transform(open(src_p).read(), refl)
        except Decline as e:
            print(f'{k}: declined ({e})'); declined += 1
            for p in [os.path.join(mac, k + 'k.metal'), os.path.join(mac, k + 'k.metallib')] + ([os.path.join(ios, k + 'k.metallib')] if ios else []):
                if os.path.exists(p): os.remove(p)
            continue
        out_msl = os.path.join(mac, k + 'k.metal')
        open(out_msl, 'w').write(kmsl)
        try:
            compile_lib(out_msl, os.path.join(mac, k + 'k.metallib'))
            if ios: compile_lib(out_msl, os.path.join(ios, k + 'k.metallib'), ios=True)
        except subprocess.CalledProcessError as e:
            print(f'[red] {k}: the kernel does not compile:\n{(e.stderr or b"").decode(errors="replace")[:2000]}'); return 1
        made += 1
    print(f'kernels: {made} made, {declined} declined, {missing} missing')
    return 1 if missing else 0

if __name__ == '__main__': sys.exit(main())
