#include "tb_fullscreenvert.slangh"

[[vk::binding(0, 0)]]
Texture2D depth_map;
[[vk::binding(1, 0)]]
SamplerState static_sampler;

float frag(Interpolators i) : SV_TARGET {
  return depth_map.Sample(static_sampler, i.uv0).r;
}
