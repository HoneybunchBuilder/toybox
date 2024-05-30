#include "tb_scene2.h"

#include "tb_allocator.h"
#include "tb_assets.h"
#include "tb_common.h"
#include "tb_gltf.h"
#include "tb_profiling.h"
#include "tb_task_scheduler.h"

typedef TB_QUEUE_OF(TbPinnedTask) TbEntityTaskQueue;

void tb_load_entity(TbEntityTaskQueue *queue, const cgltf_node *node) {}

typedef struct TbLoadSceneArgs {
  TbTaskScheduler enki;
  TbScene2 scene;
  const char *scene_path;
} TbLoadSceneArgs;

void tb_load_scene_task(const void *args) {
  TracyCZoneN(ctx, "Load GLTF Scene Task", true);
  tb_auto load_args = (const TbLoadSceneArgs *)args;

  tb_auto enki = load_args->enki;
  tb_auto scene = load_args->scene;
  tb_auto path = load_args->scene_path;

  // Read gltf onto the global allocator so it can be safely freed from any
  // thread later
  tb_auto data = tb_read_glb(tb_global_alloc, path);

  // Create an entity for each node
  // Each entity will have a task created for it
  TbEntityTaskQueue load_tasks = {0};
  TB_QUEUE_RESET(load_tasks, tb_global_alloc, 8192);
  for (cgltf_size i = 0; i < data->scene->nodes_count; ++i) {
    tb_auto node = data->scene->nodes[i];
    tb_load_entity(&load_tasks, node);
  }

  TracyCZoneEnd(ctx);
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

  // Launch a task to open the scene, parse it, and load relevant children
  TbLoadSceneArgs args = {
      .enki = enki,
      .scene = scene,
      .scene_path = path_cpy,
  };
  TbTask load_task =
      tb_async_task(enki, tb_load_scene_task, &args, sizeof(TbLoadSceneArgs));

  return scene;
}
