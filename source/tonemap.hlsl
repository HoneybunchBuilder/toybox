#include "fullscreenvert.hlsli"

#include "pbr.hlsli"

Texture2D color_map : register(t0, space0);    // Fragment Stage Only
sampler static_sampler : register(s1, space0); // Immutable Sampler

float4 frag(Interpolators i) : SV_TARGET {
  float3 color = color_map.Sample(static_sampler, i.uv0).rgb;

  // Tonemap
  float exposure = 4.5f; // TODO: pass in as a parameter
  color = tonemap(color * exposure);
  color *= 1.0f / tonemap(float3(11.2f, 11.2f, 11.2f));

  // Gamma correction
  float gamma = 1.0f / 2.2f;
  color = pow(color, float3(gamma, gamma, gamma));

  return float4(color, 1);
}