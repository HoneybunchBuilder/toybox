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
  int4 d = int4(1, 1, -1, 0);

  float4 s = input[src_coord - d.xy];
  s += input[src_coord - d.wy] * 2.0f;
  s += input[src_coord - d.zy];
  s += input[src_coord + d.zw] * 2.0f;
  s += input[src_coord] * 4.0f;
  s += input[src_coord + d.xw] * 2.0f;
  s += input[src_coord + d.zy];
  s += input[src_coord + d.wy] * 2.0f;
  s += input[src_coord + d.xy];

  output[dispatch_thread_id.xy] = s * (1.0f / 16.0f);
}