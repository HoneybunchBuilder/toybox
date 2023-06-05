#include "luminance.h"

#define GROUP_SIZE 256
#define THREADS_X 256
#define THREADS_Y 1

RWBuffer<uint> input : register(u0, space0);
RWTexture2D<float> output : register(u1, space0);

[[vk::push_constant]]
ConstantBuffer<LuminancePushConstants> consts : register(b2, space0);

groupshared uint histogram_shared[GROUP_SIZE];

[numthreads(THREADS_X, THREADS_Y, 1)]
void comp(int group_idx: SV_GroupIndex) {
  const float min_log_lum = consts.params.x;
  const float log_lum_range = consts.params.y;
  const float time = consts.params.z;
  const float num_pixels = consts.params.w;

  // Get the count from the hist buffer
  uint count_this_bin = input[group_idx];
  histogram_shared[group_idx] = count_this_bin * group_idx;

  GroupMemoryBarrierWithGroupSync();

  // Clear the input buffer now that we're done with it
  input[group_idx] = 0;

  for (uint cutoff = (GROUP_SIZE >> 1); cutoff > 0; cutoff >>= 1) {
    if (uint(group_idx) < cutoff) {
      histogram_shared[group_idx] += histogram_shared[group_idx + cutoff];
    }
    GroupMemoryBarrierWithGroupSync();
  }

  // only need to do this once
  if (group_idx == 0) {
    float weighted_log_avg =
        (histogram_shared[0] / max(num_pixels - float(count_this_bin), 1.0)) -
        1.0;

    float weighted_avg_lum =
        exp2(((weighted_log_avg / 254.0) * log_lum_range) + min_log_lum);

    float lum_last_frame = output[int2(0, 0)].r;
    float adapted_lum =
        lum_last_frame + (weighted_avg_lum - lum_last_frame) * time;
    output[int2(0, 0)] = adapted_lum;
  }
}