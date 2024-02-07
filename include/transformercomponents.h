#pragma once

#include "simd.h"

#include <flecs.h>

#define TbRotatorComponentIdStr "0xBABE0000"

typedef struct TbWorld TbWorld;

typedef struct TbRotatorComponent {
  float3 axis;
  float speed;
} TbRotatorComponent;
extern ECS_COMPONENT_DECLARE(TbRotatorComponent);

void tb_register_rotator_component(TbWorld *world);
