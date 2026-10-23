#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct main0_out
{
    float4 m_c [[user(locn0)]];
    float4 m_u [[user(locn1)]];
    float4 m_f [[user(locn2)]];
    float4 m_n [[user(locn3)]];
    float4 gl_Position [[position, invariant]];
    float gl_PointSize [[point_size]];
};

struct main0_in
{
    float4 m_pos [[attribute(0)]];
    float4 m_col [[attribute(1)]];
    float4 m_uv [[attribute(2)]];
};

vertex main0_out main0(main0_in in [[stage_in]])
{
    main0_out out = {};
    out.gl_PointSize = 1.0;
    out.gl_Position = in.m_pos;
    out.m_c = in.m_col;
    out.m_u = in.m_uv;
    out.m_f = in.m_col;
    out.m_n = in.m_uv;
    return out;
}
