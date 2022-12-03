#pragma once

typedef struct ShadowViewConstants {
  float4x4 vp;
} ShadowViewConstants;

typedef struct ShadowDrawConstants {
  float4x4 m;
} ShadowDrawConstants;

typedef struct ShadowConstants {
  float4x4 vp;
  float4x4 m;
} ShadowConstants;
