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

float tonemap_unreal_x(float x) {
  // Gamma 2.2 correction is baked in, don't use with sRGB conversion!
  return x / (x + 0.155) * 1.019;
}

float tonemap_aces_x(float x) {
  // Narkowicz 2015, "ACES Filmic Tone Mapping Curve"
  const float a = 2.51;
  const float b = 0.03;
  const float c = 2.43;
  const float d = 0.59;
  const float e = 0.14;
  return (x * (a * x + b)) / (x * (c * x + d) + e);
}

float3 tonemap_unreal(float3 rgb) {
  return float3(tonemap_unreal_x(rgb.r), tonemap_unreal_x(rgb.g),
                tonemap_unreal_x(rgb.b));
}

float3 tonemap_aces(float3 rgb) {
  return float3(tonemap_aces_x(rgb.r), tonemap_aces_x(rgb.g),
                tonemap_aces_x(rgb.b));
}

float3 gamma_correct(float3 rgb) {
  float3 lo = rgb * 12.92;
  float3 hi =
      pow(abs(rgb), float3(1.0 / 2.4, 1.0 / 2.4, 1.0 / 2.4)) * 1.055 - 0.055;
  return lerp(
      hi, lo,
      float3(rgb.x <= 0.0031308, rgb.y <= 0.0031308, rgb.z <= 0.0031308));
}

Texture2D color_map : register(t0, space0);    // Fragment Stage Only
Texture2D bloom_map : register(t1, space0);    // Fragment Stage Only
sampler static_sampler : register(s2, space0); // Immutable Sampler

float4 frag(Interpolators i) : SV_TARGET {
  const float lum = 1; // TODO: Look up from texture

  float3 color = color_map.Sample(static_sampler, i.uv0).rgb;
  float3 bloom = bloom_map.Sample(static_sampler, i.uv0).rgb;

  float3 yxy = rgb_to_yxy(color);
  yxy.x /= (9.6 * lum + 0.0001);

  color = yxy_to_rgb(yxy);

  color = tonemap_aces(color);
  return float4(color, 1);
}