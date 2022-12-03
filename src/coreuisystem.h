#pragma once

#include "tbcommon.h"
#include "world.h"

#define CoreUISystemId 0xDEADBAAD

typedef struct RenderThread RenderThread;

typedef struct CoreUISystemDescriptor {
  Allocator tmp_alloc;
} CoreUISystemDescriptor;

typedef struct CoreUISystem {
  Allocator tmp_alloc;
} CoreUISystem;

void tb_coreui_system_descriptor(SystemDescriptor *desc,
                                 const CoreUISystemDescriptor *coreui_desc);
