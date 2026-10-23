#!/usr/bin/env python3
"""gfx/tools/msl_helpers.py: SPIRV-Cross's optnone arithmetic helper, as it writes it, becomes an always-inline one with FP contraction off in its
body; the rest of the file and a file without helpers stay byte for byte; a second pass changes nothing. (Its bits on the GPU: gfx/tests/t_fpcontract.m.)
The helper left as it is (an empty rewrite, by hand) fails 3."""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'gfx', 'tools'))
import msl_helpers as mh
fails = total = 0
def check(name, ok):
    global fails, total
    total += 1; fails += not ok; print(f'{name:<74} {"✓" if ok else "✗"}')
src = ('#include <metal_stdlib>\nusing namespace metal;\n\ntemplate<typename T>\n[[clang::optnone]] T spvFAdd(T l, T r)\n{\n    return fma(T(1), l, r);\n}\n\n'
       'fragment float4 main0(float4 c [[position]]) { return float4(spvFAdd(c.x * c.y, c.z)); }\n')
out = mh.inline_helpers(src)
want = src.replace('[[clang::optnone]] T spvFAdd(T l, T r)\n{\n', 'inline __attribute__((always_inline)) T spvFAdd(T l, T r)\n{\n#pragma clang fp contract(off)\n')
check('the helper inlined, contraction off in its body', out == want)
check('no optnone left', '[[clang::optnone]]' not in out)
check('a second pass changes nothing', mh.inline_helpers(out) == out)
plain = '#include <metal_stdlib>\nkernel void k(device float *o [[buffer(0)]]) { o[0] = 1.0f; }\n'
check('a file without helpers stays byte for byte', mh.inline_helpers(plain) == plain)
sub = src.replace('spvFAdd', 'spvFSub').replace('fma(T(1), l, r)', 'fma(T(-1), r, l)')
check('the subtraction helper the same way', '[[clang::optnone]]' not in mh.inline_helpers(sub))

# precise_minmax (plan item 260): SPIRV-Cross's fast::min / max / clamp as precise::, the arguments as they were, nested calls too; other fast::
# functions and integer min / max stay; compiled with iOS's flags (gfx/tools/ios_metal.py) the library calls IEEE air.fmin / fmax / clamp and no
# fast_ one (the original calls the fast_ ones, the premise). The compile is skipped without Xcode's metal compiler.
# The rewrite left out (T_MSL_PRECISE_OFF=1, the broken variant): 3 fail
import re, subprocess, tempfile
precise = (lambda s: (s, 0)) if os.environ.get('T_MSL_PRECISE_OFF') else mh.precise_minmax
mm = ('#include <metal_stdlib>\n#include <simd/simd.h>\n\nusing namespace metal;\n\n'
      'struct main0_out\n{\n    float4 out_attr0 [[color(0)]];\n};\n\nstruct main0_in\n{\n    float4 in_attr0 [[user(locn0)]];\n};\n\n'
      'fragment main0_out main0(main0_in in [[stage_in]])\n{\n    main0_out out = {};\n'
      '    float _10 = fast::max(in.in_attr0.x, 0.0);\n'
      '    float _11 = fast::min(in.in_attr0.y, fast::max(in.in_attr0.z, 1.0));\n'
      '    float4 _12 = fast::clamp(in.in_attr0, float4(0.0), float4(1.0));\n'
      '    int _13 = min(int(_10), 3);\n'
      '    float _14 = fast::normalize(in.in_attr0.xyz).x;\n'
      '    out.out_attr0 = float4(_10 + _11 + float(_13) + _14) + _12;\n    return out;\n}\n')
out, n = precise(mm)
check('fast::min / max / clamp written as precise::, arguments as they were', out == mm.replace('fast::max(', 'precise::max(').replace('fast::min(', 'precise::min(')
                                                                                 .replace('fast::clamp(', 'precise::clamp('))
check('four calls counted, the nested one too', n == 4)
check('fast::normalize and the integer min left as they were', 'fast::normalize(' in out and 'int _13 = min(int(_10), 3);' in out)
check('a second pass changes nothing', precise(out) == (out, 0))
check('a file without them stays byte for byte', precise(plain) == (plain, 0))
if subprocess.run(['xcrun', '-sdk', 'iphoneos', '--find', 'metal'], capture_output=True).returncode:
    print('skipped: no metal compiler for the iOS SDK (the library checks)')
else:
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'gfx', 'tools')); import ios_metal
    def calls(src):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, 'k.metal'); open(p, 'w').write(src)
            cmd = [x for a in ios_metal.METAL for x in (['-S', '-emit-llvm'] if a == '-c' else [a])]   # iOS's flags, LLVM IR instead of an object
            subprocess.run([*cmd, p, '-o', p + '.ll'], check=True, capture_output=True)
            return set(re.findall(r'call [^@\n]*@air\.((?:fast_)?f?(?:min|max|clamp))\.', open(p + '.ll').read()))
    before, after = calls(mm), calls(out)
    check('the original calls the fast_ intrinsics (with iOS\'s safe math mode)', {'fast_fmin', 'fast_fmax', 'fast_clamp'} <= before)
    check('rewritten: IEEE fmin / fmax / clamp and no fast_ min / max / clamp', not any(c.startswith('fast_') for c in after) and {'fmin', 'fmax', 'clamp'} <= after)
print(f'== {total} checks, {fails} failed =='); sys.exit(fails != 0)
