#include "tb_scene.h"

#include "tb_allocator.h"
#include "tb_assets.h"
#include "tb_common.h"
#include "tb_gltf.h"
#include "tb_material_system.h"
#include "tb_mesh_system2.h"
#include "tb_profiling.h"
#include "tb_task_scheduler.h"
#include "tb_texture_system.h"
#include "tb_transform_component.h"
#include "tb_world.h"

#include <flecs.h>
#include <json.h>

typedef struct TbEntityLoadRequest {
  ecs_world_t *ecs;
  const char *source_path;
  const cgltf_data *data;
  const cgltf_node *node;
  json_object *json;
} TbEntityLoadRequest;
typedef TB_QUEUE_OF(TbEntityLoadRequest) TbEntityTaskQueue;

ECS_COMPONENT_DECLARE(TbEntityTaskQueue);

typedef const struct cgltf_node *TbNode;
ECS_COMPONENT_DECLARE(TbNode);
ECS_TAG_DECLARE(TbParentRequest);

typedef uint32_t TbSceneEntityCount;
ECS_COMPONENT_DECLARE(TbSceneEntityCount);
typedef uint32_t TbSceneEntParseCounter;
ECS_COMPONENT_DECLARE(TbSceneEntParseCounter);
typedef uint32_t TbSceneEntReadyCounter;
ECS_COMPONENT_DECLARE(TbSceneEntReadyCounter);

typedef TbScene TbSceneRef;
ECS_COMPONENT_DECLARE(TbSceneRef);

ECS_TAG_DECLARE(TbSceneRoot);
ECS_TAG_DECLARE(TbSceneParsing);
ECS_TAG_DECLARE(TbSceneParsed);
ECS_TAG_DECLARE(TbSceneLoading);
ECS_TAG_DECLARE(TbSceneLoaded);
ECS_TAG_DECLARE(TbSceneReady);
ECS_TAG_DECLARE(TbComponentsReady);
ECS_TAG_DECLARE(TbEntityReady);

typedef struct TbSceneParsedArgs {
  ecs_world_t *ecs;
  TbScene scene;
  uint32_t local_parent;
  const char *path;
  TbEntityTaskQueue *queue;
} TbSceneParsedArgs;

void tb_scene_parsed(const void *args) {
  TracyCZoneN(ctx, "Scene Parsed", true);
  tb_auto load_args = (const TbSceneParsedArgs *)args;
  tb_auto scene = load_args->scene;
  tb_auto ecs = load_args->ecs;
  tb_auto queue = load_args->queue;
  tb_auto used_node_count = TB_DYN_ARR_SIZE(queue->storage);

  ecs_remove(ecs, scene, TbSceneParsing);
  ecs_add(ecs, scene, TbSceneParsed);

  ecs_set_ptr(ecs, scene, TbEntityTaskQueue, queue);
  ecs_set(ecs, scene, TbSceneEntityCount, {used_node_count});
  ecs_set(ecs, scene, TbSceneEntParseCounter, {used_node_count}); // Counts down
  ecs_set(ecs, scene, TbSceneEntReadyCounter, {0});               // Counts up

  TracyCZoneEnd(ctx);
}

typedef struct TbParseSceneArgs {
  ecs_world_t *ecs;
  TbTaskScheduler enki;
  TbScene scene;
  TbEntityTaskQueue *queue;
  const char *scene_path;
  TbPinnedTask parsed_task;
} TbParseSceneArgs;

void tb_enqueue_entity_parse_req(ecs_world_t *ecs, const char *path,
                                 TbEntityTaskQueue *queue, json_tokener *tok,
                                 const cgltf_data *data,
                                 const cgltf_node *node) {
  TB_TRACY_SCOPE("Enqueue Entity Parse Req");
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

  // Enqueue entity load request
  TbEntityLoadRequest req = {
      .ecs = ecs,
      .source_path = path,
      .data = data,
      .node = node,
      .json = json,
  };
  TB_QUEUE_PUSH_PTR(queue, req);

  for (cgltf_size i = 0; i < node->children_count; ++i) {
    tb_enqueue_entity_parse_req(ecs, path, queue, tok, data, node->children[i]);
  }
}

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
  for (cgltf_size i = 0; i < data->scenes[0].nodes_count; ++i) {
    tb_auto node = data->scenes[0].nodes[i];
    tb_enqueue_entity_parse_req(ecs, path, queue, tok, data, node);
  }

  TbSceneParsedArgs parsed_args = {
      .ecs = ecs,
      .scene = scene,
      .path = path,
      .queue = queue,
  };
  tb_launch_pinned_task_args(enki, parsed_task, &parsed_args,
                             sizeof(TbSceneParsedArgs));
}

TbScene tb_create_scene(ecs_world_t *ecs, const char *scene_path) {
  TB_TRACY_SCOPE("Create Scene");
  // If an entity already exists with this name it is either loading or loaded
  TbScene scene = ecs_lookup(ecs, scene_path);
  if (scene != 0) {
    return scene;
  }

  tb_auto enki = *ecs_singleton_get(ecs, TbTaskScheduler);

  scene = ecs_new_entity(ecs, 0);
  ecs_set_name(ecs, scene, scene_path);

  // Need to copy strings for task safety
  // Tasks are responsible for freeing these names
  const size_t path_len = SDL_strnlen(scene_path, 256) + 1;
  char *path_cpy = tb_alloc_nm_tp(tb_global_alloc, path_len, char);
  SDL_strlcpy(path_cpy, scene_path, path_len);

  // This pinned task will be launched by the loading task
  TbPinnedTask parsed_task =
      tb_create_pinned_task(enki, tb_scene_parsed, NULL, 0);

  tb_auto entity_queue = ecs_get_mut(ecs, scene, TbEntityTaskQueue);
  *entity_queue = (TbEntityTaskQueue){0};
  TB_QUEUE_RESET(*entity_queue, tb_global_alloc, 8192);

  // Launch a task to open the scene, parse it, and load relevant children
  TbParseSceneArgs args = {
      .ecs = ecs,
      .enki = enki,
      .scene = scene,
      .scene_path = path_cpy,
      .parsed_task = parsed_task,
      .queue = entity_queue,
  };
  TbTask load_task =
      tb_async_task(enki, tb_parse_scene_task, &args, sizeof(TbParseSceneArgs));

  ecs_set(ecs, scene, TbTask, {load_task});
  ecs_add(ecs, scene, TbSceneParsing);
  ecs_add(ecs, scene, TbSceneRoot);

  return scene;
}

ecs_entity_t tb_load_entity(ecs_world_t *ecs, const char *source_path,
                            const cgltf_data *data, const cgltf_node *node,
                            json_object *json) {
  TB_TRACY_SCOPE("Load Entity");
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

  // Determine what to call this entity
  const char *name = node->name;
  if (name && ecs_lookup(ecs, name) != TbInvalidEntityId) {
    name = NULL;
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

  // We don't know our parent yet until all entities have been loaded
  // Associate the node with the entity to be able to resolve parents
  ecs_set(ecs, ent, TbNode, {node});
  if (node->parent) {
    ecs_add(ecs, ent, TbParentRequest);
  }

  // Some default components need to be tested for
  {
    tb_auto load_fn = tb_get_component_load_fn("transform");
    if (load_fn) {
      load_fn(ecs, ent, source_path, data, node, NULL);
    }
    if (node->mesh) {
      load_fn = tb_get_component_load_fn("mesh");
      if (load_fn) {
        load_fn(ecs, ent, source_path, data, node, NULL);
      }
    }
    if (node->camera) {
      load_fn = tb_get_component_load_fn("camera");
      if (load_fn) {
        load_fn(ecs, ent, source_path, data, node, NULL);
      }
    }
    if (node->light) {
      load_fn = tb_get_component_load_fn("light");
      if (load_fn) {
        load_fn(ecs, ent, source_path, data, node, NULL);
      }
    }
  }

  // Add custom components
  if (json) {
    json_object_object_foreach(json, component_name, component_obj) {
      TB_TRACY_SCOPE("Component");
      tb_auto load_fn = tb_get_component_load_fn(component_name);
      if (load_fn) {
        load_fn(ecs, ent, source_path, data, node, component_obj);
      }
    }
  }

  ecs_enable(ecs, ent, enabled);
  return ent;
}

void tb_load_entities(ecs_iter_t *it) {
  TB_TRACY_SCOPE("Load Entities");
  //  TODO: Really this should be timesliced since entities may take
  //  a while to load their components
  static const uint32_t MAX_ENTITIES_DEQUEUE_PER_FRAME = 16;

  tb_auto entity_queues = ecs_field(it, TbEntityTaskQueue, 1);
  tb_auto counters = ecs_field(it, TbSceneEntParseCounter, 2);
  uint32_t dequeue_counter = 0;
  bool exit = false;

  TbEntityLoadRequest load_req = {0};
  for (int32_t i = 0; i < it->count; ++i) {
    tb_auto entity_queue = &entity_queues[i];
    tb_auto scene_counter = &counters[i];
    if (*scene_counter == 0) {
      continue;
    }

    TbScene scene = it->entities[i];
    while (TB_QUEUE_POP(*entity_queue, &load_req)) {
      if (dequeue_counter >= MAX_ENTITIES_DEQUEUE_PER_FRAME ||
          *scene_counter == 0) {
        dequeue_counter = 0;
        exit = true;
        // USED TO HAVE A BREAK HERE BUT THAT IS NOT CORRECT
        // IF WE POP AN ENTITY FROM THE QUEUE WE MUST PROCESS IT
      }

      tb_auto ecs = load_req.ecs;
      tb_auto source_path = load_req.source_path;
      tb_auto data = load_req.data;
      tb_auto node = load_req.node;
      tb_auto json = load_req.json;

      tb_auto ent = tb_load_entity(ecs, source_path, data, node, json);

      // Entities need a refernce to their parent scene since they may not
      // be directly parented
      ecs_set(ecs, ent, TbSceneRef, {scene});

      (*scene_counter)--;
      dequeue_counter++;
    }

    if (*scene_counter == 0) {
      ecs_remove(it->world, scene, TbSceneParsed);
      ecs_add(it->world, scene, TbSceneLoading);
    }

    if (exit) {
      break;
    }
  }
}

void tb_resolve_parents(ecs_iter_t *it) {
  TB_TRACY_SCOPE("Resolve Parents");
  tb_auto ecs = it->world;

  tb_auto nodes = ecs_field(it, TbNode, 2);
  tb_auto scene_refs = ecs_field(it, TbSceneRef, 3);
  for (int32_t i = 0; i < it->count; ++i) {
    tb_auto entity = it->entities[i];
    tb_auto node = nodes[i];
    tb_auto scene = scene_refs[i];

    tb_auto filter = ecs_filter(ecs, {.terms = {
                                          {.id = ecs_id(TbNode)},
                                          {.id = ecs_id(TbSceneRef)},
                                      }});
    tb_auto filter_it = ecs_filter_iter(ecs, filter);

    while (ecs_filter_next(&filter_it)) {
      tb_auto parent_nodes = ecs_field(&filter_it, TbNode, 1);
      tb_auto parent_scenes = ecs_field(&filter_it, TbSceneRef, 2);
      for (int32_t node_idx = 0; node_idx < filter_it.count; ++node_idx) {
        if (parent_scenes[node_idx] != scene) {
          continue;
        }
        tb_auto parent_ent = filter_it.entities[node_idx];
        tb_auto parent_node = parent_nodes[node_idx];
        if (node->parent == parent_node) {
          // TODO: Should probably only allow a few of these per frame
          ecs_add_pair(ecs, entity, EcsChildOf, parent_ent);
          ecs_remove(ecs, entity, TbParentRequest);
        }
      }
    }

    ecs_filter_fini(filter);
  }
}

void tb_ready_check_components(ecs_iter_t *it) {
  TB_TRACY_SCOPE("Ready Check Components");
  tb_auto ecs = it->world;

  for (int32_t ent_idx = 0; ent_idx < it->count; ++ent_idx) {
    tb_auto ent = it->entities[ent_idx];
    if (tb_enitity_components_ready(ecs, ent)) {
      ecs_add(ecs, ent, TbComponentsReady);
    }
  }
}

void tb_ready_check_entities(ecs_iter_t *it) {
  TB_TRACY_SCOPE("Ready Check Entities");
  tb_auto ecs = it->world;

  tb_auto ent_totals = ecs_field(it, TbSceneEntityCount, 1);
  tb_auto counters = ecs_field(it, TbSceneEntReadyCounter, 2);
  for (int32_t scene_idx = 0; scene_idx < it->count; ++scene_idx) {
    tb_auto scene = it->entities[scene_idx];
    tb_auto counter = &counters[scene_idx];
    tb_auto total = ent_totals[scene_idx];

    bool complete = false;

    tb_auto filter = ecs_filter(ecs, {.terms = {
                                          {.id = ecs_id(TbNode)},
                                      }});
    tb_auto filter_it = ecs_filter_iter(ecs, filter);
    while (ecs_filter_next(&filter_it)) {
      for (int32_t ent_idx = 0; ent_idx < filter_it.count; ++ent_idx) {
        tb_auto entity = filter_it.entities[ent_idx];

        // Entity already ready
        if (ecs_has(ecs, entity, TbEntityReady)) {
          continue;
        }

        // Not relevant to this scene
        if (scene != *ecs_get(ecs, entity, TbSceneRef)) {
          continue;
        }
        // Parent is still being resolved
        if (ecs_has(ecs, entity, TbParentRequest)) {
          continue;
        }
        // Components are still pending
        if (!ecs_has(ecs, entity, TbComponentsReady)) {
          continue;
        }

        // Entity is ready! Remove unnecessary components and mark as ready
        ecs_remove(ecs, entity, TbNode);
        ecs_add(ecs, entity, TbEntityReady);
        (*counter)++;
        if (*counter == total) {
          complete = true;
          break;
        }
      }
    }
    ecs_filter_fini(filter);

    if (complete) {
      ecs_remove(ecs, scene, TbSceneLoading);
      ecs_add(ecs, scene, TbSceneLoaded);
    }
  }
}

void tb_ready_check_scenes(ecs_iter_t *it) {
  TB_TRACY_SCOPE("Ready Check Scenes");
  tb_auto ecs = it->world;
  for (int32_t scene_idx = 0; scene_idx < it->count; ++scene_idx) {
    TbScene scene = it->entities[scene_idx];

    // We're here because all entities are loaded
    // Mark everything as ready!
    tb_auto filter = ecs_filter(ecs, {.terms = {
                                          {.id = ecs_id(TbSceneRef)},
                                      }});
    tb_auto filter_it = ecs_filter_iter(ecs, filter);
    while (ecs_filter_next(&filter_it)) {
      tb_auto scene_refs = ecs_field(&filter_it, TbSceneRef, 1);
      for (int32_t ent_idx = 0; ent_idx < filter_it.count; ++ent_idx) {
        tb_auto entity = filter_it.entities[ent_idx];
        tb_auto scene_ref = scene_refs[ent_idx];
        if (scene != scene_ref) {
          continue;
        }

        if (ecs_has(ecs, entity, TbTransformComponent)) {
          tb_transform_mark_dirty(ecs, entity);
        }
      }
    }

    ecs_remove(ecs, scene, TbSceneLoaded);
    ecs_add(ecs, scene, TbSceneReady);
  }
}

void tb_register_scene_sys(TbWorld *world) {
  tb_auto ecs = world->ecs;
  ECS_COMPONENT_DEFINE(ecs, TbEntityTaskQueue);
  ECS_COMPONENT_DEFINE(ecs, TbSceneEntityCount);
  ECS_COMPONENT_DEFINE(ecs, TbSceneEntParseCounter);
  ECS_COMPONENT_DEFINE(ecs, TbSceneEntReadyCounter);
  ECS_COMPONENT_DEFINE(ecs, TbNode);
  ECS_COMPONENT_DEFINE(ecs, TbSceneRef);
  ECS_TAG_DEFINE(ecs, TbParentRequest);
  ECS_TAG_DEFINE(ecs, TbSceneRoot);
  ECS_TAG_DEFINE(ecs, TbSceneParsing);
  ECS_TAG_DEFINE(ecs, TbSceneParsed);
  ECS_TAG_DEFINE(ecs, TbSceneLoading);
  ECS_TAG_DEFINE(ecs, TbSceneLoaded);
  ECS_TAG_DEFINE(ecs, TbSceneReady);
  ECS_TAG_DEFINE(ecs, TbComponentsReady);
  ECS_TAG_DEFINE(ecs, TbEntityReady);

  // This is a no-readonly system because we are adding entities
  ecs_system(ecs,
             {.entity = ecs_entity(ecs, {.name = "tb_load_entities",
                                         .add = {ecs_dependson(EcsOnLoad)}}),
              .query.filter.terms = {{.id = ecs_id(TbEntityTaskQueue)},
                                     {.id = ecs_id(TbSceneEntParseCounter)},
                                     {.id = ecs_id(TbSceneParsed)}},
              .callback = tb_load_entities,
              .no_readonly = true});

  ECS_SYSTEM(ecs, tb_resolve_parents,
             EcsPostLoad, [in] TbParentRequest, [in] TbNode, [in] TbSceneRef);

  ECS_SYSTEM(ecs, tb_ready_check_components,
             EcsPostLoad, [in] TbNode, [in] !TbComponentsReady);

  ECS_SYSTEM(ecs, tb_ready_check_entities,
             EcsPostLoad, [in] TbSceneEntityCount, [out] TbSceneEntReadyCounter,
             TbSceneLoading);

  ECS_SYSTEM(ecs, tb_ready_check_scenes, EcsPostLoad, TbSceneLoaded);
}

void tb_unregister_scene_sys(TbWorld *world) { (void)world; }

TB_REGISTER_SYS(tb, scene, TB_SYSTEM_NORMAL)

bool tb_is_scene_ready(ecs_world_t *ecs, TbScene scene) {
  return ecs_has(ecs, scene, TbSceneReady);
}
