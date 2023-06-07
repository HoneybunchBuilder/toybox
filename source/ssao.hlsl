#include "fullscreenvert.hlsli"
#include "ssao.h"

Texture2D depth_map : register(t0, space0);
Texture2D normal_map : register(t1, space0);
sampler map_sampler : register(s2, space0);
ConstantBuffer<SSAOParams> params : register(b3, space0);
Texture2D noise : register(t4, space0);
sampler noise_sampler : register(s5, space0);

[[vk::push_constant]] ConstantBuffer<SSAOPushConstants> consts
    : register(b6, space0);

ConstantBuffer<CommonViewData> view_data : register(b0, space1);

float frag(Interpolators interp) : SV_TARGET {
  const float near = view_data.proj_params.x;
  const float far = view_data.proj_params.y;

  const float bias = 0.025f;

  float3 origin =
      view_space_pos_from_depth(depth_map.Sample(map_sampler, interp.uv0).r,
                                view_data.inv_proj, interp.uv0);
  float3 normal =
      normalize(normal_map.Sample(map_sampler, interp.uv0).rgb * 2.0 - 1.0);

  float3 random =
      noise.Sample(noise_sampler, interp.uv0 * consts.noise_scale).xyz * 2.0 -
      1.0;
  float3 tangent = normalize(random - normal * dot(random, normal));
  float3 bitangent = cross(normal, tangent);
  float3x3 orientation = float3x3(tangent, bitangent, normal);

  float occlusion = 0.0f;
  for (int i = 0; i < params.kernel_size; ++i) {
    float3 kernel_sample = mul(orientation, params.kernel[i]);
    kernel_sample = origin + kernel_sample * consts.radius;

    float4 offset = float4(kernel_sample, 1.0f);
    offset = mul(offset, view_data.p);
    offset = offset / offset.w;
    offset.xy = offset.xy * 0.5 + 0.5;

    float sample_depth =
        -linear_depth(depth_map.Sample(map_sampler, offset.xy).r, near, far);

    float range_check =
        smoothstep(0.0f, 1.0f, consts.radius / abs(origin.z - sample_depth));
    occlusion +=
        (sample_depth >= kernel_sample.z + bias ? 1.0 : 0.0) * range_check;
  }

  return 1 - (occlusion / params.kernel_size);
}