#include "fullscreenvert.hlsli"
#include "ssao.h"

Texture2D depth_map : register(t0, space0);
Texture2D normal_map : register(t1, space0);
sampler sampl : register(s2, space0);
ConstantBuffer<SSAOParams> params : register(b3, space0);
Texture2D noise : register(t4, space0);
sampler noise_sampler : register(s5, space0);

[[vk::push_constant]] ConstantBuffer<SSAOPushConstants> consts
    : register(b6, space0);

ConstantBuffer<CommonViewData> view_data : register(b0, space1);

float frag(Interpolators interp) : SV_TARGET {
  const float near = view_data.proj_params.x;
  const float far = view_data.proj_params.y;

  float3 frag_pos = view_space_pos_from_depth(
      depth_map.Sample(sampl, interp.uv0).r, view_data.inv_proj, interp.uv0);
  float3 normal =
      normalize(normal_map.Sample(sampl, interp.uv0).rgb * 2.0 - 1.0);

  float2 noise_uv = interp.uv0 * consts.noise_scale;
  float3 rand_vec = noise.Sample(noise_sampler, noise_uv).xyz * 2.0 - 1.0;
  float3 tangent = normalize(rand_vec - normal * dot(rand_vec, normal));
  float3 binormal = cross(tangent, normal);
  float3x3 orientation = float3x3(tangent, binormal, normal);

  float occlusion = 0.0f;
  for (int i = 0; i < params.kernel_size; ++i) {
    float3 kernel_sample = mul(orientation, params.kernel[i]);
    kernel_sample = (kernel_sample * consts.radius) + frag_pos;

    float4 offset = float4(kernel_sample, 1.0f);
    offset = mul(view_data.p, offset);
    offset.xy /= offset.w;
    offset.xy = offset.xy * 0.5 + 0.5;

    float sample_depth =
        -linear_depth(depth_map.Sample(sampl, offset.xy).r, near, far);

    float range_check =
        smoothstep(0.0, 1.0, consts.radius / abs(frag_pos.z - sample_depth));
    occlusion += (sample_depth >= kernel_sample.z ? 1.0 : 0.0) * range_check;
  }

  return 1.0 - (occlusion / (float)params.kernel_size);
}