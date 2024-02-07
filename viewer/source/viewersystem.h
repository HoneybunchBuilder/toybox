#pragma once

#include <flecs.h>

typedef struct TbViewerSystem {
  bool *viewer_menu;
  bool load_scene_signal;
  bool unload_scene_signal;
  int32_t selected_scene_idx;
  const char *selected_scene;
} TbViewerSystem;
extern ECS_COMPONENT_DECLARE(TbViewerSystem);
