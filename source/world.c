#include "world.h"

#include "allocator.h"
#include "assets.h"

#include "profiling.h"
#include "simd.h"
#include "tbcommon.h"
#include "tbgltf.h"

#include "transformcomponent.h"

#include "inputsystem.h"

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
}

static TbComponentRegistry s_comp_reg = {0};

void tb_register_component(const char *name, TbRegisterComponentFn reg_fn,
                           TbLoadComponentFn load_fn) {
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
  TB_DYN_ARR_RESET(world->scenes, gp_alloc, 1);

  // Create all registered systems after sorting by priority
  {
    const int32_t count = s_sys_reg.count;
    SDL_qsort(s_sys_reg.entries, count, sizeof(TbSystemEntry), tb_sys_cmp);

    for (int32_t i = 0; i < s_sys_reg.count; ++i) {
      tb_auto fn = s_sys_reg.entries[i].create_fn;
      if (fn) {
        fn(world);
      }
    }
  }

#ifndef FINAL
  // By setting this singleton we allow the application to connect to the
  // flecs explorer
  ecs_singleton_set(ecs, EcsRest, {0});
  ECS_IMPORT(ecs, FlecsMonitor);
#endif

  return true;
}

bool tb_tick_world(TbWorld *world, float delta_seconds) {
  TracyCZoneNC(ctx, "World Tick", TracyCategoryColorCore, true);
  ecs_world_t *ecs = world->ecs;

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

void tb_clear_world(TbWorld *world) {
  TB_DYN_ARR_FOREACH(world->scenes, i) {
    TbScene *scene = &TB_DYN_ARR_AT(world->scenes, i);
    tb_unload_scene(world, scene);
  }
  TB_DYN_ARR_CLEAR(world->scenes);
}

void tb_destroy_world(TbWorld *world) {
  // Clean up singletons
  ecs_world_t *ecs = world->ecs;

  tb_clear_world(world);

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

void load_entity(TbWorld *world, TbScene *scene, json_tokener *tok,
                 const cgltf_data *data, const char *root_scene_path,
                 ecs_entity_t parent, const cgltf_node *node) {
  ecs_world_t *ecs = world->ecs;
  ECS_TAG(ecs, EcsDisabled);
  ECS_TAG(ecs, EcsPrefab);

  TbAllocator tmp_alloc = world->tmp_alloc;

  // Get extras
  json_object *json = NULL;
  {
    cgltf_size extra_size = 0;
    char *extra_json = NULL;
    if (node->extras.end_offset != 0 && node->extras.start_offset != 0) {
      extra_size = (node->extras.end_offset - node->extras.start_offset) + 1;
      extra_json = tb_alloc_nm_tp(tmp_alloc, extra_size, char);
      if (cgltf_copy_extras_json(data, &node->extras, extra_json,
                                 &extra_size) != cgltf_result_success) {
        extra_size = 0;
        extra_json = NULL;
      }
    }

    if (extra_json) {
      json = json_tokener_parse_ex(tok, extra_json, (int32_t)extra_size);
    }
  }

  // See if the entity is enabled or a prefab
  bool enabled = true;
  bool prefab = false;
  if (json) {
    tb_auto enabled_obj = json_object_object_get(json, "enabled");
    if (enabled_obj) {
      enabled = (bool)json_object_get_boolean(enabled_obj);
    }
    tb_auto prefab_obj = json_object_object_get(json, "prefab");
    if (prefab_obj) {
      prefab = (bool)json_object_get_boolean(prefab_obj);
    }
  }
  // We inherit some properties from a parent
  if (parent != TbInvalidEntityId) {
    if (ecs_has(ecs, parent, EcsDisabled)) {
      enabled = false;
    }
    if (ecs_has(ecs, parent, EcsPrefab)) {
      prefab = true;
    }
  }

  const char *name = node->name;
  if (name && ecs_lookup(ecs, node->name) != TbInvalidEntityId) {
    name = NULL; // Name already exists
  }

  // Create an entity
  ecs_entity_t ent = 0;
  if (prefab) {
    ent = ecs_new_prefab(ecs, 0);
  } else {
    ent = ecs_new_entity(ecs, 0);
  }

  if (name) {
    ecs_set_name(ecs, ent, name);
  }

  // Express heirarchy via flecs
  if (parent != TbInvalidEntityId) {
    ecs_add_pair(ecs, ent, EcsChildOf, parent);
  }

  // See if there are core components that need construction first
  for (int32_t i = 0; i < s_comp_reg.count; ++i) {
    const char *name = s_comp_reg.entries[i].name;
    tb_auto load_fn = s_comp_reg.entries[i].load_fn;
    if (load_fn) {
      if (SDL_strcmp(name, "transform") == 0) {
        load_fn(world, ent, root_scene_path, node, NULL);
      } else if (SDL_strcmp(name, "mesh") == 0 && node->mesh) {
        load_fn(world, ent, root_scene_path, node, NULL);
      } else if (SDL_strcmp(name, "camera") == 0 && node->camera) {
        load_fn(world, ent, root_scene_path, node, NULL);
      } else if (SDL_strcmp(name, "light") == 0 && node->light) {
        load_fn(world, ent, root_scene_path, node, NULL);
      }
    }
  }

  if (node->children_count > 0) {
    tb_auto trans_comp = ecs_get_mut(ecs, ent, TbTransformComponent);
    // Make sure this entity actually has a transform
    if (trans_comp) {
      // Load all children
      for (uint32_t i = 0; i < node->children_count; ++i) {
        tb_auto child = node->children[i];
        load_entity(world, scene, tok, data, root_scene_path, ent, child);
      }
    }
  }

  // Add custom components
  if (json) {
    json_object_object_foreach(json, component_name, component_obj) {
      for (int32_t i = 0; i < s_comp_reg.count; ++i) {
        const char *name = s_comp_reg.entries[i].name;
        tb_auto load_fn = s_comp_reg.entries[i].load_fn;
        if (SDL_strcmp(component_name, name) == 0) {
          load_fn(world, ent, root_scene_path, node, component_obj);
        }
      }
    }
  }

  ecs_enable(ecs, ent, enabled);

  TB_DYN_ARR_APPEND(scene->entities, ent);
}

bool tb_load_scene(TbWorld *world, const char *scene_path) {
  // Get qualified path to scene asset
  char *asset_path = tb_resolve_asset_path(world->tmp_alloc, scene_path);

  // Load glb off disk
  cgltf_data *data = tb_read_glb(world->gp_alloc, asset_path);
  TB_CHECK_RETURN(data, "Failed to load glb", false);

  json_tokener *tok = json_tokener_new();

  TbScene scene = {0};
  TB_DYN_ARR_RESET(scene.entities, world->gp_alloc, data->scene->nodes_count);

  // Create an entity for each node
  for (cgltf_size i = 0; i < data->scene->nodes_count; ++i) {
    tb_auto node = data->scene->nodes[i];
    load_entity(world, &scene, tok, data, scene_path, TbInvalidEntityId, node);
  }

  TB_DYN_ARR_APPEND(world->scenes, scene);

  json_tokener_free(tok);

  // Clean up gltf file now that it's parsed
  cgltf_free(data);

  return true;
}

void tb_unload_scene(TbWorld *world, TbScene *scene) {
  ecs_world_t *ecs = world->ecs;
  // Remove all entities in the scene from the world
  TB_DYN_ARR_FOREACH(scene->entities, i) {
    ecs_delete(ecs, TB_DYN_ARR_AT(scene->entities, i));
  }
}
