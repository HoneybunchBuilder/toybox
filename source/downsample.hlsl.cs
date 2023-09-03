#include "common.hlsli"

Texture2D input : register(t0, space0);
RWTexture2D<float4> output : register(u1, space0);

[numthreads(16, 16, 1)]
void comp(int3 group_thread_id: SV_GroupThreadID,
          int3 dispatch_thread_id: SV_DispatchThreadID) {
  float2 in_res;
  input.GetDimensions(in_res.x, in_res.y);

  float2 out_res;
  output.GetDimensions(out_res.x, out_res.y);

  float2 uv = dispatch_thread_id.xy / out_res;
  int2 src_coord = uv * in_res;

  // Downsample with box filter
  float4 a = input[src_coord + int2(-2, -2)];
  float4 b = input[src_coord + int2(0, -2)];
  float4 c = input[src_coord + int2(2, -2)];
  float4 d = input[src_coord + int2(-1, -1)];
  float4 e = input[src_coord + int2(1, -1)];
  float4 f = input[src_coord + int2(-2, 0)];
  float4 g = input[src_coord];
  float4 h = input[src_coord + int2(2, 0)];
  float4 i = input[src_coord + int2(-1, 1)];
  float4 j = input[src_coord + int2(1, 1)];
  float4 k = input[src_coord + int2(-2, 2)];
  float4 l = input[src_coord + int2(0, 2)];
  float4 m = input[src_coord + int2(2, 2)];

  float4 downsample = e * 0.125;
  downsample += (a + c + g + i) * 0.03125;
  downsample += (b + d + f + h) * 0.0625;
  downsample += (j + k + l + m) * 0.125;

  output[dispatch_thread_id.xy] = downsample;
}