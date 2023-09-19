#pragma once

#include "allocator.h"

#define RotatorSystemId 0x0000BABE

typedef struct ecs_world_t ecs_world_t;

typedef struct RotatorSystem {
  Allocator tmp_alloc;
} RotatorSystem;

void tb_register_rotator_sys(ecs_world_t *ecs, Allocator tmp_alloc);
void tb_unregister_rotator_sys(ecs_world_t *ecs);
