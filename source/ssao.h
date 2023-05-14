#pragma once

#include "simd.h"

// TODO: Define different kernel sizes for different performance scenarios?

#define SSAO_KERNEL_SIZE 64

typedef struct SSAOParams {
  int kernel_size;
  float3 kernel[SSAO_KERNEL_SIZE];
} SSAOParams;
