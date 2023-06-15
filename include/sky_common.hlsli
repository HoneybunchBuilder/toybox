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

float3 sky(float time, float cirrus, float cumulus, float3 sun_dir,
           float3 view_pos) {
  /* alternate settings
  // Original
  const float Br = 0.0025;
  const float Bm = 0.0003;
  const float g = 0.9800;

  // Softer
  const float Br = 0.0005;
  const float Bm = 0.0003;
  const float g = 0.9200;
  */

  const float Br = 0.0020;
  const float Bm = 0.0009;
  const float g = 0.9200;

  const float3 nitrogen = float3(0.650, 0.570, 0.475);
  const float3 Kr = Br / pow(nitrogen, float3(4.0, 4.0, 4.0));
  const float3 Km = Bm / pow(nitrogen, float3(0.84, 0.84, 0.84));

  // Atmosphere Scattering
  float mu = dot(normalize(view_pos), normalize(sun_dir));
  float rayleigh = 3.0 / (8.0 * 3.14) * (1.0 + mu * mu);
  float3 mie = (Kr + Km * (1.0 - g * g) / (2.0 + g * g) /
                         pow(1.0 + g * g - 2.0 * g * mu, 1.5)) /
               (Br + Bm);

  float view_dir_y = max(view_pos.y, 0.0001);

  float3 day_extinction = exp(-exp(-((view_dir_y + sun_dir.y * 4.0) *
                                     (exp(-view_dir_y * 16.0) + 0.1) / 80.0) /
                                   Br) *
                              (exp(-view_dir_y * 16.0) + 0.1) * Kr / Br) *
                          exp(-view_dir_y * exp(-view_dir_y * 8.0) * 4.0) *
                          exp(-view_dir_y * 2.0) * 4.0;
  float3 night_extinction =
      float3(1.0 - exp(sun_dir.y), 1.0 - exp(sun_dir.y), 1.0 - exp(sun_dir.y)) *
      0.2;
  float3 extinction =
      lerp(day_extinction, night_extinction, -sun_dir.y * 0.2 + 0.5);
  float3 color = rayleigh * mie * extinction;

  // Cirrus clouds
  float density =
      smoothstep(1.0 - cirrus, 1.0,
                 fbm(view_pos.xyz / view_dir_y * 2.0 + time * 0.05)) *
      0.3;
  color = lerp(color, extinction * 4.0, density * max(view_dir_y, 0.0));

  // Cumulus Clouds
  for (int32_t j = 0; j < 3; j++) {
    float density = smoothstep(
        1.0 - cumulus, 1.0,
        fbm((0.7 + float(j) * 0.01) * view_pos.xyz / view_dir_y + time * 0.3));
    color = lerp(color, extinction * density * 5.0,
                 min(density, 1.0) * max(view_dir_y, 0.0));
  }

  return color * 0.2;
}