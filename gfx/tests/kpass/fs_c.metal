#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct main0_out
{
    float4 m_o0 [[color(0)]];
};

struct main0_in
{
    float4 m_c [[user(locn0)]];
};

fragment main0_out main0(main0_in in [[stage_in]])
{
    main0_out out = {};
    out.m_o0 = in.m_c;
    return out;
}
