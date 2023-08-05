#include "fullscreenvert.hlsli"

Texture2D input : register(t0, space0);
sampler static_sampler : register(s1, space0);

[[vk::push_constant]] ConstantBuffer<BlurPushConstants> consts
    : register(b2, space0);

float4 frag(Interpolators i) : SV_TARGET {
#define WEIGHT_COUNT 5
  const float weight[WEIGHT_COUNT] = {0.227027, 0.1945946, 0.1216216, 0.054054,
                                      0.016216};

  float2 input_len;
  input.GetDimensions(input_len.x, input_len.y);
  float2 tex_offset = 1 / input_len;

  float2 sample_point = i.uv0;
  float4 result =
      input.SampleLevel(static_sampler, sample_point, 0) * weight[0];

  if (consts.horizontal > 0.0f) {
    for (int i = 1; i < WEIGHT_COUNT; ++i) {
      result +=
          input.SampleLevel(static_sampler,
                            sample_point + float2(tex_offset.x * i, 0), 0) *
          weight[i];
      result +=
          input.SampleLevel(static_sampler,
                            sample_point - float2(tex_offset.x * i, 0), 0) *
          weight[i];
    }
  } else {
    for (int i = 1; i < WEIGHT_COUNT; ++i) {
      result +=
          input.SampleLevel(static_sampler,
                            sample_point + float2(0, tex_offset.y * i), 0) *
          weight[i];
      result +=
          input.SampleLevel(static_sampler,
                            sample_point - float2(0, tex_offset.y * i), 0) *
          weight[i];
    }
  }

  return result;
#undef WEIGHT_COUNT
}