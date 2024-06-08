#include "tb_scene2.h"

#include "tb_allocator.h"
#include "tb_assets.h"
#include "tb_common.h"
#include "tb_gltf.h"
#include "tb_material_system.h"
#include "tb_mesh_system2.h"
#include "tb_profiling.h"
#include "tb_task_scheduler.h"
#include "tb_texture_system.h"
#include "tb_world.h"

#include <json.h>

typedef struct TbEntityLoadRequest {
  ecs_world_t *ecs;
  const char *source_path;
  const cgltf_data *data;
  const cgltf_node *node;
  json_object *json;
  TbLoadComponentFn load_fn;
} TbEntityLoadRequest;
typedef TB_QUEUE_OF(TbEntityLoadRequest) TbEntityTaskQueue;

ECS_COMPONENT_DECLARE(TbEntityTaskQueue);

ECS_TAG_DECLARE(TbSceneParsing);
ECS_TAG_DECLARE(TbSceneParsed);
ECS_TAG_DECLARE(TbSceneLoading);
ECS_TAG_DECLARE(TbSceneLoaded);

void tb_load_entity(TbEntityTaskQueue queue, ecs_world_t *ecs, const char *path,
                    cgltf_data *data, const cgltf_node *node,
                    json_object *json) {

  // TbLoadComponentFn load_fn = tb_get_component_load_fn();

  // Lookup component load fn in the registry

  TbEntityLoadRequest req = {
      .ecs = ecs, .source_path = path, .data = data, .node = node, .json = json,
      //.load_fn = load_fn,
  };
  TB_QUEUE_PUSH(queue, req);
}

typedef struct TbSceneParsedArgs {
  ecs_world_t *ecs;
  TbScene2 scene;
  const cgltf_data *data;
  const char *path;
  TbScene2 scene_ent;
} TbSceneParsedArgs;

void tb_scene_loaded(const void *args) {
  TracyCZoneN(ctx, "Scene Loaded", true);
  tb_auto load_args = (const TbSceneParsedArgs *)args;
  tb_auto scene = load_args->scene;
  tb_auto ecs = load_args->ecs;
  tb_auto data = load_args->data;

  // Reserve space for the assets
  tb_mesh_sys_reserve_mesh_count(ecs, data->meshes_count);
  tb_mat_sys_reserve_mat_count(ecs, data->materials_count);
  tb_tex_sys_reserve_tex_count(ecs, data->textures_count);

  // ecs_remove(ecs, scene, TbSceneParsing);
  // ecs_add(ecs, scene, TbSceneParsed);

  TracyCZoneEnd(ctx);
}

typedef struct TbParseSceneArgs {
  ecs_world_t *ecs;
  TbTaskScheduler enki;
  TbScene2 scene;
  TbEntityTaskQueue *queue;
  const char *scene_path;
  TbPinnedTask parsed_task;
} TbParseSceneArgs;

void tb_parse_scene_task(const void *args) {
  TB_TRACY_SCOPE("Parse GLTF Scene Task");
  tb_auto load_args = (const TbParseSceneArgs *)args;

  tb_auto ecs = load_args->ecs;
  tb_auto enki = load_args->enki;
  tb_auto scene = load_args->scene;
  tb_auto path = load_args->scene_path;
  tb_auto parsed_task = load_args->parsed_task;
  tb_auto queue = load_args->queue;

  // Read gltf onto the global allocator so it can be safely freed from any
  // thread later
  tb_auto data = tb_read_glb(tb_global_alloc, path);

  json_tokener *tok = json_tokener_new(); // TODO: clean this up alongside data

  // Create an entity for each node
  /*
  for (cgltf_size i = 0; i < data->scene->nodes_count; ++i) {
    tb_auto node = data->scene->nodes[i];

    json_object *json = NULL;
    {
      cgltf_size extra_size = 0;
      char *extra_json = NULL;
      if (node->extras.end_offset != 0 && node->extras.start_offset != 0) {
        extra_size = (node->extras.end_offset - node->extras.start_offset) + 1;
        extra_json = tb_alloc_nm_tp(tb_thread_alloc, extra_size, char);
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

    tb_load_entity(*queue, ecs, path, data, node, json);
  }
  */

  TbSceneParsedArgs loaded_args = {
      .ecs = ecs,
      .data = data,
      .scene_ent = scene,
      .path = path,
  };
  tb_launch_pinned_task_args(enki, parsed_task, &loaded_args,
                             sizeof(TbSceneParsedArgs));
}

TbScene2 tb_load_scene2(ecs_world_t *ecs, const char *scene_path) {
  // If an entity already exists with this name it is either loading or loaded
  TbScene2 scene = ecs_lookup(ecs, scene_path);
  if (scene != 0) {
    return scene;
  }

  TbTaskScheduler enki = *ecs_singleton_get(ecs, TbTaskScheduler);

  scene = ecs_new_entity(ecs, 0);
  ecs_set_name(ecs, scene, scene_path);

  // Need to copy strings for task safety
  // Tasks are responsible for freeing these names
  const size_t path_len = SDL_strnlen(scene_path, 256) + 1;
  char *path_cpy = tb_alloc_nm_tp(tb_global_alloc, path_len, char);
  SDL_strlcpy(path_cpy, scene_path, path_len);

  // This pinned task will be launched by the loading task
  TbPinnedTask parsed_task =
      tb_create_pinned_task(enki, tb_scene_loaded, NULL, 0);

  // Launch a task to open the scene, parse it, and load relevant children
  TbParseSceneArgs args = {
      .ecs = ecs,
      .enki = enki,
      .scene = scene,
      .scene_path = path_cpy,
      .parsed_task = parsed_task,
  };
  TbTask load_task =
      tb_async_task(enki, tb_parse_scene_task, &args, sizeof(TbParseSceneArgs));
  (void)load_task;

  TbEntityTaskQueue entity_queue = {0};
  TB_QUEUE_RESET(entity_queue, tb_global_alloc, 8192);

  // ecs_set_ptr(ecs, scene, TbEntityTaskQueue, &entity_queue);
  // ecs_add(ecs, scene, TbSceneParsing);

  return scene;
}

void tb_load_entites(ecs_iter_t *it) {
  static const uint32_t MAX_ENTITIES_DEQUEUE_PER_FRAME = 16;

  tb_auto entity_queues = ecs_field(it, TbEntityTaskQueue, 1);
  uint32_t dequeue_counter = 0;
  bool exit = false;

  TbEntityLoadRequest load_req = {0};
  for (int32_t i = 0; i < it->count; ++i) {
    tb_auto entity_queue = entity_queues[i];
    TbScene2 scene = it->entities[i];
    while (TB_QUEUE_POP(entity_queue, &load_req)) {
      if (dequeue_counter >= MAX_ENTITIES_DEQUEUE_PER_FRAME) {
        exit = true;
        break;
      }

      // Do entity loading on main thread
      // load_req.load_fn(load_req.ecs, );
    }

    if (exit) {
      break;
    }

    ecs_remove(it->world, scene, TbSceneLoading);
    ecs_add(it->world, scene, TbSceneLoaded);
  }
}

void tb_register_scene_sys(TbWorld *world) {
  tb_auto ecs = world->ecs;
  ECS_COMPONENT_DEFINE(ecs, TbEntityTaskQueue);

  // ECS_SYSTEM(ecs, tb_load_entities, EcsOnLoad, TbEntityTaskQueue,
  //            TbSceneParsed);
}

void tb_unregister_scene_sys(TbWorld *world) { (void)world; }

TB_REGISTER_SYS(tb, scene, TB_SYSTEM_NORMAL)
