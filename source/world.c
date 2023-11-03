#include "world.h"

#include "allocator.h"
#include "assets.h"
#include "assetsystem.h"
#include "profiling.h"
#include "simd.h"
#include "tbcommon.h"
#include "tbgltf.h"

#include "cameracomponent.h"
#include "lightcomponent.h"
#include "meshcomponent.h"
#include "noclipcomponent.h"
#include "oceancomponent.h"
#include "skycomponent.h"
#include "transformcomponent.h"
#include "transformercomponents.h"

#include "audiosystem.h"
#include "camerasystem.h"
#include "coreuisystem.h"
#include "imguisystem.h"
#include "inputsystem.h"
#include "lightsystem.h"
#include "materialsystem.h"
#include "meshsystem.h"
#include "noclipcontrollersystem.h"
#include "oceansystem.h"
#include "physicssystem.h"
#include "renderobjectsystem.h"
#include "renderpipelinesystem.h"
#include "rendersystem.h"
#include "rendertargetsystem.h"
#include "rotatorsystem.h"
#include "shadowsystem.h"
#include "skysystem.h"
#include "texturesystem.h"
#include "timeofdaysystem.h"
#include "viewsystem.h"
#include "visualloggingsystem.h"

#include <flecs.h>
#include <json.h>
#include <mimalloc.h>

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

TbWorld tb_create_world(Allocator std_alloc, Allocator tmp_alloc,
                        RenderThread *render_thread, SDL_Window *window) {
  // Ensure the instrumented allocator is used
  ecs_os_set_api_defaults();
  ecs_os_api_t os_api = ecs_os_api;
  os_api.malloc_ = ecs_malloc;
  os_api.free_ = ecs_free;
  os_api.calloc_ = ecs_calloc;
  os_api.realloc_ = ecs_realloc;
  ecs_os_set_api(&os_api);

  TbWorld world = {
      .ecs = ecs_init(),
      .std_alloc = std_alloc,
      .tmp_alloc = tmp_alloc,
  };
  TB_DYN_ARR_RESET(world.scenes, std_alloc, 1);

  ecs_world_t *ecs = world.ecs;
  tb_register_physics_sys(&world);
  tb_register_light_sys(ecs);
  tb_register_audio_sys(&world);
  tb_register_render_sys(ecs, std_alloc, tmp_alloc, render_thread);
  tb_register_input_sys(ecs, tmp_alloc, window);
  tb_register_render_target_sys(ecs, std_alloc, tmp_alloc);
  tb_register_texture_sys(ecs, std_alloc, tmp_alloc);
  tb_register_view_sys(ecs, std_alloc, tmp_alloc);
  tb_register_render_object_sys(ecs, std_alloc, tmp_alloc);
  tb_register_render_pipeline_sys(ecs, std_alloc, tmp_alloc);
  tb_register_material_sys(ecs, std_alloc, tmp_alloc);
  tb_register_mesh_sys(ecs, std_alloc, tmp_alloc);
  tb_register_sky_sys(ecs, std_alloc, tmp_alloc);
  tb_register_imgui_sys(ecs, std_alloc, tmp_alloc);
  tb_register_noclip_sys(ecs);
  tb_register_core_ui_sys(ecs, std_alloc, tmp_alloc);
  tb_register_visual_logging_sys(ecs, std_alloc, tmp_alloc);
  tb_register_ocean_sys(ecs, std_alloc, tmp_alloc);
  tb_register_camera_sys(ecs, std_alloc, tmp_alloc);
  tb_register_shadow_sys(&world);
  tb_register_time_of_day_sys(ecs);
  tb_register_rotator_sys(ecs, tmp_alloc);

// By setting this singleton we allow the application to connect to the
// flecs explorer
#ifndef FINAL
  ecs_singleton_set(ecs, EcsRest, {0});
#endif

  return world;
}

bool tb_tick_world(TbWorld *world, float delta_seconds) {
  TracyCZoneNC(ctx, "World Tick", TracyCategoryColorCore, true);
  ecs_world_t *ecs = world->ecs;

  // Tick with flecs
  if (!ecs_progress(ecs, delta_seconds)) {
    return false;
  }
  // Manually check flecs for quit event
  ECS_COMPONENT(ecs, InputSystem);
  const InputSystem *in_sys = ecs_singleton_get(ecs, InputSystem);
  if (in_sys) {
    for (uint32_t event_idx = 0; event_idx < in_sys->event_count; ++event_idx) {
      if (in_sys->events[event_idx].type == SDL_QUIT) {
        TracyCZoneEnd(ctx);
        return false;
      }
    }
  }
  TracyCZoneEnd(ctx);
  return true;
}

void tb_clear_world(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;
  TB_DYN_ARR_FOREACH(world->scenes, i) {
    TbScene *s = &TB_DYN_ARR_AT(world->scenes, i);
    tb_unload_scene(world, s);
  }
  TB_DYN_ARR_CLEAR(world->scenes);
  // Progress ecs to process deletions
  ecs_progress(ecs, .1);
}

void tb_destroy_world(TbWorld *world) {
  // Clean up singletons
  ecs_world_t *ecs = world->ecs;

  // Unregister systems so that they will be cleaned up by observers in ecs_fini
  tb_unregister_physics_sys(world);
  tb_unregister_rotator_sys(ecs);
  tb_unregister_time_of_day_sys(ecs);
  tb_unregister_camera_sys(ecs);
  tb_unregister_shadow_sys(ecs);
  tb_unregister_visual_logging_sys(ecs);
  tb_unregister_ocean_sys(ecs);
  tb_unregister_sky_sys(ecs);
  tb_unregister_core_ui_sys(ecs);
  tb_unregister_imgui_sys(ecs);
  tb_unregister_mesh_sys(ecs);
  tb_unregister_material_sys(ecs);
  tb_unregister_render_pipeline_sys(ecs);
  tb_unregister_render_object_sys(ecs);
  tb_unregister_view_sys(ecs);
  tb_unregister_texture_sys(ecs);
  tb_unregister_render_target_sys(ecs);
  tb_unregister_render_sys(ecs);
  tb_unregister_light_sys(ecs);

  ecs_fini(ecs);
}

ecs_entity_t load_entity(ecs_world_t *ecs, Allocator std_alloc,
                         Allocator tmp_alloc, json_tokener *tok,
                         const cgltf_data *data, const char *root_scene_path,
                         ecs_entity_t parent, const cgltf_node *node) {
  ECS_TAG(ecs, NoTransform);
  ECS_COMPONENT(ecs, TransformComponent);
  ECS_COMPONENT(ecs, AssetSystem);
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

  // Create an entity
  ecs_entity_t e = ecs_new(ecs, 0);

  // Attempt to add a component for each asset system provided
  ecs_filter_t *asset_filter = ecs_filter(ecs, {.terms = {
                                                    {.id = ecs_id(AssetSystem)},
                                                }});
  ecs_iter_t asset_it = ecs_filter_iter(ecs, asset_filter);
  while (ecs_filter_next(&asset_it)) {
    AssetSystem *asset_sys = ecs_field(&asset_it, AssetSystem, 1);
    for (int32_t i = 0; i < asset_it.count; ++i) {
      if (!asset_sys[i].add_fn(ecs, e, root_scene_path, node, json)) {
        TB_CHECK_RETURN(false, "Failed to handle component parsing", 0);
      }
    }
  }
  ecs_filter_fini(asset_filter);

  // Add a transform component to the entity unless directed not to
  // by some loaded component
  if (!ecs_has(ecs, e, NoTransform)) {
    TransformComponent trans = {
        .dirty = true,
        .parent = parent,
        .child_count = node->children_count,
        .transform = tb_transform_from_node(node),
    };
    ecs_set_ptr(ecs, e, TransformComponent, &trans);
  }
  if (node->name) {
    ecs_set_name(ecs, e, node->name);
  }

  if (node->children_count > 0) {
    TransformComponent *trans_comp = ecs_get_mut(ecs, e, TransformComponent);
    // Make sure this entity actually has a transform
    if (trans_comp) {
      trans_comp->children =
          tb_alloc_nm_tp(std_alloc, node->children_count, ecs_entity_t);
      ecs_entity_t *children = trans_comp->children;

      // Load all children
      for (uint32_t i = 0; i < node->children_count; ++i) {
        const cgltf_node *child = node->children[i];
        children[i] = load_entity(ecs, std_alloc, tmp_alloc, tok, data,
                                  root_scene_path, e, child);
      }
      ecs_modified(ecs, e, TransformComponent);
    }
  }

  return e;
}

bool tb_load_scene(TbWorld *world, const char *scene_path) {
  // Get qualified path to scene asset
  char *asset_path = tb_resolve_asset_path(world->tmp_alloc, scene_path);

  // Load glb off disk
  cgltf_data *data = tb_read_glb(world->std_alloc, asset_path);
  TB_CHECK_RETURN(data, "Failed to load glb", false);

  json_tokener *tok = json_tokener_new();

  TbScene scene = {0};
  TB_DYN_ARR_RESET(scene.entities, world->std_alloc, data->scene->nodes_count);

  // Create an entity for each node
  for (cgltf_size i = 0; i < data->scene->nodes_count; ++i) {
    const cgltf_node *node = data->scene->nodes[i];
    ecs_entity_t e = load_entity(world->ecs, world->std_alloc, world->tmp_alloc,
                                 tok, data, scene_path, InvalidEntityId, node);
    TB_DYN_ARR_APPEND(scene.entities, e);
  }

  TB_DYN_ARR_APPEND(world->scenes, scene);

  json_tokener_free(tok);

  // Clean up gltf file now that it's parsed
  cgltf_free(data);

  return true;
}

void tb_unload_scene(TbWorld *world, TbScene *scene) {
  ecs_world_t *ecs = world->ecs;
  ECS_COMPONENT(ecs, AssetSystem);

  // Remove all components managed by the asset system from the scene
  // TODO: This doesn't handle the case of multiple scenes
  ecs_filter_t *asset_filter = ecs_filter(ecs, {.terms = {
                                                    {.id = ecs_id(AssetSystem)},
                                                }});
  ecs_iter_t asset_it = ecs_filter_iter(ecs, asset_filter);
  while (ecs_filter_next(&asset_it)) {
    AssetSystem *asset_sys = ecs_field(&asset_it, AssetSystem, 1);
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
