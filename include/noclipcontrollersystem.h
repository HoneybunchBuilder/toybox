#pragma once

#include "allocator.h"

#define NoClipControllerSystemId 0xFEFEFEFE

typedef struct SystemDescriptor SystemDescriptor;

typedef struct InputSystem InputSystem;

typedef struct NoClipControllerSystemDescriptor {
  Allocator tmp_alloc;
} NoClipControllerSystemDescriptor;

typedef struct NoClipControllerSystem {
  Allocator tmp_alloc;
  InputSystem *input;
} NoClipControllerSystem;

void tb_noclip_controller_system_descriptor(
    SystemDescriptor *desc,
    const NoClipControllerSystemDescriptor *noclip_desc);
