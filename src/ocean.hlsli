#pragma once

#define TB_WAVE_MAX 4

typedef struct OceanWave {
  float steepness;
  float wavelength;
  float2 direction;
} OceanWave;

typedef struct OceanData {
  int32_t wave_count;
  OceanWave wave[TB_WAVE_MAX];
  float4x4 m;
} OceanData;

typedef struct OceanPushConstants {
  float time;
} OceanPushConstants;