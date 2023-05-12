#include "fullscreenvert.hlsli"

Texture2D color_map : register(t0, space0);    // Fragment Stage Only
sampler static_sampler : register(s1, space0); // Immutable Sampler

[[vk::push_constant]] ConstantBuffer<BloomBlurPushConstants> consts
    : register(b0, space0);

float4 frag(Interpolators interp) : SV_TARGET {
  const float weight[5] = {0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216};

  // Get size of single texel in uv coordinates
  float2 tex_size;
  color_map.GetDimensions(tex_size.x, tex_size.y);

  float2 tex_offset = 1 / tex_size;

  float3 result = color_map.Sample(static_sampler, interp.uv0).rgb *
                  weight[0]; // current fragment's contribution
  if (consts.horizontal > 0.0f) {
    for (int i = 1; i < 5; ++i) {
      result += color_map
                    .Sample(static_sampler,
                            interp.uv0 + float2(tex_offset.x * i, 0.0))
                    .rgb *
                weight[i];
      result += color_map
                    .Sample(static_sampler,
                            interp.uv0 - float2(tex_offset.x * i, 0.0))
                    .rgb *
                weight[i];
    }
  } else {
    for (int i = 1; i < 5; ++i) {
      result += color_map
                    .Sample(static_sampler,
                            interp.uv0 + float2(0.0, tex_offset.y * i))
                    .rgb *
                weight[i];
      result += color_map
                    .Sample(static_sampler,
                            interp.uv0 - float2(0.0, tex_offset.y * i))
                    .rgb *
                weight[i];
    }
  }
  return float4(result, 1.0);
}