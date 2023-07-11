// Based on "A Practical Analytic Model for Daylight" aka The Preetham Model,
// the de facto standard analytic skydome model
// http://www.cs.utah.edu/~shirley/papers/sunsky/sunsky.pdf
// Original implementation by Simon Wallner:
// http://www.simonwallner.at/projects/atmospheric-scattering Improved by Martin
// Upitis:
// http://blenderartists.org/forum/showthread.php?245954-preethams-sky-impementation-HDR
// Three.js integration by zz85: http://twitter.com/blurspline /
// https://github.com/zz85 / http://threejs.org/examples/webgl_shaders_sky.html

#pragma once

#include "sky.hlsli"

float mod289(float x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
float4 mod289(float4 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
float4 perm(float4 x) { return mod289(((x * 34.0) + 1.0) * x); }

float noise(float3 p) {
  float3 a = floor(p);
  float3 d = p - a;
  d = d * d * (3.0 - 2.0 * d);

  float4 b = a.xxyy + float4(0.0, 1.0, 0.0, 1.0);
  float4 k1 = perm(b.xyxy);
  float4 k2 = perm(k1.xyxy + b.zzww);

  float4 c = k2 + a.zzzz;
  float4 k3 = perm(c);
  float4 k4 = perm(c + 1.0);

  float4 o1 = frac(k3 * (1.0 / 41.0));
  float4 o2 = frac(k4 * (1.0 / 41.0));

  float4 o3 = o2 * d.z + o1 * (1.0 - d.z);
  float2 o4 = o3.yw * d.x + o3.xz * (1.0 - d.x);

  return o4.y * d.y + o4.x * (1.0 - d.y);
}

float fbm(float3 x) {
  float v = 0.0;
  float a = 0.5;
  float3 shift = float3(100, 100, 100);
  for (int i = 0; i < 5; ++i) {
    v += a * noise(x);
    x = x * 2.0 + shift;
    a *= 0.5;
  }
  return v;
}

float3 total_rayleigh(float3 lambda, float refractive_index,
                      float depolarization_factor, float num_molecules) {
  return (8.0 * pow(PI, 3.0) * pow(pow(refractive_index, 2.0) - 1.0, 2.0) *
          (6.0 + 3.0 * depolarization_factor)) /
         (3.0 * num_molecules * pow(lambda, float3(4.0, 4.0, 4.0)) *
          (6.0 - 7.0 * depolarization_factor));
}

float3 total_mie(float3 lambda, float3 K, float T, float mie_v) {
  float c = 0.2 * T * 10e-18;
  return 0.434 * c * PI * pow((2.0 * PI) / lambda, mie_v - 2.0) * K;
}

float rayleigh_phase(float cos_theta) {
  return (3.0 / (16.0 * PI)) * (1.0 + pow(cos_theta, 2.0));
}

float henyey_greenstein_phase(float cos_theta, float g) {
  return (1.0 / (4.0 * PI)) *
         ((1.0 - pow(g, 2.0)) /
          pow(1.0 - 2.0 * g * cos_theta + pow(g, 2.0), 1.5));
}

float sun_intensity(float zenith_angle_cos, float sun_intensity_factor,
                    float sun_intensity_falloff_steepness) {
  float cutoff_angle = PI / 1.95; // Earth shadow hack
  return sun_intensity_factor *
         max(0.0, 1.0 - exp(-((cutoff_angle - acos(zenith_angle_cos)) /
                              sun_intensity_falloff_steepness)));
}

float3 sky(float time, float cirrus, float cumulus, float3 sun_dir,
           float3 view_pos) {
  // TODO: Paramaterize
  float turbidity = 2.0f;
  float rayleigh = 1.0f;
  float mie_coefficient = 0.005;
  float mie_directional_g = 0.8;
  float inclination = 0.49;
  float azimuth = 0.25;
  float refractive_index = 1.0003;
  float depolarization_factor = 0.035;
  float num_molecules = 2.542e25;
  float3 primaries = float3(6.8e-7, 5.5e-7, 4.5e-7);
  float3 mie_k_coefficient = float3(0.686, 0.678, 0.666);
  float mie_v = 4.0;
  float rayleigh_zenith_length = 8.4e3;
  float mie_zenith_length = 1.25e3;
  float sun_intensity_factor = 1000.0;
  float sun_intensity_falloff_steepness = 1.5;
  float sun_angular_diameter_degrees = 0.0093333;

  const float3 up = float3(0, 1, 0);

  float view_dir_y = max(view_pos.y, 0.0001);

  // Rayleigh coefficient
  float sunfade = 1.0 - clamp(1.0 - exp((view_dir_y / 450000.0)), 0.0, 1.0);
  float rayleigh_coefficient = rayleigh - (1.0 * (1.0 - sunfade));
  float3 beta_r = total_rayleigh(primaries, refractive_index,
                                 depolarization_factor, num_molecules) *
                  rayleigh_coefficient;

  // Mie coefficient
  float3 beta_m = total_mie(primaries, mie_k_coefficient, turbidity, mie_v) *
                  mie_coefficient;

  // Optical length, cutoff angle at 90 to avoid singularity
  float zenith_angle = acos(max(0.0, dot(up, normalize(view_pos))));
  float denom = cos(zenith_angle) +
                0.15 * pow(93.885 - ((zenith_angle * 180.0) / PI), -1.253);
  float s_r = rayleigh_zenith_length / denom;
  float s_m = mie_zenith_length / denom;

  // Combined extinction factor
  float3 fex = exp(-(beta_r * s_r + beta_m * s_m));

  // In-scattering
  float cos_theta = dot(normalize(view_pos), sun_dir);
  float3 beta_r_theta = beta_r * rayleigh_phase(cos_theta * 0.5 + 0.5);
  float3 beta_m_theta =
      beta_m * henyey_greenstein_phase(cos_theta, mie_directional_g);
  float sun_e = sun_intensity(dot(sun_dir, up), sun_intensity_factor,
                              sun_intensity_falloff_steepness);
  float3 Lin = pow(sun_e * ((beta_r_theta + beta_m_theta) / (beta_r + beta_m)) *
                       (1.0 - fex),
                   float3(1.5, 1.5, 1.5));
  Lin *= lerp(
      float3(1.0, 1.0, 1.0),
      pow(sun_e * ((beta_r_theta + beta_m_theta) / (beta_r + beta_m)) * fex,
          float3(0.5, 0.5, 0.5)),
      clamp(pow(1.0 - dot(up, sun_dir), 5.0), 0.0, 1.0));

  // Composition + solar disc
  float sun_angluar_diameter_cos = cos(sun_angular_diameter_degrees);
  float sundisk = smoothstep(sun_angluar_diameter_cos,
                             sun_angluar_diameter_cos + 0.00002, cos_theta);
  float3 L0 = float3(0.1, 0.1, 0.1) * fex;
  L0 += sun_e * 19000.0 * fex * sundisk;
  float3 color = Lin + L0;
  color *= 0.04;
  color += float3(0.0, 0.001, 0.0025) * 0.3;
  // color = pow(color, 1.0 / (1.2 + (1.2 * sunfade)));

  // Cirrus clouds
  float density =
      smoothstep(1.0 - cirrus, 1.0,
                 fbm(view_pos.xyz / view_dir_y * 2.0 + time * 0.05)) *
      0.3;
  color = lerp(color, color * 4.0, density * max(view_dir_y, 0.0));

  // Cumulus Clouds
  for (int32_t j = 0; j < 3; j++) {
    float density = smoothstep(
        1.0 - cumulus, 1.0,
        fbm((0.7 + float(j) * 0.01) * view_pos.xyz / view_dir_y + time * 0.3));
    color = lerp(color, color * density * 5.0,
                 min(density, 1.0) * max(view_dir_y, 0.0));
  }

  // Must reduce color to avoid blow out during tonemapping
  return color * 0.05f;
}