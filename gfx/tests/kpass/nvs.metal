#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct main0_out
{
    float4 m_c [[user(locn0)]];
    float4 gl_Position [[position, invariant]];
};

vertex main0_out main0(uint gl_VertexIndex [[vertex_id]])
{
    main0_out out = {};
    float2 _1 = float2(gl_VertexIndex == 1 ? 3.0 : -1.0, gl_VertexIndex == 2 ? 3.0 : -1.0);
    out.gl_Position = float4(_1, 0.5, 1.0);
    out.m_c = float4(_1 * 0.25 + 0.25, 0.5, 1.0);
    return out;
}
