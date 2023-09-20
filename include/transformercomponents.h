#pragma once

#include "simd.h"

#define RotatorComponentId 0xBABE0000
#define RotatorComponentIdStr "0xBABE0000"

typedef struct ecs_world_t ecs_world_t;

typedef struct RotatorComponent {
  float3 axis;
  float speed;
} RotatorComponent;

void tb_register_rotator_component(ecs_world_t *ecs);
