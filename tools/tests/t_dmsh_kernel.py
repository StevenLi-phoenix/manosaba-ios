#!/usr/bin/env python3
"""gfx/tools/dmsh_kernel.py (plan item 217) on MSL written as the chain's spirv-cross output: every implicit-LOD sample gets the quad's gradients in
the form its texture type takes (2D, 2D array, cube, 3D, depth with sample_compare, bias as scaled gradients), explicit lods are left, the
stages the kernels cannot reproduce are declined with their reason, and a vertex and a fragment kernel compile for macOS and iOS. Broken variant:
DM_TEST_KERNEL_NO_GRAD=1 (samples left as they are): 6 fail. (gfx/tests/t_kpass.m checks the kernels against Metal on the GPU.)"""
import os, re, subprocess, sys, tempfile
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
sys.path.insert(0, os.path.join(ROOT, 'gfx', 'tools')); import dmsh_kernel as K
fails = total = 0
def check(name, ok, detail=''):
    global fails, total
    total += 1; fails += not ok; print(f'{name:<100} {"✓" if ok else "✗"}' + (f'\n    {detail}' if detail and not ok else ''))

FS = '''#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct spvDescriptorSetBuffer0
{
    texture2d<float> fp_a [[id(0)]];
    sampler fp_aSmplr [[id(1)]];
    texture2d_array<float> fp_b [[id(2)]];
    texturecube<float> fp_c [[id(3)]];
    depth2d<float> fp_d [[id(4)]];
    texture3d<float> fp_e [[id(5)]];
};

struct main0_out
{
    float4 m_o [[color(0)]];
};

struct main0_in
{
    float4 m_u [[user(locn0)]];
};

fragment main0_out main0(main0_in in [[stage_in]], constant spvDescriptorSetBuffer0& spvDescriptorSet0 [[buffer(0)]])
{
    main0_out out = {};
    float2 _1 = in.m_u.xy;
    float4 _2 = spvDescriptorSet0.fp_a.sample(spvDescriptorSet0.fp_aSmplr, _1);
    float4 _3 = spvDescriptorSet0.fp_b.sample(spvDescriptorSet0.fp_aSmplr, _1, uint(rint(in.m_u.z)));
    float4 _4 = spvDescriptorSet0.fp_c.sample(spvDescriptorSet0.fp_aSmplr, in.m_u.xyz);
    float _5 = spvDescriptorSet0.fp_d.sample_compare(spvDescriptorSet0.fp_aSmplr, _1, in.m_u.w, int2(1, 0));
    float4 _6 = spvDescriptorSet0.fp_a.sample(spvDescriptorSet0.fp_aSmplr, _1, bias(in.m_u.z));
    float4 _7 = spvDescriptorSet0.fp_a.sample(spvDescriptorSet0.fp_aSmplr, _1, level(2.0));
    float4 _8 = spvDescriptorSet0.fp_e.sample(spvDescriptorSet0.fp_aSmplr, in.m_u.xyz);
    out.m_o = _2 + _3 + _4 + float4(_5) + _6 + _7 + _8;
    return out;
}
'''
k = K.transform(FS, {})
check('2D: sample(s, c) gets gradient2d(tk_ddx(c), tk_ddy(c))', 'fp_a.sample(spvDescriptorSet0.fp_aSmplr, _1, gradient2d(tk_ddx(_1, tk_l), tk_ddy(_1, tk_l)))' in k)
check('2D array: the gradients after the array index', 'fp_b.sample(spvDescriptorSet0.fp_aSmplr, _1, uint(rint(in.m_u.z)), gradient2d(tk_ddx(_1, tk_l), tk_ddy(_1, tk_l)))' in k)
check('cube: gradientcube of the 3-component coordinate', 'fp_c.sample(spvDescriptorSet0.fp_aSmplr, in.m_u.xyz, gradientcube(tk_ddx(in.m_u.xyz, tk_l), tk_ddy(in.m_u.xyz, tk_l)))' in k)
check('depth sample_compare: the gradients after the reference, before the offset',
      'fp_d.sample_compare(spvDescriptorSet0.fp_aSmplr, _1, in.m_u.w, gradient2d(tk_ddx(_1, tk_l), tk_ddy(_1, tk_l)), int2(1, 0))' in k)
check('bias(b): the gradients scaled by 2^b', 'gradient2d(tk_ddx(_1, tk_l) * exp2(in.m_u.z), tk_ddy(_1, tk_l) * exp2(in.m_u.z))' in k)
check('level(2.0): left as it is', 'fp_a.sample(spvDescriptorSet0.fp_aSmplr, _1, level(2.0))' in k)
check('3D: gradient3d', 'gradient3d(tk_ddx(in.m_u.xyz, tk_l), tk_ddy(in.m_u.xyz, tk_l))' in k)
check('both fragment kernels, the stage as tk_body with the quad lane', 'kernel void tk_frag_w(' in k and 'kernel void tk_frag_rw(' in k and 'static inline main0_out tk_body(main0_in in, constant spvDescriptorSetBuffer0& spvDescriptorSet0, ushort tk_l)' in k)
def declined(src):
    try: K.transform(src, {}); return None
    except K.Decline as e: return str(e)
D = [('discard', FS.replace('return out;\n}', 'if (_2.x < 0) discard_fragment();\n    return out;\n}')),
     ('derivative', FS.replace('float2 _1 = in.m_u.xy;', 'float2 _1 = dfdx(in.m_u.xy);')),
     ('fragment output', FS.replace('float4 m_o [[color(0)]];', 'float4 m_o [[color(0)]];\n    float gl_FragDepth [[depth(any)]];')),
     ('main0_vi', FS.replace('fragment main0_out main0(', 'fragment main0_out main0_vi(')),
     ('device write', FS.replace('constant spvDescriptorSetBuffer0& spvDescriptorSet0 [[buffer(0)]])', 'constant spvDescriptorSetBuffer0& spvDescriptorSet0 [[buffer(0)]], device uint* w [[buffer(1)]])')),
     ('integer output', FS.replace('float4 m_o [[color(0)]];', 'uint4 m_o [[color(0)]];')),
     ('parameter', FS.replace('[[buffer(0)]])', '[[buffer(0)]], bool gl_FrontFacing [[front_facing]])')),
     ('buffer index', FS.replace('[[buffer(0)]])', '[[buffer(0)]], constant float4* x [[buffer(29)]])'))]
for why, src in D:
    r = declined(src); check(f'declined: {why}', r is not None and why in r, str(r))
VS = open(os.path.join(ROOT, 'gfx', 'tests', 'kpass', 'vs.metal')).read()
kv = K.transform(VS, {})
check('vertex: the attributes fetched, the position and locations to the record', 'in.m_pos = float4((tk_fetch_f(tkP.a[0], tk_vid)));' in kv and 'tkR[tkI * TKS] = o.gl_Position;' in kv
      and 'tkR[tkI * TKS + 1 + 3] = float4(float4(o.m_n));' in kv)
r = declined(VS.replace('float gl_PointSize [[point_size]];', 'float gl_PointSize [[point_size]];\n    float gl_ClipDistance [[clip_distance]] [1];')); check('declined: clip distances', r is not None, str(r))
with tempfile.TemporaryDirectory() as td:
    for name, src in (('fs', k), ('vs', kv)):
        p = os.path.join(td, name + '.metal'); open(p, 'w').write(src)
        for ios in (False, True):
            try: K.compile_lib(p, os.path.join(td, f'{name}{int(ios)}.metallib'), ios=ios); ok, err = True, ''
            except subprocess.CalledProcessError as e: ok, err = False, (e.stderr or b'').decode(errors='replace')[:400]
            check(f'the {name} kernel compiles for {"iOS" if ios else "macOS"}', ok, err)
print(f'== {total} checks, {fails} failed =='); sys.exit(fails != 0)
