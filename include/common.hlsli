#pragma once

#ifndef TB_COMMON_H
#define TB_COMMON_H

#ifdef __cplusplus
#define _Static_assert static_assert
#endif

#include "pi.h"
#include "simd.h"

#define PUSH_CONSTANT_BYTES 128

typedef struct TB_GPU_STRUCT TbSkyPushConstants {
  float4x4 vp;
}
TbSkyPushConstants;

typedef struct TB_GPU_STRUCT TbEnvFilterConstants {
  float roughness;
  uint32_t sample_count;
}
TbEnvFilterConstants;

typedef struct TB_GPU_STRUCT TbPrimitivePushConstants {
  float3 position;
  float3 scale;
  float4 color;
}
TbPrimitivePushConstants;

// Constant per-view Camera Data
typedef struct TB_GPU_STRUCT TbCommonViewData {
  float4x4 v;
  float4x4 p;
  float4x4 vp;
  float4x4 inv_vp;
  float4x4 inv_proj;
  float3 view_pos;
  float4 proj_params;
}
TbCommonViewData;

// Constant per-view Light Data
#define TB_CASCADE_COUNT 4
typedef struct TB_GPU_STRUCT TbCommonLightData {
  float3 color;
  float3 light_dir;
  float4 cascade_splits;
  float4x4 cascade_vps[TB_CASCADE_COUNT];
}
TbCommonLightData;

// Per-instance object data
typedef struct TB_GPU_STRUCT TbCommonObjectData {
  float4x4 m;
}
TbCommonObjectData;

// Macros for declaring access to common toybox descriptor sets
// that represent global loaded resource tables
#define TB_TEXTURE_SET(space) Texture2D gltf_textures[] : register(t0, space);
#define TB_OBJECT_SET(space)                                                   \
  StructuredBuffer<TbCommonObjectData> object_data[] : register(t0, space);
#define TB_IDX_SET(space) RWBuffer<int32_t> idx_buffers[] : register(u0, space);
#define TB_POS_SET(space) RWBuffer<int4> pos_buffers[] : register(u0, space);
#define TB_NORM_SET(space)                                                     \
  RWBuffer<float4> norm_buffers[] : register(u0, space);
#define TB_TAN_SET(space) RWBuffer<float4> tan_buffers[] : register(u0, space);
#define TB_UV0_SET(space) RWBuffer<int2> uv0_buffers[] : register(u0, space);
#define TB_JOINT_SET(space)                                                    \
  RWBuffer<int4> joint_buffers[] : register(u0, space);
#define TB_WEIGHT_SET(space)                                                   \
  RWBuffer<int4> weight_buffers[] : register(u0, space);

// Common input layout info and permutation settings
#define TB_INPUT_PERM_NONE 0x00000000
#define TB_INPUT_PERM_POSITION 0x00000001
#define TB_INPUT_PERM_NORMAL 0x00000002
#define TB_INPUT_PERM_TEXCOORD0 0x00000004
#define TB_INPUT_PERM_TANGENT 0x00000008
#define TB_INPUT_PERM_JOINT 0x00000010
#define TB_INPUT_PERM_WEIGHT 0x00000020
#define TB_INPUT_PERM_COUNT 6

// If a shader, provide some helper functions
#ifdef __HLSL_VERSION

TbCommonObjectData
tb_get_obj_data(int32_t obj, StructuredBuffer<TbCommonObjectData> buffers[]) {
  return buffers[NonUniformResourceIndex(obj)][0];
}

Texture2D tb_get_texture(int32_t tex, Texture2D textures[]) {
  return textures[NonUniformResourceIndex(tex)];
}

int32_t tb_get_idx(int32_t vertex, int32_t mesh, RWBuffer<int32_t> buffers[]) {
  return buffers[NonUniformResourceIndex(mesh)][vertex];
}

int3 tb_vert_get_local_pos(int32_t perm, int32_t index, int32_t mesh,
                           RWBuffer<int4> buffers[]) {
  if ((perm & TB_INPUT_PERM_POSITION) > 0) {
    return buffers[NonUniformResourceIndex(mesh)][index].xyz;
  }
  return 0;
}

float3 tb_vert_get_normal(int32_t perm, int32_t index, int32_t mesh,
                          RWBuffer<float4> buffers[]) {
  if ((perm & TB_INPUT_PERM_NORMAL) > 0) {
    return buffers[NonUniformResourceIndex(mesh)][index].xyz;
  }
  return 0;
}

float4 tb_vert_get_tangent(int32_t perm, int32_t index, int32_t mesh,
                           RWBuffer<float4> buffers[]) {
  if ((perm & TB_INPUT_PERM_TANGENT) > 0) {
    return buffers[NonUniformResourceIndex(mesh)][index].xyzw;
  }
  return 0;
}

int2 tb_vert_get_uv0(int32_t perm, int32_t index, int32_t mesh,
                     RWBuffer<int2> buffers[]) {
  if ((perm & TB_INPUT_PERM_TEXCOORD0) > 0) {
    return buffers[NonUniformResourceIndex(mesh)][index];
  }
  return 0;
}

float4 aa_to_quat(float4 aa) {
  float s = sin(aa.w * 0.5f);
  return normalize(float4(aa.x * s, aa.y * s, aa.z * s, cos(aa.w * 0.5f)));
}

float3 qrot(float4 q, float3 v) {
  float3 u = q.xyz;
  float3 uv = cross(u, v);
  float3 uuv = cross(u, uv);

  return v + ((uv * q.w) + uuv) * 2.0f;
}

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

float3 view_space_pos_from_depth(float depth, float4x4 inv_proj, float2 uv) {
  float x = uv.x * 2.0 - 1.0;
  float y = uv.y * 2.0 - 1.0;
  float4 pos = float4(x, y, depth, 1.0f);
  float4 view_space_pos = mul(inv_proj, pos);
  return view_space_pos.xyz / view_space_pos.w;
}

#endif

#endif
