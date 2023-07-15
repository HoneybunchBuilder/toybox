#pragma once

#include "dynarray.h"
#include "tbcommon.h"
#include "world.h"

#define CoreUISystemId 0xDEADBAAD

typedef struct RenderThread RenderThread;

typedef struct CoreUISystemDescriptor {
  Allocator std_alloc;
  Allocator tmp_alloc;
} CoreUISystemDescriptor;

typedef struct CoreUIMenu CoreUIMenu;

typedef struct CoreUISystem {
  Allocator std_alloc;
  Allocator tmp_alloc;
  TB_DYN_ARR_OF(CoreUIMenu) menu_registry;

  bool *metrics;
  bool *about;
} CoreUISystem;

void tb_coreui_system_descriptor(SystemDescriptor *desc,
                                 const CoreUISystemDescriptor *coreui_desc);

bool *tb_coreui_register_menu(CoreUISystem *self, const char *name);
