#include "fullscreenvert.hlsli"
#include "pbr.hlsli"

float3 rgb_to_xyz(float3 rgb) {
  // Reference:
  // RGB/XYZ Matrices
  // http://www.brucelindbloom.com/index.html?Eqn_RGB_XYZ_Matrix.html
  float3 xyz;
  xyz.x = dot(float3(0.4124564, 0.3575761, 0.1804375), rgb);
  xyz.y = dot(float3(0.2126729, 0.7151522, 0.0721750), rgb);
  xyz.z = dot(float3(0.0193339, 0.1191920, 0.9503041), rgb);
  return xyz;
}

float3 xyz_to_yxy(float3 xyz) {
  // Reference:
  // http://www.brucelindbloom.com/index.html?Eqn_XYZ_to_xyY.html
  float inv = 1.0 / dot(xyz, float3(1.0, 1.0, 1.0));
  return float3(xyz.y, xyz.x * inv, xyz.y * inv);
}

float3 yxy_to_xyz(float3 yxy) {
  // Reference:
  // http://www.brucelindbloom.com/index.html?Eqn_xyY_to_XYZ.html
  float3 xyz;
  xyz.x = yxy.x * yxy.y / yxy.z;
  xyz.y = yxy.x;
  xyz.z = yxy.x * (1.0 - yxy.y - yxy.z) / yxy.z;
  return xyz;
}

float3 xyz_to_rgb(float3 xyz) {
  float3 rgb;
  rgb.x = dot(float3(3.2404542, -1.5371385, -0.4985314), xyz);
  rgb.y = dot(float3(-0.9692660, 1.8760108, 0.0415560), xyz);
  rgb.z = dot(float3(0.0556434, -0.2040259, 1.0572252), xyz);
  return rgb;
}

float3 rgb_to_yxy(float3 rgb) { return xyz_to_yxy(rgb_to_xyz(rgb)); }

float3 yxy_to_rgb(float3 yxy) { return xyz_to_rgb(yxy_to_xyz(yxy)); }

float3 tonemap_unreal(float3 rgb) { return rgb / (rgb + 0.155) * 1.019; }

Texture2D color_map : register(t0, space0);
Texture2D bloom_map : register(t1, space0);
RWStructuredBuffer<float> lum_avg : register(u2, space0);
SamplerState static_sampler : register(s3, space0);

float4 frag(Interpolators i) : SV_TARGET {
  float lum = lum_avg[0];

  float3 color = color_map.Sample(static_sampler, i.uv0).rgb;
  float3 bloom = bloom_map.Sample(static_sampler, i.uv0).rgb;

  float bloom_strength = 0.04f; // Could be a parameter
  float3 yxy = rgb_to_yxy(lerp(color, bloom, bloom_strength));
  yxy.x /= (9.6 * lum + 0.0001);

  color = yxy_to_rgb(yxy);

  return float4(tonemap_unreal(color), 1);
}
