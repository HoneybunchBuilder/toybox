#pragma once

#include "allocator.h"

#define CameraSystemId 0xFFBADD11

typedef struct ecs_world_t ecs_world_t;

typedef struct CameraSystem {
  TbAllocator std_alloc;
  TbAllocator tmp_alloc;
} CameraSystem;

void tb_register_camera_sys(ecs_world_t *ecs, TbAllocator std_alloc,
                            TbAllocator tmp_alloc);
void tb_unregister_camera_sys(ecs_world_t *ecs);
