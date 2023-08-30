#include "common.hlsli"

Texture2D input : register(t0, space0);
RWTexture2D<float4> output : register(u1, space0);
sampler s : register(s2, space0);

#define GROUP_SIZE 256

float3 to_srgb(float3 v) {
  const float inv_gamma = 1.0 / 2.2;
  return pow(v, inv_gamma);
}

float rgb_to_lum(float3 c) { return dot(c, float3(0.2126f, 0.7152f, 0.0722f)); }

float karis_average(float3 c) {
  float luma = rgb_to_lum(to_srgb(c)) * 0.25f;
  return 1.0f / (1.0f + luma);
}

[numthreads(GROUP_SIZE, 1, 1)]
void comp(int3 group_thread_id: SV_GroupThreadID,
          int3 dispatch_thread_id: SV_DispatchThreadID) {
  uint mip_level;
  uint num_levels;
  float2 in_res;
  input.GetDimensions(mip_level, in_res.x, in_res.y, num_levels);

  float2 out_res;
  output.GetDimensions(out_res.x, out_res.y);

  float2 uv = dispatch_thread_id.xy / in_res;
  float2 texel_size = 1 / in_res;
  float x = texel_size.x;
  float y = texel_size.y;

  // Take 13 samples around current texel:
  // a - b - c
  // - j - k -
  // d - e - f0
  // - l - m -
  // g - h - i
  // === ('e' is the current texel) ===
  float3 a = input.SampleLevel(s, float2(uv.x - 2 * x, uv.y + 2 * y), 0).rgb;
  float3 b = input.SampleLevel(s, float2(uv.x, uv.y + 2 * y), 0).rgb;
  float3 c = input.SampleLevel(s, float2(uv.x + 2 * x, uv.y + 2 * y), 0).rgb;

  float3 d = input.SampleLevel(s, float2(uv.x - 2 * x, uv.y), 0).rgb;
  float3 e = input.SampleLevel(s, float2(uv.x, uv.y), 0).rgb;
  float3 f = input.SampleLevel(s, float2(uv.x + 2 * x, uv.y), 0).rgb;

  float3 g = input.SampleLevel(s, float2(uv.x - 2 * x, uv.y - 2 * y), 0).rgb;
  float3 h = input.SampleLevel(s, float2(uv.x, uv.y - 2 * y), 0).rgb;
  float3 i = input.SampleLevel(s, float2(uv.x + 2 * x, uv.y - 2 * y), 0).rgb;

  float3 j = input.SampleLevel(s, float2(uv.x - x, uv.y + y), 0).rgb;
  float3 k = input.SampleLevel(s, float2(uv.x + x, uv.y + y), 0).rgb;
  float3 l = input.SampleLevel(s, float2(uv.x - x, uv.y - y), 0).rgb;
  float3 m = input.SampleLevel(s, float2(uv.x + x, uv.y - y), 0).rgb;

  // Apply weighted distribution:
  // 0.5 + 0.125 + 0.125 + 0.125 + 0.125 = 1
  // a,b,d,e * 0.125
  // b,c,e,f * 0.125
  // d,e,g,h * 0.125
  // e,f,h,i * 0.125
  // j,k,l,m * 0.5
  // This shows 5 square areas that are being sampled. But some of them overlap,
  // so to have an energy preserving downsample we need to make some
  // adjustments. The weights are the distributed, so that the sum of j,k,l,m
  // (e.g.) contribute 0.5 to the final color output. The code below is written
  // to effectively yield this sum. We get:
  // 0.125*5 + 0.03125*4 + 0.0625*4 = 1
  float3 groups[5];
  float3 downsample = 0;
  switch (mip_level) {
  case 0:
    // We are writing to mip 0, so we need to apply Karis average to each block
    // of 4 samples to prevent fireflies (very bright subpixels, leads to
    // pulsating artifacts).
    groups[0] = (a + b + d + e) * (0.125f / 4.0f);
    groups[1] = (b + c + e + f) * (0.125f / 4.0f);
    groups[2] = (d + e + g + h) * (0.125f / 4.0f);
    groups[3] = (e + f + h + i) * (0.125f / 4.0f);
    groups[4] = (j + k + l + m) * (0.5f / 4.0f);
    groups[0] *= karis_average(groups[0]);
    groups[1] *= karis_average(groups[1]);
    groups[2] *= karis_average(groups[2]);
    groups[3] *= karis_average(groups[3]);
    groups[4] *= karis_average(groups[4]);
    downsample = groups[0] + groups[1] + groups[2] + groups[3] + groups[4];
    break;
  default:
    downsample = e * 0.125;
    downsample += (a + c + g + i) * 0.03125;
    downsample += (b + d + f + h) * 0.0625;
    downsample += (j + k + l + m) * 0.125;
    break;
  }

  downsample = max(downsample, 0.00001f); // To prevent propogating zeroes

  output[uv * out_res] = float4(downsample, 0);
}