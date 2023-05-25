#include "common.hlsli"

RWTexture2D<float4> input : register(u0, space0);
RWTexture2D<float4> output : register(u1, space0);

[[vk::push_constant]]
ConstantBuffer<BlurPushConstants> consts : register(b2, space0);

// Should this be adjusted per device?
#define N 256

[numthreads(N, 1, 1)]
void comp(int3 group_thread_id: SV_GroupThreadID,
          int3 dispatch_thread_id: SV_DispatchThreadID) {
#define WEIGHT_COUNT 5
  const float weight[WEIGHT_COUNT] = { 0.227027, 0.1945946, 0.1216216, 0.054054,
                                       0.016216 };
  const float radius = consts.radius;

  float2 input_len;
  input.GetDimensions(input_len.x, input_len.y);
  float2 tex_offset = 1 / input_len;

#define BOUNDS(p) min(p, input_len - 1)

  int2 sample_point = dispatch_thread_id.xy;
  float4 result = input[BOUNDS(sample_point)];

  if (consts.horizontal > 0.0f)
  {
    for (int i = 1; i < WEIGHT_COUNT; ++i)
    {
      result +=
          input[BOUNDS(sample_point + int2(tex_offset.x * i, 0))] * weight[i];
      result +=
          input[BOUNDS(sample_point - int2(tex_offset.x * i, 0))] * weight[i];
    }
  }
  else
  {
    for (int i = 1; i < WEIGHT_COUNT; ++i)
    {
      result +=
          input[BOUNDS(sample_point + int2(0, tex_offset.y * i))] * weight[i];
      result +=
          input[BOUNDS(sample_point - int2(0, tex_offset.y * i))] * weight[i];
    }
  }

  output[dispatch_thread_id.xy] = result;
#undef WEIGHT_COUNT
}