#pragma once

#include "allocator.h"

#define ViewerSystemId 0xB001DEAD

typedef struct SystemDescriptor SystemDescriptor;

typedef struct ViewerSystemDescriptor {
  Allocator tmp_alloc;
  Allocator std_alloc;
} ViewerSystemDescriptor;

typedef struct ViewerSystem {
  Allocator tmp_alloc;
  Allocator std_alloc;

  bool *viewer_menu;
  bool load_scene_signal;
  bool unload_scene_signal;
  int32_t selected_scene_idx;
  const char *selected_scene;
} ViewerSystem;

void tb_viewer_system_descriptor(SystemDescriptor *desc,
                                 const ViewerSystemDescriptor *ocean_desc);
