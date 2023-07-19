#include "bloom.h"
#include "fullscreenvert.hlsli"

Texture2D input : register(t0, space0);
sampler s : register(s1, space0);

float4 frag(Interpolators interp) : SV_TARGET {
  float2 resolution;
  input.GetDimensions(resolution.x, resolution.y);

  float2 uv = interp.uv0;
  float2 texel_size = 1 / resolution;
  float x = texel_size.x;
  float y = texel_size.y;

  // Take 13 samples around current texel:
  // a - b - c
  // - j - k -
  // d - e - f
  // - l - m -
  // g - h - i
  // === ('e' is the current texel) ===
  float3 a = input.Sample(s, float2(uv.x - 2 * x, uv.y + 2 * y)).rgb;
  float3 b = input.Sample(s, float2(uv.x, uv.y + 2 * y)).rgb;
  float3 c = input.Sample(s, float2(uv.x + 2 * x, uv.y + 2 * y)).rgb;

  float3 d = input.Sample(s, float2(uv.x - 2 * x, uv.y)).rgb;
  float3 e = input.Sample(s, float2(uv.x, uv.y)).rgb;
  float3 f = input.Sample(s, float2(uv.x + 2 * x, uv.y)).rgb;

  float3 g = input.Sample(s, float2(uv.x - 2 * x, uv.y - 2 * y)).rgb;
  float3 h = input.Sample(s, float2(uv.x, uv.y - 2 * y)).rgb;
  float3 i = input.Sample(s, float2(uv.x + 2 * x, uv.y - 2 * y)).rgb;

  float3 j = input.Sample(s, float2(uv.x - x, uv.y + y)).rgb;
  float3 k = input.Sample(s, float2(uv.x + x, uv.y + y)).rgb;
  float3 l = input.Sample(s, float2(uv.x - x, uv.y - y)).rgb;
  float3 m = input.Sample(s, float2(uv.x + x, uv.y - y)).rgb;

  // Apply weighted distribution:
  // 0.5 + 0.125 + 0.125 + 0.125 + 0.125 = 1
  // a,b,d,e * 0.125
  // b,c,e,f * 0.125
  // d,e,g,h * 0.125
  // e,f,h,i * 0.125
  // j,k,l,m * 0.5
  // This shows 5 square areas that are being sampled. But some of them
  // overlap, so to have an energy preserving downsample we need to make some
  // adjustments. The weights are the distributed, so that the sum of j,k,l,m
  // (e.g.) contribute 0.5 to the final color output. The code below is
  // written to effectively yield this sum. We get: 0.125*5 + 0.03125*4 +
  // 0.0625*4 = 1
  float3 downsample = e * 0.125;
  downsample += (a + c + g + i) * 0.03125;
  downsample += (b + d + f + h) * 0.0625;
  downsample += (j + k + l + m) * 0.125;
  downsample = max(downsample, 0.00001f); // To prevent artifacts

  return float4(downsample, 0);
}