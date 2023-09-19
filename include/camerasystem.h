#pragma once

#include "allocator.h"

#define CameraSystemId 0xFFBADD11

typedef struct ecs_world_t ecs_world_t;

typedef struct CameraSystem {
  Allocator std_alloc;
  Allocator tmp_alloc;
} CameraSystem;

void tb_register_camera_sys(ecs_world_t *ecs, Allocator std_alloc,
                            Allocator tmp_alloc);
void tb_unregister_camera_sys(ecs_world_t *ecs);
