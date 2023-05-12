#include "fullscreenvert.hlsli"
#include "pbr.hlsli"

//=================================================================================================
//
//  Baking Lab
//  by MJP and David Neubelt
//  http://mynameismjp.wordpress.com/
//
//  All code licensed under the MIT license
//
//=================================================================================================

// The code in this file was originally written by Stephen Hill (@self_shadow),
// who deserves all credit for coming up with this fit and implementing it. Buy
// him a beer next time you see him. :)

// sRGB => XYZ => D65_2_D60 => AP1 => RRT_SAT
static const float3x3 ACESInputMat = {{0.59719, 0.35458, 0.04823},
                                      {0.07600, 0.90834, 0.01566},
                                      {0.02840, 0.13383, 0.83777}};

// ODT_SAT => XYZ => D60_2_D65 => sRGB
static const float3x3 ACESOutputMat = {{1.60475, -0.53108, -0.07367},
                                       {-0.10208, 1.10813, -0.00605},
                                       {-0.00327, -0.07276, 1.07602}};

float3 RRTAndODTFit(float3 v) {
  float3 a = v * (v + 0.0245786f) - 0.000090537f;
  float3 b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
  return a / b;
}

float3 ACESFitted(float3 color) {
  color = mul(ACESInputMat, color);

  // Apply RRT and ODT
  color = RRTAndODTFit(color);

  color = mul(ACESOutputMat, color);

  // Clamp to [0, 1]
  color = saturate(color);

  return color;
}

Texture2D color_map : register(t0, space0);    // Fragment Stage Only
Texture2D bloom_map : register(t1, space0);    // Fragment Stage Only
sampler static_sampler : register(s2, space0); // Immutable Sampler

float4 frag(Interpolators i) : SV_TARGET {
  const float gamma = 2.2;
  const float exposure = 4.5;

  float3 color = color_map.Sample(static_sampler, i.uv0).rgb;
  float3 bloom = bloom_map.Sample(static_sampler, i.uv0).rgb;

  color = (color * exposure) + bloom;

  // Tonemap
  color = ACESFitted(color);

  // Gamma correction
  return float4(pow(color, 1.0 / gamma), 1.0);
}