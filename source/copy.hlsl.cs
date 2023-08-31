#include "common.hlsli"

Texture2D input : register(t0, space0);
RWTexture2D<float4> output : register(u1, space0);
sampler static_sampler : register(s2, space0);

#define GROUP_SIZE 256

[numthreads(GROUP_SIZE, 1, 1)]
void comp(int3 group_thread_id: SV_GroupThreadID,
          int3 dispatch_thread_id: SV_DispatchThreadID) {
  float2 resolution;
  input.GetDimensions(resolution.x, resolution.y);
  float2 tex_offset = 1 / resolution;

  float2 uv = dispatch_thread_id.xy / resolution;
  float4 value = input.SampleLevel(static_sampler, uv, 0);
  output[dispatch_thread_id.xy] = value;
}