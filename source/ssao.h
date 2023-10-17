#pragma once

#include "simd.h"

// TODO: Define different kernel sizes for different performance scenarios?

#define SSAO_KERNEL_SIZE 32

typedef struct SSAOParams {
  int kernel_size;
  float3 kernel[SSAO_KERNEL_SIZE];
} SSAOParams;

typedef struct SSAOPushConstants {
  float2 noise_scale;
  float radius;
} SSAOPushConstants;

//_Static_assert(sizeof(SSAOPushConstants) <= 128, "Too Many Push Constants");
