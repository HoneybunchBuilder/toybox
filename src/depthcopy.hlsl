#include "fullscreenvert.hlsli"

Texture2D depth_map : register(t0, space0);    // Fragment Stage Only
sampler static_sampler : register(s1, space0); // Immutable Sampler

float frag(Interpolators i) : SV_TARGET {
  return depth_map.Sample(static_sampler, i.uv0).r;
}