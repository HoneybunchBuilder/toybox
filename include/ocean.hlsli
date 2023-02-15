#pragma once

#include "pi.h"

#define TB_WAVE_MAX 4

typedef struct OceanWave {
  float steepness;
  float wavelength;
  float2 direction;
} OceanWave;

typedef struct OceanData {
  float time;
  int32_t wave_count;
  OceanWave wave[TB_WAVE_MAX];
  float4x4 m;
} OceanData;

#ifdef __HLSL_VERSION
float3 gerstner_wave(OceanWave wave, float3 p, float time, inout float3 tangent,
                     inout float3 binormal) {
  float steepness = wave.steepness;
  float k = 2 * PI / wave.wavelength;
  float c = sqrt(9.8 / k);
  float2 d = normalize(wave.direction);
  float f = k * (dot(d, p.xz) - c * time);
  float a = steepness / k;

  float sinf = sin(f);
  float cosf = cos(f);

  tangent += float3(-d.x * d.x * (steepness * sinf), d.x * (steepness * cosf),
                    -d.x * d.y * (steepness * sinf));
  binormal += float3(-d.x * d.y * (steepness * sinf), d.y * (steepness * cosf),
                     -d.y * d.y * (steepness * sinf));
  return float3(d.x * (a * cosf), a * sinf, d.y * (a * cosf));
}

float3 calc_wave_pos(int3 local_pos, float4x4 m, float time,
                     inout float3 tangent, inout float3 binormal) {
  OceanWave wave_0 = {0.3, 50, float2(-0.8, 0.5)};
  OceanWave wave_1 = {0.21, 30, float2(0.9, 0.6)};
  OceanWave wave_2 = {0.13, 20, float2(0.1, 0.6)};
  OceanWave wave_3 = {0.07, 5, float2(-0.8, 0.3)};
  OceanWave wave_4 = {0.04, 1, float2(-0.9, -0.1)};

  float3 pos = mul(float4(local_pos, 1), m).xyz;

  pos += gerstner_wave(wave_0, pos, time, tangent, binormal);
  pos += gerstner_wave(wave_1, pos, time, tangent, binormal);
  pos += gerstner_wave(wave_2, pos, time, tangent, binormal);
  pos += gerstner_wave(wave_3, pos, time, tangent, binormal);
  pos += gerstner_wave(wave_4, pos, time, tangent, binormal);

  return pos;
}
#endif