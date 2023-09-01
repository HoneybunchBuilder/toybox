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
  float4 A = input[src_coord + int2(-2, -2)];
  float4 B = input[src_coord + int2(0, -2)];
  float4 C = input[src_coord + int2(2, -2)];
  float4 D = input[src_coord + int2(-1, -1)];
  float4 E = input[src_coord + int2(1, -1)];
  float4 F = input[src_coord + int2(-2, 0)];
  float4 G = input[src_coord];
  float4 H = input[src_coord + int2(2, 0)];
  float4 I = input[src_coord + int2(-1, 1)];
  float4 J = input[src_coord + int2(1, 1)];
  float4 K = input[src_coord + int2(-2, 2)];
  float4 L = input[src_coord + int2(0, 2)];
  float4 M = input[src_coord + int2(2, 2)];

  float2 div = (1.0f / 4.0f) * float2(0.5f, 0.125f);

  float4 o = (D + E + I + J) * div.x;
  o += (A + B + G + F) * div.y;
  o += (B + C + H + G) * div.y;
  o += (F + G + L + K) * div.y;
  o += (G + H + M + L) * div.y;

  output[dispatch_thread_id.xy] = o;
}