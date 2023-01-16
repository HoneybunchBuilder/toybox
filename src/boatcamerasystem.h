#pragma once

#include "allocator.h"

#define BoatCameraSystemId 0xDEADF000

typedef struct SystemDescriptor SystemDescriptor;

typedef struct BoatCameraSystemDescriptor {
  Allocator tmp_alloc;
} BoatCameraSystemDescriptor;

typedef struct BoatCameraSystem {
  Allocator tmp_alloc;
} BoatCameraSystem;

void tb_boat_camera_system_descriptor(
    SystemDescriptor *desc, const BoatCameraSystemDescriptor *cam_desc);
