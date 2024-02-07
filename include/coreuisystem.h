#pragma once

#include "dynarray.h"
#include "imguisystem.h"
#include "tbcommon.h"
#include <flecs.h>

#define TB_COREUI_SYS_PRIO (TB_IMGUI_SYS_PRIO + 1)

typedef struct TbImGuiSystem TbImGuiSystem;
typedef struct TbRenderThread TbRenderThread;
typedef struct TbCoreUIMenu TbCoreUIMenu;
typedef struct TbWorld TbWorld;

typedef struct TbCoreUISystem {
  TbAllocator gp_alloc;
  TbAllocator tmp_alloc;

  TbImGuiSystem *imgui;

  TB_DYN_ARR_OF(TbCoreUIMenu) menu_registry;

  bool *metrics;
  bool *about;
} TbCoreUISystem;
extern ECS_COMPONENT_DECLARE(TbCoreUISystem);

bool *tb_coreui_register_menu(TbCoreUISystem *self, const char *name);
