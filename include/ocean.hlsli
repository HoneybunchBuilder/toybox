#pragma once

#include "common.hlsli"
#include "pi.h"

#define TB_WAVE_MAX 8

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
void gerstner_wave(OceanWave wave, float time, float2 pos, out float h,
                   out float3 tangent, out float3 binormal) {
  float steepness = saturate(wave.z);
  float k = 2 * PI / wave.w;
  float c = sqrt(9.8 / k);
  float2 d = normalize(wave.xy);
  float f = k * (dot(d, pos) - c * time);
  float a = steepness / k;

  float sinf = sin(f);
  float cosf = cos(f);

  tangent = float3(-d.x * d.y * (steepness * sinf), d.y * (steepness * cosf),
                   -d.y * d.y * (steepness * sinf));
  binormal = float3(-d.x * d.x * (steepness * sinf), d.x * (steepness * cosf),
                    -d.x * d.y * (steepness * sinf));
  h = a * sinf;
}

void calc_wave_pos(float2 pos, OceanData data, inout float3 position,
                   inout float3 tangent, inout float3 binormal) {
  float time = data.time_waves.x;
  uint count = (uint)data.time_waves.y;
  if (count > TB_WAVE_MAX) {
    count = TB_WAVE_MAX;
  }

  float height = 0.0f;
  float weight = 1.0f;
  float time_mul = 1.0f;
  float weight_sum = 0;

  for (uint i = 0; i < count; ++i) {
    float h = 0;
    float3 t = 0;
    float3 b = 0;

    gerstner_wave(data.wave[i], time * time_mul, pos, h, t, b);

    height += h * weight;
    tangent += t * weight;
    binormal += b * weight;
    weight_sum += weight;

    weight *= 0.82;
    time_mul *= 1.07;
  }

  height = height / weight_sum;
  position = float3(pos.x, height, pos.y);
  tangent = normalize(tangent / weight_sum);
  binormal = normalize(binormal / weight_sum);
}
#endif
