#include "fullscreenvert.hlsli"
#include "ssao.h"

Texture2D depth_map : register(t0, space0);
Texture2D normal_map : register(t1, space0);
sampler map_sampler : register(s2, space0);
ConstantBuffer<SSAOParams> params : register(b3, space0);
Texture2D noise : register(t4, space0);
sampler noise_sampler : register(s5, space0);

[[vk::push_constant]] ConstantBuffer<SSAOPushConstants> consts : register(b6, space0);

float frag(Interpolators interp) : SV_TARGET {
    float3 origin = consts.view_dir * depth_map.Sample(map_sampler, interp.uv0).r;
    float3 normal = normalize(normal_map.Sample(map_sampler, interp.uv0).rgb * 2.0 - 1.0);

    float3 random = noise.Sample(noise_sampler, interp.uv0 * consts.noise_scale).xyz * 2.0 - 1.0;
    float3 tangent =  normalize(random - normal * dot(random, normal));
    float3 bitangent = cross(normal, tangent);
    float3x3 orientation = float3x3(tangent, bitangent, normal);

    float occlusion = 0.0f;
    for(int i = 0; i < params.kernel_size; ++i) {
        float3 kernel_sample = mul(orientation, params.kernel[i]);
        kernel_sample = origin + (kernel_sample * consts.radius);

        float4 offset = float4(kernel_sample, 1.0f);
        offset = mul(consts.projection, offset);
        
        offset.xy = offset.xy * 0.5 + 0.5;
        offset.xy /= offset.w;

        float sample_depth = depth_map.Sample(map_sampler, offset.xy).r;
        float range_check = abs(origin.z - sample_depth) < consts.radius ? 1.0 : 0.0;
        occlusion += (sample_depth <= kernel_sample.z ? 1.0 : 0.0) * range_check;
    }

    return 1.0 - (occlusion / params.kernel_size);
}