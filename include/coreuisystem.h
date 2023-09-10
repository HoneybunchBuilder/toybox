#pragma once

#include "dynarray.h"
#include "tbcommon.h"
#include "world.h"

#define CoreUISystemId 0xDEADBAAD

typedef struct ImGuiSystem ImGuiSystem;
typedef struct RenderThread RenderThread;

typedef struct CoreUISystemDescriptor {
  Allocator std_alloc;
  Allocator tmp_alloc;
} CoreUISystemDescriptor;

typedef struct CoreUIMenu CoreUIMenu;

typedef struct CoreUISystem {
  Allocator std_alloc;
  Allocator tmp_alloc;

  ImGuiSystem *imgui;

  TB_DYN_ARR_OF(CoreUIMenu) menu_registry;

  bool *metrics;
  bool *about;
} CoreUISystem;

void tb_coreui_system_descriptor(SystemDescriptor *desc,
                                 const CoreUISystemDescriptor *coreui_desc);

bool *tb_coreui_register_menu(CoreUISystem *self, const char *name);

typedef struct ecs_world_t ecs_world_t;
void tb_register_core_ui_sys(ecs_world_t *ecs, Allocator std_alloc,
                             Allocator tmp_alloc);
void tb_unregister_core_ui_sys(ecs_world_t *ecs);
