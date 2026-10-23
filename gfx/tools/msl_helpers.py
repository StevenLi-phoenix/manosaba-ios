#!/usr/bin/env python3
# SPIRV-Cross writes the additions the SPIR-V marks NoContraction (a Maxwell FADD that must not fuse with the FMUL before it) through a helper
#     template<typename T>
#     [[clang::optnone]] T spvFAdd(T l, T r)
#     {
#         return fma(T(1), l, r);
#     }
# whose optnone keeps the compiler from fusing it, at the price of a real function call on the GPU: 3,088 of chain bab8d4746968's 31,392 files,
# 8,641 call sites. dmsh_build.py passes every MSL through inline_helpers(): the helpers are inlined instead, with FP contraction off inside their
# own bodies only (totk-mobile's nvn_shader_math.h turns it off for the whole file and the math mode with it; Apache-2.0, the idea only), so the
# rest of the file compiles as before. Measured (gfx/tests/t_fpcontract.m, bit for bit against the optnone helper): under our safe math mode not
# even a plain a * b + c is fused, and the inlined fma(1, l, r) stays unfused under fast math too. In dmsh_chain.py's FILES (a change here is a new
# chain). Usage (the test's runner): msl_helpers.py <MSL> [--plain]  → the rewritten MSL on stdout (--plain: the broken variant, a plain l + r)
#
# precise_minmax() (plan item 260, after seedds/nxnative 56704a0, MIT, written anew): SPIRV-Cross writes the translator's FMin / FMax / FClamp as
# fast::min / max / clamp, which compile to air.fast_fmin / fast_fmax / fast_clamp in every Metal math mode, safe included, and leave a NaN
# operand's result to the GPU. Maxwell's FMNMX returns the other operand and its saturation gives 0, as precise:: (air.fmin / fmax / clamp, IEEE
# fmin / fmax) does on every GPU. The M4 Pro's and our M5's fast ones happen to agree (fast::max(x, 0) of NaN is 0 on the M5), an A20 Pro's do
# not: Super Mario Galaxy 2's lighting takes max(0, NaN) from light directions left at zero, and half of Mario came out white on that iPhone.
# Only the fast:: calls are rewritten: in a 300-shader sample of chain f9e7d3ecf49e every plain min / max / clamp left is an integer one, and no
# fast_ intrinsic call is left in the compiled libraries.
import re, sys

HELPER = re.compile(r'\[\[clang::optnone\]\] (T spvF(?:Add|Sub|Mul)\(T l, T r\)\n\{\n)')
FAST_MINMAX = re.compile(r'\bfast::(min|max|clamp)\(')

def inline_helpers(src):
    """The MSL with every optnone arithmetic helper inlined, contraction off in its body; unchanged when it has none"""
    return HELPER.sub(lambda m: 'inline __attribute__((always_inline)) ' + m.group(1) + '#pragma clang fp contract(off)\n', src)

def precise_minmax(src):
    """The MSL with every fast::min / max / clamp call written as precise:: (Maxwell's results for a NaN operand), and how many were rewritten"""
    return FAST_MINMAX.subn(r'precise::\1(', src)

if __name__ == '__main__':
    out = inline_helpers(open(sys.argv[1]).read())
    if '--plain' in sys.argv: out = out.replace('#pragma clang fp contract(off)\n    return fma(T(1), l, r);', '    return l + r;')
    sys.stdout.write(out)
