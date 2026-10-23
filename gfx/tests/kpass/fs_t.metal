#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct spvDescriptorSetBuffer0
{
    texture2d<float> fp_t [[id(0)]];
    sampler fp_tSmplr [[id(1)]];
};

struct main0_out
{
    float4 m_o0 [[color(0)]];
};

struct main0_in
{
    float4 m_u [[user(locn1)]];
};

fragment main0_out main0(main0_in in [[stage_in]], constant spvDescriptorSetBuffer0& spvDescriptorSet0 [[buffer(0)]])
{
    main0_out out = {};
    float2 _9 = in.m_u.xy;
    out.m_o0 = spvDescriptorSet0.fp_t.sample(spvDescriptorSet0.fp_tSmplr, _9);
    return out;
}
