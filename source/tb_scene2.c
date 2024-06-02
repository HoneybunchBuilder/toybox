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

typedef TB_QUEUE_OF(TbPinnedTask) TbEntityTaskQueue;

void tb_load_entity(TbEntityTaskQueue *queue, const cgltf_node *node) {
  (void)queue;
  (void)node;
}

typedef struct TbSceneLoadedArgs {
  ecs_world_t *ecs;
  const cgltf_data *data;
  const char *path;
  TbScene2 scene_ent;
} TbSceneLoadedArgs;

void tb_scene_loaded(const void *args) {
  TracyCZoneN(ctx, "Scene Loaded", true);
  tb_auto load_args = (const TbSceneLoadedArgs *)args;
  tb_auto ecs = load_args->ecs;
  tb_auto path = load_args->path;
  tb_auto data = load_args->data;

  // Reserve space for the assets
  tb_mesh_sys_reserve_mesh_count(ecs, data->meshes_count);
  tb_mat_sys_reserve_mat_count(ecs, data->materials_count);
  tb_tex_sys_reserve_tex_count(ecs, data->textures_count);

  // Loading meshes will trigger dependant materials and textures to load
  for (cgltf_size i = 0; i < data->meshes_count; ++i) {
    const uint32_t max_name_len = 256;
    char name[max_name_len] = {0};
    SDL_snprintf(name, max_name_len, "mesh_%d", i);
    tb_mesh_sys_load_gltf_mesh(ecs, path, name, i);
  }

  tb_free(tb_global_alloc, (void *)path);

  TracyCZoneEnd(ctx);
}

typedef struct TbLoadSceneArgs {
  ecs_world_t *ecs;
  TbTaskScheduler enki;
  TbScene2 scene;
  const char *scene_path;
  TbPinnedTask loaded_task;
} TbLoadSceneArgs;

void tb_load_scene_task(const void *args) {
  TracyCZoneN(ctx, "Load GLTF Scene Task", true);
  tb_auto load_args = (const TbLoadSceneArgs *)args;

  tb_auto ecs = load_args->ecs;
  tb_auto enki = load_args->enki;
  tb_auto scene = load_args->scene;
  tb_auto path = load_args->scene_path;
  tb_auto loaded_task = load_args->loaded_task;

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

  TbSceneLoadedArgs loaded_args = {
      .ecs = ecs,
      .data = data,
      .scene_ent = scene,
      .path = path,
  };
  tb_launch_pinned_task_args(enki, loaded_task, &loaded_args,
                             sizeof(TbSceneLoadedArgs));

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

  // This pinned task will be launched by the loading task
  TbPinnedTask loaded_task =
      tb_create_pinned_task(enki, tb_scene_loaded, NULL, 0);

  // Launch a task to open the scene, parse it, and load relevant children
  TbLoadSceneArgs args = {
      .ecs = ecs,
      .enki = enki,
      .scene = scene,
      .scene_path = path_cpy,
      .loaded_task = loaded_task,
  };
  TbTask load_task =
      tb_async_task(enki, tb_load_scene_task, &args, sizeof(TbLoadSceneArgs));
  (void)load_task;

  return scene;
}
