#pragma once

#include "allocator.h"

typedef struct SystemDescriptor SystemDescriptor;

#define RotatorSystemId 0x0000BABE

typedef struct RotatorSystemDescriptor {
  Allocator tmp_alloc;
} RotatorSystemDescriptor;

typedef struct RotatorSystem {
  Allocator tmp_alloc;
} RotatorSystem;

void tb_rotator_system_descriptor(SystemDescriptor *desc,
                                  const RotatorSystemDescriptor *rot_desc);

typedef struct ecs_world_t ecs_world_t;
void tb_register_rotator_sys(ecs_world_t *ecs, Allocator tmp_alloc);
void tb_unregister_rotator_sys(ecs_world_t *ecs);
