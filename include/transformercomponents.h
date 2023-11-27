#pragma once

#include "simd.h"

#define TbRotatorComponentIdStr "0xBABE0000"

typedef struct TbWorld TbWorld;

typedef struct TbRotatorComponent {
  float3 axis;
  float speed;
} TbRotatorComponent;

void tb_register_rotator_component(TbWorld *world);
