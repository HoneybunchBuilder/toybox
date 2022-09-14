#pragma once

#include "pi.h"

// Push Constants Data for a fullscreen pass
typedef struct FullscreenPushConstants {
  float4 time;
  float2 resolution;
} FullscreenPushConstants;

typedef struct SkyPushConstants {
  float4x4 vp;
} SkyPushConstants;

typedef struct EnvFilterConstants{
  float roughness;
  uint32_t sample_count;
}EnvFilterConstants;

// Constant per-view Camera Data
typedef struct CommonCameraData {
  float4x4 vp;
  float4x4 inv_vp;
  float3 view_pos;
} CommonCameraData;

// Constant per-view Light Data
typedef struct CommonLightData {
  float3 light_dir;
  float4x4 light_vp;
} CommonLightData;

// Constant per-object Object Data for common objects
typedef struct CommonObjectData {
  float4x4 mvp;
  float4x4 m;
} CommonObjectData;

// Common input layout info and permutation settings
#define VA_INPUT_PERM_NONE 0x00000000
#define VA_INPUT_PERM_POSITION 0x00000001
#define VA_INPUT_PERM_NORMAL 0x00000002
#define VA_INPUT_PERM_TEXCOORD0 0x00000004
#define VA_INPUT_PERM_TEXCOORD1 0x00000008
#define VA_INPUT_PERM_TANGENT 0x00000010
#define VA_INPUT_PERM_COLOR 0x00000020
#define VA_INPUT_PERM_COUNT 6
