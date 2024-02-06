#pragma once

#include "allocator.h"

typedef struct TbWorld TbWorld;

typedef struct TbViewerSystem {
  TbAllocator tmp_alloc;
  TbAllocator gp_alloc;

  bool *viewer_menu;
  bool load_scene_signal;
  bool unload_scene_signal;
  int32_t selected_scene_idx;
  const char *selected_scene;
} TbViewerSystem;

void tb_register_viewer_sys(TbWorld *world);
void tb_unregister_viewer_sys(TbWorld *world);
