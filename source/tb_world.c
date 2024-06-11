#include "tb_world.h"

#include "tb_allocator.h"
#include "tb_assets.h"
#include "tb_common.h"
#include "tb_gltf.h"
#include "tb_input_system.h"
#include "tb_material_system.h"
#include "tb_mesh_system2.h"
#include "tb_profiling.h"
#include "tb_scene.h"
#include "tb_simd.h"
#include "tb_task_scheduler.h"
#include "tb_texture_system.h"
#include "tb_transform_component.h"

#include <flecs.h>
#include <json.h>
#include <mimalloc.h>
#include <stdio.h>

typedef struct TbSystemEntry {
  char *name;
  int32_t priority;
  TbCreateSystemFn create_fn;
  TbDestroySystemFn destroy_fn;
} TbSystemEntry;

typedef struct TbSystemRegistry {
  int32_t count;
  TbSystemEntry *entries;
} TbSystemRegistry;

typedef struct TbComponentEntry {
  char *name;
  TbRegisterComponentFn reg_fn;
  TbLoadComponentFn load_fn;
  ecs_entity_t desc_id;
} TbComponentEntry;

typedef struct TbComponentRegistry {
  int32_t count;
  TbComponentEntry *entries;
} TbComponentRegistry;

ECS_COMPONENT_DECLARE(TbWorldRef);

void *ecs_malloc(ecs_size_t size) {
  TracyCZone(ctx, true);
  TracyCZoneColor(ctx, TracyCategoryColorMemory);
  void *ptr = mi_malloc(size);
  TracyCAllocN(ptr, size, "ECS");
  TracyCZoneEnd(ctx);
  return ptr;
}

void ecs_free(void *ptr) {
  TracyCZone(ctx, true);
  TracyCZoneColor(ctx, TracyCategoryColorMemory);
  TracyCFreeN(ptr, "ECS");
  mi_free(ptr);
  TracyCZoneEnd(ctx);
}

void *ecs_calloc(ecs_size_t size) {
  TracyCZone(ctx, true);
  TracyCZoneColor(ctx, TracyCategoryColorMemory);
  void *ptr = mi_calloc(1, size);
  TracyCAllocN(ptr, size, "ECS");
  TracyCZoneEnd(ctx);
  return ptr;
}

void *ecs_realloc(void *original, ecs_size_t size) {
  TracyCZone(ctx, true);
  TracyCZoneColor(ctx, TracyCategoryColorMemory);
  TracyCFreeN(original, "ECS");
  void *ptr = mi_realloc(original, size);
  TracyCAllocN(ptr, size, "ECS");
  TracyCZoneEnd(ctx);
  return ptr;
}

ECS_COMPONENT_DECLARE(float3);
ECS_COMPONENT_DECLARE(float4);
ECS_COMPONENT_DECLARE(float4x4);
ECS_COMPONENT_DECLARE(TbTransform);

static TbSystemRegistry s_sys_reg = {0};

int32_t tb_sys_cmp(const void *a, const void *b) {
  tb_auto sys_a = (const TbSystemEntry *)a;
  tb_auto sys_b = (const TbSystemEntry *)b;
  return sys_a->priority - sys_b->priority;
}

void tb_register_system(const char *name, int32_t priority,
                        TbCreateSystemFn create_fn,
                        TbDestroySystemFn destroy_fn) {
  TracyCZoneN(ctx, "Register System", true);
  int32_t index = s_sys_reg.count;
  int32_t next_count = ++s_sys_reg.count;
  size_t entry_size = next_count * sizeof(TbSystemEntry);
  size_t name_len = SDL_strlen(name) + 1;

  s_sys_reg.entries = mi_realloc(s_sys_reg.entries, entry_size);
  tb_auto entry = &s_sys_reg.entries[index];
  entry->priority = priority;
  entry->create_fn = create_fn;
  entry->destroy_fn = destroy_fn;
  entry->name = mi_malloc(name_len);
  SDL_memset(entry->name, 0, name_len);
  SDL_strlcpy(entry->name, name, name_len);
  TracyCZoneEnd(ctx);
}

static TbComponentRegistry s_comp_reg = {0};

void tb_register_component(const char *name, TbRegisterComponentFn reg_fn,
                           TbLoadComponentFn load_fn) {
  TracyCZoneN(ctx, "Register Component", true);
  int32_t index = s_comp_reg.count;
  int32_t next_count = ++s_comp_reg.count;
  size_t entry_size = next_count * sizeof(TbComponentEntry);
  size_t name_len = SDL_strlen(name) + 1;

  s_comp_reg.entries = mi_realloc(s_comp_reg.entries, entry_size);
  tb_auto entry = &s_comp_reg.entries[index];
  entry->reg_fn = reg_fn;
  entry->load_fn = load_fn;
  entry->name = mi_malloc(name_len);
  SDL_memset(entry->name, 0, name_len);
  SDL_strlcpy(entry->name, name, name_len);
  TracyCZoneEnd(ctx);
}

#ifndef FINAL
int32_t tb_check_info_mode(int32_t argc, char *const *argv) {
  static const char *info_mode_str = "--info";
  for (int32_t i = 0; i < argc; ++i) {
    const char *argument = argv[i];
    if (SDL_strncmp(argument, info_mode_str, SDL_strlen(info_mode_str)) == 0) {
      return 1;
    }
  }
  return 0;
}

void tb_write_info(TbWorld *world) {
  json_tokener *tok = json_tokener_new();

  ecs_world_t *ecs = world->ecs;

  // Reflect all components
  tb_auto reflection = json_object_new_object();

  for (int32_t i = 0; i < s_comp_reg.count; ++i) {
    const char *name = s_comp_reg.entries[i].name;
    tb_auto desc_id = s_comp_reg.entries[i].desc_id;
    if (desc_id) {
      char *info = ecs_type_info_to_json(ecs, desc_id);
      tb_auto parsed = json_tokener_parse_ex(tok, info, SDL_strlen(info) + 1);
      if (parsed) {
        json_object_object_add(reflection, name, parsed);
      }
      ecs_os_free(info);
    }
  }

  const char *refl_json = json_object_to_json_string(reflection);
  printf("%s\n", refl_json);
  json_object_put(reflection);

  json_tokener_free(tok);
}
#endif

bool tb_create_world(const TbWorldDesc *desc, TbWorld *world) {
  TracyCZoneN(ctx, "Create World", true);
  TbAllocator gp_alloc = desc->gp_alloc;

  tb_auto ecs = ecs_init();

  // Ensure the instrumented allocator is used
  ecs_os_set_api_defaults();
  ecs_os_api_t os_api = ecs_os_api;
  os_api.malloc_ = ecs_malloc;
  os_api.free_ = ecs_free;
  os_api.calloc_ = ecs_calloc;
  os_api.realloc_ = ecs_realloc;
  ecs_os_set_api(&os_api);

  *world = (TbWorld){
      .ecs = ecs,
      .window = desc->window,
      .gp_alloc = gp_alloc,
      .tmp_alloc = desc->tmp_alloc,
  };

  {
    ECS_COMPONENT_DEFINE(ecs, TbWorldRef);
    TbWorldRef ref = {world};
    ecs_singleton_set_ptr(ecs, TbWorldRef, &ref);
  }

  // Register all components first so info mode can function
  for (int32_t i = 0; i < s_comp_reg.count; ++i) {
    tb_auto fn = s_comp_reg.entries[i].reg_fn;
    if (fn) {
      s_comp_reg.entries[i].desc_id = fn(world);
    }
  }

// Run optional info mode in non-final builds only
#ifndef FINAL
  if (tb_check_info_mode(desc->argc, desc->argv) > 0) {
    tb_write_info(world);
    TracyCZoneEnd(ctx);
    return false; // Do not continue
  }
#endif

  // Must create render thread on the heap like this
  TbRenderThread *render_thread = tb_alloc_tp(gp_alloc, TbRenderThread);
  TbRenderThreadDescriptor render_thread_desc = {.window = desc->window};
  TB_CHECK(tb_start_render_thread(&render_thread_desc, render_thread),
           "Failed to start render thread");

  // Do not go initializing anything until we know the render thread is ready
  tb_wait_thread_initialized(render_thread);

  world->render_thread = render_thread;

  // Create all registered systems after sorting by priority
  {
    TracyCZoneNC(ctx, "Create Systems", TracyCategoryColorCore, true);
    const int32_t count = s_sys_reg.count;
    SDL_qsort(s_sys_reg.entries, count, sizeof(TbSystemEntry), tb_sys_cmp);

    for (int32_t i = 0; i < s_sys_reg.count; ++i) {
      tb_auto fn = s_sys_reg.entries[i].create_fn;
      if (fn) {
        fn(world);
      }
    }
    TracyCZoneEnd(ctx);
  }

#ifndef FINAL
  // By setting this singleton we allow the application to connect to the
  // flecs explorer
  ecs_singleton_set(ecs, EcsRest, {0});
  ECS_IMPORT(ecs, FlecsMonitor);
#endif

  TracyCZoneEnd(ctx);
  return true;
}

bool tb_tick_world(TbWorld *world, float delta_seconds) {
  TracyCZoneNC(ctx, "World Tick", TracyCategoryColorCore, true);
  ecs_world_t *ecs = world->ecs;

  world->time += (double)delta_seconds;

  // Run pinned tasks first
  tb_run_pinned_tasks(ecs);

  // Tick with flecs
  if (!ecs_progress(ecs, delta_seconds)) {
    return false;
  }
  // Manually check flecs for quit event

  tb_auto in_sys = ecs_singleton_get(ecs, TbInputSystem);
  if (in_sys) {
    for (uint32_t event_idx = 0; event_idx < in_sys->event_count; ++event_idx) {
      if (in_sys->events[event_idx].type == SDL_EVENT_QUIT) {
        TracyCZoneEnd(ctx);
        return false;
      }
    }
  }
  TracyCZoneEnd(ctx);
  return true;
}

void tb_destroy_world(TbWorld *world) {
  // Clean up singletons
  ecs_world_t *ecs = world->ecs;

  // Stop the render thread before we start destroying render objects
  tb_stop_render_thread(world->render_thread);

  // Destroy systems in reverse order
  for (int32_t i = s_sys_reg.count - 1; i >= 0; --i) {
    tb_auto fn = s_sys_reg.entries[i].destroy_fn;
    if (fn) {
      fn(world);
    }
  }

  // Destroying the render thread will also close the window
  tb_destroy_render_thread(world->render_thread);
  tb_free(world->gp_alloc, world->render_thread);

  ecs_fini(ecs);
}

TbScene tb_load_scene(TbWorld *world, const char *scene_path) {
  TB_TRACY_SCOPE("Load Scene");
  // Get qualified path to scene asset
  char *asset_path = tb_resolve_asset_path(world->tmp_alloc, scene_path);
  // Create an entity that represents the scene and trigger an async load
  return tb_create_scene(world->ecs, asset_path);
}

TbLoadComponentFn tb_get_component_load_fn(const char *name) {
  for (int32_t i = 0; i < s_comp_reg.count; ++i) {
    const char *comp_name = s_comp_reg.entries[i].name;
    tb_auto load_fn = s_comp_reg.entries[i].load_fn;
    if (SDL_strcmp(comp_name, name) == 0) {
      return load_fn;
    }
  }
  return NULL;
}
