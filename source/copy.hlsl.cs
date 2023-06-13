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
  float2 input_len;
  input.GetDimensions(input_len.x, input_len.y);
  float2 tex_offset = 1 / input_len;

  float2 sample_point = dispatch_thread_id.xy / input_len;
  float4 value = input.SampleLevel(static_sampler, sample_point, 0);
  output[dispatch_thread_id.xy] = value;
}