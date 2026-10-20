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
print(f'== {total} checks, {fails} failed =='); sys.exit(fails != 0)
