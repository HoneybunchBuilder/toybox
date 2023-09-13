#pragma once

#include "allocator.h"

#define CameraSystemId 0xFFBADD11

typedef struct SystemDescriptor SystemDescriptor;
typedef struct ViewSystem ViewSystem;

typedef struct CameraSystemDescriptor {
  Allocator tmp_alloc;
  Allocator std_alloc;
} CameraSystemDescriptor;

typedef struct CameraSystem {
  ViewSystem *view_system;
  Allocator tmp_alloc;
  Allocator std_alloc;
} CameraSystem;

void tb_camera_system_descriptor(SystemDescriptor *desc,
                                 const CameraSystemDescriptor *camera_desc);

typedef struct ecs_world_t ecs_world_t;
void tb_register_camera_sys(ecs_world_t *ecs, Allocator std_alloc,
                            Allocator tmp_alloc);
void tb_unregister_camera_sys(ecs_world_t *ecs);
