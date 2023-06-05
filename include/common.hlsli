#pragma once

#ifndef TB_COMMON_H
#define TB_COMMON_H

#include "pi.h"
#include "simd.h"

#define PUSH_CONSTANT_BYTES 128

typedef struct BlurPushConstants {
  float horizontal;
} BlurPushConstants;

typedef struct SkyPushConstants {
  float4x4 vp;
} SkyPushConstants;

typedef struct EnvFilterConstants {
  float roughness;
  uint32_t sample_count;
} EnvFilterConstants;

typedef struct PrimitivePushConstants {
  float3 position;
  float3 scale;
  float4 color;
} PrimitivePushConstants;

// Constant per-view Camera Data
typedef struct CommonViewData {
  float4x4 v;
  float4x4 p;
  float4x4 vp;
  float4x4 inv_vp;
  float3 view_pos;
  float4 proj_params;
} CommonViewData;

// Constant per-view Light Data
#define CASCADE_COUNT 4
typedef struct CommonLightData {
  float3 color;
  float3 light_dir;
  float4 cascade_splits;
  float4x4 cascade_vps[CASCADE_COUNT];
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

float4 clip_to_screen(float4 clip) {
  float4 o = clip * 0.5f;
  o.xy += o.w;
  o.zw = clip.zw;
  return o;
}

float linear_depth(float depth, float near, float far) {
  return (far * near / ((near - far) * depth + far));
}

float depth_from_clip_z(float z, float near, float far) {
  return max((1 - z / near) * far, 0);
}

float2 tiling_and_offset(float2 uv, float2 tiling, float2 offset) {
  return uv * tiling + offset;
}

float inv_lerp(float from, float to, float value) {
  return (value - from) / (to - from);
}

float remap(float x_from, float x_to, float y_from, float y_to, float value) {
  float rel = inv_lerp(x_from, x_to, value);
  return lerp(y_from, y_to, rel);
}

float2 gradient_noise_dir(float2 uv) {
  uv = uv % 289;
  float x = (34 * uv.x + 1) * uv.x % 289 + uv.y;
  x = (34 * x + 1) * x % 289;
  x = frac(x / 41) * 2 - 1;
  return normalize(float2(x - floor(x + 0.5), abs(x) - 0.5));
}

float gradient_noise(float2 uv) {
  float2 ip = floor(uv);
  float2 fp = frac(uv);
  float d00 = dot(gradient_noise_dir(ip), fp);
  float d01 = dot(gradient_noise_dir(ip + float2(0, 1)), fp - float2(0, 1));
  float d10 = dot(gradient_noise_dir(ip + float2(1, 0)), fp - float2(1, 0));
  float d11 = dot(gradient_noise_dir(ip + float2(1, 1)), fp - float2(1, 1));
  fp = fp * fp * fp * (fp * (fp * 6 - 15) + 10);
  return lerp(lerp(d00, d01, fp.y), lerp(d10, d11, fp.y), fp.x) + 0.5f;
}

float3 view_space_pos_from_depth(float depth, float4x4 inv_proj, float2 uv){
  float x = uv.x * 2.0 - 1.0;
  float y = uv.y * 2.0 - 1.0;
  float z = depth;
  float4 view_space_pos = mul(float4(x, y, z, 1.0f), inv_proj);
  return view_space_pos.xyz / view_space_pos.w;
}

#endif

#endif
