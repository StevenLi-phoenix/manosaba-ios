// t_fpcontract.m's kernels: a product added to a third value through SPIRV-Cross's NoContraction helper, exactly as it writes it (msl_helpers.py
// rewrites this file into the inlined form), beside the fused and the plain forms
#include <metal_stdlib>
using namespace metal;

template<typename T>
[[clang::optnone]] T spvFAdd(T l, T r)
{
    return fma(T(1), l, r);
}

kernel void k_helper(device const float *a [[buffer(0)]], device const float *b [[buffer(1)]], device const float *c [[buffer(2)]], device float *o [[buffer(3)]], uint i [[thread_position_in_grid]])
{
    float p = a[i] * b[i];
    o[i] = spvFAdd(p, c[i]);
}
kernel void k_fused(device const float *a [[buffer(0)]], device const float *b [[buffer(1)]], device const float *c [[buffer(2)]], device float *o [[buffer(3)]], uint i [[thread_position_in_grid]])
{
    o[i] = fma(a[i], b[i], c[i]);
}
