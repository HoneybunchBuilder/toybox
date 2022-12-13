#pragma once

#ifndef __HLSL_VERSION
#include "simd.h" // If not a shader, we need simd types
#endif

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
typedef struct CommonViewData {
  float4x4 v;
  float4x4 vp;
  float4x4 inv_vp;
  float3 view_pos;
} CommonViewData;

// Constant per-view Light Data
typedef struct CommonLightData {
  float3 color;
  float3 light_dir;
  float4 cascade_splits;
  float4x4 light_vp;
  float4x4 cascade_vps[4];
} CommonLightData;

// Constant per-object Object Data for common objects
typedef struct CommonObjectData {
  float4x4 m;
  // Optional parameters for mesh shaders
  unsigned int index_bytes;
  unsigned int meshlet_offset;
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

// If a shader, provide some helper functions
#ifdef __HLSL_VERSION

float4 clip_to_screen(float4 clip)
{
  float4 o = clip * 0.5f;
  o.xy += o.w;
  o.zw = clip.zw;
  return o;
}

float linear_depth(float depth, float near, float far)
{
  return near * far / (far + depth * (near - far));
}

float depth_from_clip_z(float z, float near, float far)
{
  return max((1.0 - z / near) * far, 0);
}

#endif
