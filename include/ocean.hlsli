#pragma once

#include "common.hlsli"
#include "pi.h"

#define TB_WAVE_MAX 12

// To avoid struct packing issues
typedef float4 OceanWave; // xy = dir, z = steep, w = wavelength

typedef struct OceanData {
  float4 time_waves; // x = time, y = wave count
  OceanWave wave[TB_WAVE_MAX];
} OceanData;

typedef struct OceanPushConstants {
  float4x4 m;
} OceanPushConstants;

// If not in a shader, make a quick static assert check
#ifndef __HLSL_VERSION
_Static_assert(sizeof(OceanPushConstants) <= PUSH_CONSTANT_BYTES,
               "Too Many Push Constants");
#endif

#ifdef __HLSL_VERSION
float gerstner_wave(OceanWave wave, float time, float2 pos) {
  float steepness = wave.z;
  float k = 2 * PI / wave.w;
  float c = sqrt(9.8 / k);
  float2 d = normalize(wave.xy);
  float f = k * (dot(d, pos) - c * time);
  float a = steepness / k;

  float sinf = sin(f);
  float cosf = cos(f);

  return a * sinf;
}

float iter_wave_height(float2 pos, OceanData data) {
  float time = data.time_waves.x;
  uint count = (uint)data.time_waves.y;
  if (count > TB_WAVE_MAX) {
    count = TB_WAVE_MAX;
  }

  float weight = 1.0f;
  float time_mul = 1.0f;
  float value_sum = 0.0f;
  float weight_sum = 0.0f;

  for (uint i = 0; i < count; ++i) {
    float wave = gerstner_wave(data.wave[i], time * time_mul, pos);

    value_sum += wave * weight;
    weight_sum += weight;

    wave *= 0.82;
    time_mul *= 1.09;
  }
  return value_sum / weight_sum;
}

float3 calc_wave_pos(float2 pos, OceanData data) {
  float H = iter_wave_height(pos, data);
  return float3(pos.x, H, pos.y);
}

float3 calc_wave_normal(float2 pos, OceanData data) {
  float e = 0.01;
  float2 ex = float2(e, 0);
  float H = iter_wave_height(pos, data);
  float3 a = float3(pos.x, H, pos.y);
  return normalize(
      cross(a - float3(pos.x - e, iter_wave_height(pos - ex, data), pos.y),
            a - float3(pos.x, iter_wave_height(pos + ex.yx, data), pos.y + e)));
}
#endif
