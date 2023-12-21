#include "fullscreenvert.hlsli"

Texture2D color_map : register(t0, space0);         // Fragment Stage Only
SamplerState static_sampler : register(s1, space0); // Immutable Sampler

float4 frag(Interpolators i) : SV_TARGET {
  return color_map.Sample(static_sampler, i.uv0);
}
