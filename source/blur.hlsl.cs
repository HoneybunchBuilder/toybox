#include "common.hlsli"

Texture2D input : register(t0, space0);
RWTexture2D<float4> output : register(u1, space0);
sampler static_sampler : register(s2, space0);

[[vk::push_constant]]
ConstantBuffer<BlurPushConstants> consts : register(b3, space0);

#define GROUP_SIZE 256

[numthreads(GROUP_SIZE, 1, 1)]
void comp(int3 group_thread_id: SV_GroupThreadID,
          int3 dispatch_thread_id: SV_DispatchThreadID) {
#define WEIGHT_COUNT 5
  const float weight[WEIGHT_COUNT] = { 0.227027, 0.1945946, 0.1216216, 0.054054,
                                       0.016216 };

  float2 input_len;
  input.GetDimensions(input_len.x, input_len.y);
  float2 tex_offset = 1 / input_len;

  float2 sample_point = dispatch_thread_id.xy / input_len;
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

  output[dispatch_thread_id.xy] = result;
#undef WEIGHT_COUNT
}