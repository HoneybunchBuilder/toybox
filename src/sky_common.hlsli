#pragma once

#include "sky.hlsli"

float hash(float n)
{
  return frac(sin(n) * 43758.5453123);
}

float noise(float3 x)
{
  float3 f = frac(x);
  float n = dot(floor(x), float3(1.0, 157.0, 113.0));
  return lerp(lerp(lerp(hash(n +   0.0), hash(n +   1.0), f.x),
                   lerp(hash(n + 157.0), hash(n + 158.0), f.x), f.y),
               lerp(lerp(hash(n + 113.0), hash(n + 114.0), f.x),
                   lerp(hash(n + 270.0), hash(n + 271.0), f.x), f.y), f.z);
}

float fbm(float3 p)
{
  const float3x3 m = float3x3(0.0, 1.60, 1.20,
                             -1.6, 0.72, -0.96,
                             -1.2, -0.96, 1.28);

  float f = 0.0;
    f += noise(p) / 2; p = mul(m, p) * 1.1;
    f += noise(p) / 4; p = mul(m, p) * 1.2;
    f += noise(p) / 6; p = mul(m, p) * 1.3;
    f += noise(p) / 12; p = mul(m, p) * 1.4;
    f += noise(p) / 24;
    return f;
}

float3 sky(float time, float cirrus, float cumulus, float3 sun_dir, float3 view_pos)
{
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
  float3 mie = (Kr + Km * (1.0 - g * g) / (2.0 + g * g) / pow(1.0 + g * g - 2.0 * g * mu, 1.5)) / (Br + Bm);

  float view_dir_y = max(view_pos.y, 0.0001);

  float3 day_extinction = exp(-exp(-((view_dir_y + sun_dir.y * 4.0) * (exp(-view_dir_y * 16.0) + 0.1) / 80.0) / Br) * (exp(-view_dir_y * 16.0) + 0.1) * Kr / Br) * exp(-view_dir_y * exp(-view_dir_y * 8.0 ) * 4.0) * exp(-view_dir_y * 2.0) * 4.0;
  float3 night_extinction = float3(1.0 - exp(sun_dir.y), 1.0 - exp(sun_dir.y), 1.0 - exp(sun_dir.y)) * 0.2;
  float3 extinction = lerp(day_extinction, night_extinction, -sun_dir.y * 0.2 + 0.5);
  float3 color = rayleigh * mie * extinction;

  // Cirrus clouds
  float density = smoothstep(1.0 - cirrus, 1.0, fbm(view_pos.xyz / view_dir_y * 2.0 + time * 0.05)) * 0.3;
  color = lerp(color, extinction * 4.0, density * max(view_dir_y, 0.0));
  
  // Cumulus Clouds
  for (int32_t j = 0; j < 3; j++)
  {
    float density = smoothstep(1.0 - cumulus, 1.0, fbm((0.7 + float(j) * 0.01) * view_pos.xyz / view_dir_y + time * 0.3));
    color = lerp(color, extinction * density * 5.0, min(density, 1.0) * max(view_dir_y, 0.0));
  }

  // Dithering Noise
  color += noise(view_dir_y * 1000) * 0.01;

  return color;
}