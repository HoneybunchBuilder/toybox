#pragma once

#include "allocator.h"

#define ViewerSystemId 0xB001DEAD

typedef struct TbWorld TbWorld;

typedef struct ViewerSystem {
  Allocator tmp_alloc;
  Allocator std_alloc;

  bool *viewer_menu;
  bool load_scene_signal;
  bool unload_scene_signal;
  int32_t selected_scene_idx;
  const char *selected_scene;
} ViewerSystem;

void tb_register_viewer_sys(TbWorld *world);
void tb_unregister_viewer_sys(TbWorld *world);
