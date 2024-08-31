#pragma once

#include "common.hlsli"
#include "tb_pi.h"

#define TB_WAVE_MAX 8

// To avoid struct packing issues
typedef float4 TbOceanWave; // xy = dir, z = steep, w = wavelength

typedef struct OceanData {
  float4 time_waves; // x = time, y = wave count
  TbOceanWave wave[TB_WAVE_MAX];
} OceanData;

typedef struct OceanPushConstants {
  float4x4 m;
} OceanPushConstants;

// If not in a shader, make a quick static assert check
#ifndef TB_SHADER
_Static_assert(sizeof(OceanPushConstants) <= PUSH_CONSTANT_BYTES,
               "Too Many Push Constants");
#endif

#ifdef TB_SHADER

#define OCEAN_SET(b)                                                           \
  [[vk::binding(b, 0)]] ConstantBuffer<OceanData> ocean_data;                  \
  [[vk::binding(b, 1)]] Texture2D depth_map;                                   \
  [[vk::binding(b, 2)]] Texture2D color_map;                                   \
  [[vk::binding(b, 3)]] SamplerState material_sampler;                         \
  [[vk::binding(b, 4)]] SamplerComparisonState shadow_sampler;                 \
  [[vk::push_constant]]                                                        \
  ConstantBuffer<OceanPushConstants> consts

void gerstner_wave(TbOceanWave wave, float time, inout float3 pos,
                   inout float3 tangent, inout float3 binormal) {
  float steepness = wave.z;
  float k = 2 * TB_PI / wave.w;
  float c = sqrt(9.8 / k);
  float2 d = normalize(wave.xy);
  float f = k * (dot(d, pos.xz) - c * time);
  float a = steepness / k;

  float sinf = sin(f);
  float cosf = cos(f);

  tangent += float3(-d.x * d.y * (steepness * sinf), d.y * (steepness * cosf),
                    -d.y * d.y * (steepness * sinf));
  binormal += float3(-d.x * d.x * (steepness * sinf), d.x * (steepness * cosf),
                     -d.x * d.y * (steepness * sinf));
  pos += float3(d.x * (a * cosf), a * sinf, d.y * (a * cosf));
}

float3 calc_wave_pos(float3 pos, OceanData data, inout float3 tangent,
                     inout float3 binormal) {
  float time = data.time_waves.x;
  uint count = (uint)data.time_waves.y;
  if (count > TB_WAVE_MAX) {
    count = TB_WAVE_MAX;
  }
  for (uint i = 0; i < count; ++i) {
    gerstner_wave(data.wave[i], time, pos, tangent, binormal);
  }

  return pos;
}
#endif
