#include "fullscreenvert.hlsli"

#include "bloom.h"

Texture2D input : register(t0, space0);
sampler s : register(s1, space0);

[[vk::push_constant]] ConstantBuffer<UpsamplePushConstants> consts
    : register(b2, space0);

float4 frag(Interpolators interp) : SV_TARGET {
  float2 uv = interp.uv0;

  // The filter kernel is applied with a radius, specified input texture
  // coordinates, so that the radius will vary across mip resolutions.
  float x = consts.radius;
  float y = consts.radius;

  // Take 9 samples around current texel:
  // a - b - c
  // d - e - f
  // g - h - i
  // === ('e' is the current texel) ===
  float3 a = input.SampleLevel(s, float2(uv.x - x, uv.y + y), 0).rgb;
  float3 b = input.SampleLevel(s, float2(uv.x, uv.y + y), 0).rgb;
  float3 c = input.SampleLevel(s, float2(uv.x + x, uv.y + y), 0).rgb;

  float3 d = input.SampleLevel(s, float2(uv.x - x, uv.y), 0).rgb;
  float3 e = input.SampleLevel(s, float2(uv.x, uv.y), 0).rgb;
  float3 f = input.SampleLevel(s, float2(uv.x + x, uv.y), 0).rgb;

  float3 g = input.SampleLevel(s, float2(uv.x - x, uv.y - y), 0).rgb;
  float3 h = input.SampleLevel(s, float2(uv.x, uv.y - y), 0).rgb;
  float3 i = input.SampleLevel(s, float2(uv.x + x, uv.y - y), 0).rgb;

  // Apply weighted distribution, by using a 3x3 tent filter:
  //  1   | 1 2 1 |
  // -- * | 2 4 2 |
  // 16   | 1 2 1 |
  float3 upsample = e * 4.0;
  upsample += (b + d + f + h) * 2.0;
  upsample += (a + c + g + i);
  upsample *= 1.0 / 16.0;

  return float4(upsample, 0);
}