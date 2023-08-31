#include "common.hlsli"

Texture2D input : register(t0, space0);
RWTexture2D<float4> output : register(u1, space0);

#define GROUP_SIZE 64

static const int radius = 2;
static const int weight_count = (radius * 2) + 1;
static const int cache_size = GROUP_SIZE + 2 * radius;
static const int load = (cache_size + (GROUP_SIZE - 1)) / GROUP_SIZE;

groupshared float4 cache[cache_size];

[numthreads(1, GROUP_SIZE, 1)]
void comp(int3 group_id: SV_GroupID, int3 group_thread_id: SV_GroupThreadID,
          int3 dispatch_thread_id: SV_DispatchThreadID) {
  const float weight[weight_count] = {
    0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216,
  };

  int2 size;
  input.GetDimensions(size.x, size.y);
  int2 pixel_coord = dispatch_thread_id.xy;

  int origin = int(group_id.y) * GROUP_SIZE - radius;
  for (int i = 0; i < load; ++i) {
    int local = int(group_thread_id.y) * load + i;
    if (local < cache_size) {
      int pc = origin + local;
      if (pc >= 0 && pc < size.x) {
        cache[local] = input[int2(pixel_coord.x, pc)];
      }
    }
  }

  DeviceMemoryBarrier();
  GroupMemoryBarrierWithGroupSync();

  if (pixel_coord.x < size.x && pixel_coord.y < size.y) {
    float4 result = 0;

    for (int i = 0; i < weight_count; ++i) {
      int2 pc = pixel_coord + int2(0, i - radius);
      if (pc.y < 0) {
        pc.y = 0;
      }
      if (pc.y >= size.y) {
        pc.y = size.y - 1;
      }

      int local = pc.y - origin;
      result += weight[i] * cache[local];
    }

    output[dispatch_thread_id.xy] = result;
  }
}