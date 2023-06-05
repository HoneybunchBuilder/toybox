#include "fullscreenvert.hlsli"

Texture2D color_map : register(t0, space0);    // Fragment Stage Only
sampler static_sampler : register(s1, space0); // Immutable Sampler

float4 frag(Interpolators i) : SV_TARGET {
  float3 color = color_map.Sample(static_sampler, i.uv0).rgb;

  float3 threshold = float3(0.2126, 0.7152, 0.0722);
  if (dot(color, threshold) <= 0.9) {
    color = float3(0.0, 0.0, 0.0);
  }

  return float4(color, 1);
}