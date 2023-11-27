#pragma once

#include "simd.h"

typedef struct TB_GPU_STRUCT ShadowViewConstants {
  float4x4 vp;
} ShadowViewConstants;

typedef struct TB_GPU_STRUCT ShadowDrawConstants {
  float4x4 m;
} ShadowDrawConstants;

typedef struct TB_GPU_STRUCT TbShadowConstants {
  float4x4 vp;
  float4x4 m;
} TbShadowConstants;
