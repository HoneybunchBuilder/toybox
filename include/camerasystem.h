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