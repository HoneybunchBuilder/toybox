#pragma once

#include "dynarray.h"
#include "tbcommon.h"
#include <flecs.h>

typedef struct TbImGuiSystem TbImGuiSystem;
typedef struct TbRenderThread TbRenderThread;
typedef struct TbCoreUIMenu TbCoreUIMenu;
typedef struct TbWorld TbWorld;

typedef struct TbCoreUISystem {
  TbAllocator std_alloc;
  TbAllocator tmp_alloc;

  TbImGuiSystem *imgui;

  TB_DYN_ARR_OF(TbCoreUIMenu) menu_registry;

  bool *metrics;
  bool *about;
} TbCoreUISystem;
extern ECS_COMPONENT_DECLARE(TbCoreUISystem);

void tb_register_core_ui_sys(TbWorld *world);
void tb_unregister_core_ui_sys(TbWorld *world);

bool *tb_coreui_register_menu(TbCoreUISystem *self, const char *name);
