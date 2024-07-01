#include "tb_luminance.h"

#define GROUP_SIZE 256
#define THREADS_X 16
#define THREADS_Y 16

#define EPSILON 0.000001
// Taken from RTR vol 4 pg. 278
#define RGB_TO_LUM float3(0.2125, 0.7154, 0.0721)

Texture2D input : register(t0, space0);
RWStructuredBuffer<uint> output : register(u1, space0);
sampler static_sampler : register(s2, space0);

[[vk::push_constant]]
ConstantBuffer<TbLuminancePushConstants> consts : register(b3, space0);

groupshared uint histogram_shared[GROUP_SIZE] ;

// Histogram bin lookup based on color and luminance range
uint color_to_hist(float3 color, float min_log_lum, float inv_log_lum_range)
{
  float lum = dot(color, RGB_TO_LUM); // convert RGB to Luminance
  if (lum < EPSILON)
  {
    return 0; // avoid taking log of 0
  }

  // calc the log2 of luminance and clamp to 0 to 1 range
  float log_lum = saturate((log2(lum) - min_log_lum) * inv_log_lum_range);

  // remap [0, 1] to [1, 255]
  return uint(log_lum * 254.0 + 1.0);
}

[numthreads(THREADS_X, THREADS_Y, 1)]
void comp(int group_idx: SV_GroupIndex,
          int3 dispatch_thread_id: SV_DispatchThreadID) {
  const float4 params = consts.params;

// Init and wait for all threads in the group to reach this point
histogram_shared[group_idx] = 0;
GroupMemoryBarrierWithGroupSync();

float2 dimensions = params.zw;

// Ignore any threads that map outside the image
if (dispatch_thread_id.x < dimensions.x &&
    dispatch_thread_id.y < dimensions.y)
{
  float2 uv = dispatch_thread_id.xy / dimensions;

  float3 color = input.SampleLevel(static_sampler, uv, 0).rgb;
  uint idx = color_to_hist(color, params.x, params.y);
  InterlockedAdd(histogram_shared[idx], 1);
}

GroupMemoryBarrierWithGroupSync();

// Contention here should be low
InterlockedAdd(output[group_idx], histogram_shared[group_idx]);
}