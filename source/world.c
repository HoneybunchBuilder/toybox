#include "world.h"

#include "allocator.h"
#include "assets.h"
#include "assetsystem.h"
#include "infomode.h"
#include "profiling.h"
#include "simd.h"
#include "tbcommon.h"
#include "tbgltf.h"

#include "cameracomponent.h"
#include "lightcomponent.h"
#include "meshcomponent.h"
#include "transformcomponent.h"
#include "transformercomponents.h"

#include "inputsystem.h"

#include <flecs.h>
#include <json.h>
#include <mimalloc.h>

typedef struct TbSystemEntry {
  char *name;
  int32_t priority;
  TbCreateSystemFn create_fn;
  TbDestroySystemFn destroy_fn;
} TbSystemEntry;

typedef struct TbSystemRegistry {
  int32_t sys_count;
  TbSystemEntry *entries;
} TbSystemRegistry;

typedef struct TbComponentEntry {
  char *name;
  // TbCreateSystemFn create_fn;
  // TbDestroySystemFn destroy_fn;
} TbComponentEntry;

typedef struct TbComponentRegistry {
  int32_t count;
  TbComponentEntry *entries;
} TbComponentRegistry;

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
ECS_COMPONENT_DECLARE(TbTransformComponent);
ECS_COMPONENT_DECLARE(TbAssetSystem);

static TbSystemRegistry s_sys_reg = {0};

int32_t tb_sys_cmp(const void *a, const void *b) {
  tb_auto sys_a = (const TbSystemEntry *)a;
  tb_auto sys_b = (const TbSystemEntry *)b;
  return sys_a->priority - sys_b->priority;
}

void tb_register_system(const char *name, int32_t priority,
                        TbCreateSystemFn create_fn,
                        TbDestroySystemFn destroy_fn) {
  int32_t index = s_sys_reg.sys_count;
  int32_t next_count = ++s_sys_reg.sys_count;
  size_t entry_size = next_count * sizeof(TbSystemEntry);
  size_t name_len = SDL_strlen(name);

  s_sys_reg.entries = mi_realloc(s_sys_reg.entries, entry_size);
  tb_auto entry = &s_sys_reg.entries[index];
  entry->priority = priority;
  entry->create_fn = create_fn;
  entry->destroy_fn = destroy_fn;
  entry->name = mi_malloc(name_len + 1);
  SDL_strlcpy(entry->name, name, name_len);
}

bool tb_create_world(const TbWorldDesc *desc, TbWorld *world) {
  int32_t info_mode = tb_check_info_mode(desc->argc, desc->argv);

  TbAllocator gp_alloc = desc->gp_alloc;

  // Must create render thread on the heap like this
  TbRenderThread *render_thread = tb_alloc_tp(gp_alloc, TbRenderThread);

  // No render thread in info mode
  if (info_mode == 0) {
    TbRenderThreadDescriptor render_thread_desc = {.window = desc->window};
    TB_CHECK(tb_start_render_thread(&render_thread_desc, render_thread),
             "Failed to start render thread");

    // Do not go initializing anything until we know the render thread is ready
    tb_wait_thread_initialized(render_thread);
  }

  // Ensure the instrumented allocator is used
  ecs_os_set_api_defaults();
  ecs_os_api_t os_api = ecs_os_api;
  os_api.malloc_ = ecs_malloc;
  os_api.free_ = ecs_free;
  os_api.calloc_ = ecs_calloc;
  os_api.realloc_ = ecs_realloc;
  ecs_os_set_api(&os_api);

  *world = (TbWorld){
      .ecs = ecs_init(),
      .window = desc->window,
      .render_thread = render_thread,
      .gp_alloc = gp_alloc,
      .tmp_alloc = desc->tmp_alloc,
  };
  TB_DYN_ARR_RESET(world->scenes, gp_alloc, 1);

  tb_auto ecs = world->ecs;

  // Define some components that no one else will
  ECS_COMPONENT_DEFINE(ecs, float3);
  ECS_COMPONENT_DEFINE(ecs, float4);
  ECS_COMPONENT_DEFINE(ecs, float4x4);
  ECS_COMPONENT_DEFINE(ecs, TbTransform);
  ECS_COMPONENT_DEFINE(ecs, TbTransformComponent);
  ECS_COMPONENT_DEFINE(ecs, TbAssetSystem);

  // Register components from other modules
  tb_register_mesh_component(world);
  tb_register_camera_component(world);

  // Metadata for transform component
  {
    ecs_struct(ecs, {.entity = ecs_id(float3),
                     .members = {
                         {.name = "x", .type = ecs_id(ecs_f32_t)},
                         {.name = "y", .type = ecs_id(ecs_f32_t)},
                         {.name = "z", .type = ecs_id(ecs_f32_t)},
                     }});
    ecs_struct(ecs, {.entity = ecs_id(float4),
                     .members = {
                         {.name = "x", .type = ecs_id(ecs_f32_t)},
                         {.name = "y", .type = ecs_id(ecs_f32_t)},
                         {.name = "z", .type = ecs_id(ecs_f32_t)},
                         {.name = "w", .type = ecs_id(ecs_f32_t)},
                     }});
    ecs_struct(ecs, {.entity = ecs_id(float4x4),
                     .members = {
                         {.name = "col0", .type = ecs_id(float4)},
                         {.name = "col1", .type = ecs_id(float4)},
                         {.name = "col2", .type = ecs_id(float4)},
                         {.name = "col3", .type = ecs_id(float4)},
                     }});
    ecs_struct(ecs, {.entity = ecs_id(TbTransform),
                     .members = {
                         {.name = "position", .type = ecs_id(float3)},
                         {.name = "scale", .type = ecs_id(float3)},
                         {.name = "rotation", .type = ecs_id(float4)},
                     }});
    ecs_struct(ecs,
               {
                   .entity = ecs_id(TbTransformComponent),
                   .members =
                       {
                           {.name = "dirty", .type = ecs_id(ecs_bool_t)},
                           {.name = "world_matrix", .type = ecs_id(float4x4)},
                           {.name = "transform", .type = ecs_id(TbTransform)},
                           {.name = "entity", .type = ecs_id(ecs_entity_t)},
                       },
               });
  }

  // Create all registered systems after sorting by priority
  {
    const int32_t sys_count = s_sys_reg.sys_count;
    SDL_qsort(s_sys_reg.entries, sys_count, sizeof(TbSystemEntry), tb_sys_cmp);

    for (int32_t i = 0; i < s_sys_reg.sys_count; ++i) {
      tb_auto fn = s_sys_reg.entries[i].create_fn;
      if (fn) {
        fn(world);
      }
    }
  }

#ifndef FINAL
  // Run optional info mode
  if (info_mode) {
    tb_write_info(world);
    return false; // do not continue in info mode
  }

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
  ECS_COMPONENT(ecs, TbInputSystem);
  const TbInputSystem *in_sys = ecs_singleton_get(ecs, TbInputSystem);
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
  for (int32_t i = s_sys_reg.sys_count - 1; i >= 0; --i) {
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

  // Attempt to add a component for each asset system provided
  ecs_filter_t *asset_filter =
      ecs_filter(ecs, {.terms = {
                           {.id = ecs_id(TbAssetSystem)},
                       }});
  ecs_iter_t asset_it = ecs_filter_iter(ecs, asset_filter);
  while (ecs_filter_next(&asset_it)) {
    TbAssetSystem *asset_sys = ecs_field(&asset_it, TbAssetSystem, 1);
    for (int32_t i = 0; i < asset_it.count; ++i) {
      if (!asset_sys[i].add_fn(ecs, ent, root_scene_path, node, json)) {
        TB_CHECK(false, "Failed to handle component parsing");
      }
    }
  }
  ecs_filter_fini(asset_filter);

  // Always add a transform
  {
    TbTransformComponent trans = {
        .dirty = true,
        .transform = tb_transform_from_node(node),
    };
    ecs_set_ptr(ecs, ent, TbTransformComponent, &trans);
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

  ecs_enable(ecs, ent, enabled);

  TB_DYN_ARR_APPEND(scene->entities, ent);
}

bool tb_load_scene(TbWorld *world, const char *scene_path) {
  ecs_world_t *ecs = world->ecs;
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

  // Trigger post load events for asset systems that care
  {
    ecs_filter_t *asset_filter =
        ecs_filter(ecs, {.terms = {
                             {.id = ecs_id(TbAssetSystem)},
                         }});
    ecs_iter_t asset_it = ecs_filter_iter(ecs, asset_filter);
    while (ecs_filter_next(&asset_it)) {
      TbAssetSystem *asset_sys = ecs_field(&asset_it, TbAssetSystem, 1);
      for (int32_t i = 0; i < asset_it.count; ++i) {
        TbComponentPostLoadFn post_fn = asset_sys[i].post_load_fn;
        if (post_fn) {
          TB_DYN_ARR_FOREACH(scene.entities, i) {
            post_fn(ecs, TB_DYN_ARR_AT(scene.entities, i));
          }
        }
      }
    }
    ecs_filter_fini(asset_filter);
  }

  return true;
}

void tb_unload_scene(TbWorld *world, TbScene *scene) {
  ecs_world_t *ecs = world->ecs;

  // Remove all components managed by the asset system from the scene
  // TODO: This doesn't handle the case of multiple scenes
  ecs_filter_t *asset_filter =
      ecs_filter(ecs, {.terms = {
                           {.id = ecs_id(TbAssetSystem)},
                       }});
  ecs_iter_t asset_it = ecs_filter_iter(ecs, asset_filter);
  while (ecs_filter_next(&asset_it)) {
    TbAssetSystem *asset_sys = ecs_field(&asset_it, TbAssetSystem, 1);
    for (int32_t i = 0; i < asset_it.count; ++i) {
      asset_sys[i].rem_fn(ecs);
    }
  }
  ecs_filter_fini(asset_filter);

  // Remove all entities in the scene from the world
  TB_DYN_ARR_FOREACH(scene->entities, i) {
    ecs_delete(world->ecs, TB_DYN_ARR_AT(scene->entities, i));
  }
}
