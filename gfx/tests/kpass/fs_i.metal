#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct main0_out
{
    float4 m_o0 [[color(0)]];
    float4 m_o1 [[color(1)]];
    float4 m_o2 [[color(2)]];
    float4 m_o3 [[color(3)]];
};

struct main0_in
{
    float4 m_c [[user(locn0)]];
    float4 m_f [[user(locn2), flat]];
    float4 m_n [[user(locn3), center_no_perspective]];
};

fragment main0_out main0(main0_in in [[stage_in]], float4 gl_FragCoord [[position]])
{
    main0_out out = {};
    out.m_o0 = in.m_c;
    out.m_o1 = in.m_f;
    out.m_o2 = in.m_n;
    out.m_o3 = float4(gl_FragCoord.x / 64.0, gl_FragCoord.y / 64.0, gl_FragCoord.z, gl_FragCoord.w);
    return out;
}
