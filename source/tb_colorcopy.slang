#include "tb_fullscreenvert.slangh"

[[vk::binding(0, 0)]]
Texture2D color_map;
[[vk::binding(1, 0)]]
SamplerState static_sampler;

float4 frag(Interpolators i) : SV_TARGET {
  return color_map.Sample(static_sampler, i.uv0);
}
