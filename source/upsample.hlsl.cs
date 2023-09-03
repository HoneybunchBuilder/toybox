#include "bloom.h"

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

  // Upsample with tent filter
  float r = 0.005;

  float4 a = input[src_coord + int2(-r, r)];
  float4 b = input[src_coord + int2(0, r)];
  float4 c = input[src_coord + int2(r, r)];
  float4 d = input[src_coord + int2(-r, r)];
  float4 e = input[src_coord + int2(0, 0)];
  float4 f = input[src_coord + int2(r, r)];
  float4 g = input[src_coord + int2(-r, -r)];
  float4 h = input[src_coord + int2(0, -r)];
  float4 i = input[src_coord + int2(r, -r)];

  float4 upsample = e * 4.0;
  upsample += (b + d + f + h) * 2;
  upsample += (a + c + g + i);
  upsample *= 1.0f / 16.0f;

  output[dispatch_thread_id.xy] = upsample;
}