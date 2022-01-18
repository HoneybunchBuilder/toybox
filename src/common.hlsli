#pragma once

// Push Constants Data for a fullscreen pass
typedef struct FullscreenPushConstants {
  float4 time;
  float2 resolution;
} FullscreenPushConstants;

typedef struct SkyPushConstants {
  float4x4 vp;
} SkyPushConstants;

// Constant per-view Camera Data
typedef struct CommonCameraData {
  float4x4 vp;
  float4x4 inv_vp;
  float3 view_pos;
} CommonCameraData;

// Constant per-view Light Data
typedef struct CommonLightData {
  float3 light_dir;
} CommonLightData;

// Constant per-object Object Data for common objects
typedef struct CommonObjectData {
  float4x4 mvp;
  float4x4 m;
} CommonObjectData;

#define PI 3.141592653589793
#define TAU 6.283185307179586
