#include "tb_material_system.h"

#include "assets.h"
#include "tb_task_scheduler.h"
#include "tbcommon.h"
#include "tbgltf.h"
#include "tbqueue.h"
#include "world.h"

static const int32_t TbMaxParallelMaterialLoads = 24;

// Components

ECS_COMPONENT_DECLARE(TbMaterialUsage);

typedef SDL_AtomicInt TbMatQueueCounter;
ECS_COMPONENT_DECLARE(TbMatQueueCounter);

typedef struct TbGPUMaterial {
} TbGPUMaterial;
ECS_COMPONENT_DECLARE(TbGPUMaterial);

typedef struct TbMaterialUsageHandler {
  tb_mat_parse_fn *fn;
  size_t type_size;
} TbMaterialUsageHandler;
ECS_COMPONENT_DECLARE(TbMaterialUsageHandler);

typedef struct TbMaterialCtx {
  VkDescriptorSetLayout set_layout;
  TbFrameDescriptorPoolList frame_set_pool;

  ecs_query_t *loaded_mat_query;

  TbMaterial2 default_mat;
} TbMaterialCtx;
ECS_COMPONENT_DECLARE(TbMaterialCtx);

typedef uint32_t TbMaterialComponent;
ECS_COMPONENT_DECLARE(TbMaterialComponent);

// Describes the creation of a material that lives in a GLB file
typedef struct TbMaterialGLTFLoadRequest {
  const char *path;
  const char *name;
} TbMaterialGLTFLoadRequest;
ECS_COMPONENT_DECLARE(TbMaterialGLTFLoadRequest);

ECS_TAG_DECLARE(TbMaterialLoaded);

// Internals

// Systems

void tb_queue_gltf_mat_loads(ecs_iter_t *it) {}

void tb_reset_mat_queue_count(ecs_iter_t *it) {
  tb_auto counter = ecs_field(it, TbMatQueueCounter, 1);
  SDL_AtomicSet(counter, 0);
}

// Toybox Glue

void tb_register_material2_sys(TbWorld *world) {
  tb_auto ecs = world->ecs;
  ECS_COMPONENT_DEFINE(ecs, TbMaterialCtx);
  ECS_COMPONENT_DEFINE(ecs, TbMaterialGLTFLoadRequest);
  ECS_COMPONENT_DEFINE(ecs, TbMaterialComponent);
  ECS_COMPONENT_DEFINE(ecs, TbGPUMaterial);
  ECS_COMPONENT_DEFINE(ecs, TbMatQueueCounter);
  ECS_COMPONENT_DEFINE(ecs, TbMaterialUsageHandler);
  ECS_COMPONENT_DEFINE(ecs, TbMaterialUsage);
  ECS_TAG_DEFINE(ecs, TbMaterialLoaded);

  ECS_SYSTEM(
      ecs, tb_queue_gltf_mat_loads, EcsPreUpdate,
      TbTaskScheduler(TbTaskScheduler), TbRenderSystem(TbRenderSystem),
      TbMatQueueCounter(TbMatQueueCounter), [in] TbMaterialGLTFLoadRequest);
  ECS_SYSTEM(ecs, tb_reset_mat_queue_count,
             EcsPostUpdate, [in] TbTexQueueCounter(TbMatQueueCounter));

  TbMaterialCtx ctx = {
      .loaded_mat_query =
          ecs_query(ecs, {.filter.terms =
                              {
                                  {.id = ecs_id(TbGPUMaterial), .inout = EcsIn},
                                  {.id = ecs_id(TbMaterialLoaded)},
                              }}),
  };

  TbMatQueueCounter queue_count = {0};
  SDL_AtomicSet(&queue_count, 0);
  ecs_singleton_set_ptr(ecs, TbMatQueueCounter, &queue_count);

  // Must set ctx before we try to load any materials
  ecs_singleton_set_ptr(ecs, TbMaterialCtx, &ctx);

  // TODO: Register default material usage handlers

  // TODO: Load default materials

  ecs_singleton_set_ptr(ecs, TbMaterialCtx, &ctx);
}

void tb_unregister_material2_sys(TbWorld *world) {
  tb_auto ecs = world->ecs;
  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  tb_auto ctx = ecs_singleton_get_mut(ecs, TbMaterialCtx);

  ecs_query_fini(ctx->loaded_mat_query);

  tb_rnd_destroy_set_layout(rnd_sys, ctx->set_layout);

  // TODO: Release all default references

  // TODO: Check for leaks

  // TODO: Clean up descriptor pool

  ecs_singleton_remove(ecs, TbMaterialCtx);
}

TB_REGISTER_SYS(tb, material2, TB_MAT_SYS_PRIO)

// Public API

bool tb_register_mat_usage(ecs_world_t *ecs, TbMaterialUsage usage,
                           tb_mat_parse_fn parse_fn, void *default_data,
                           size_t size) {}

TbMaterial2 tb_mat_sys_load_gltf_mat(ecs_world_t *ecs, const char *path,
                                     const char *name, TbMaterialUsage usage) {
  // If an entity already exists with this name it is either loading or loaded
  TbTexture mat_ent = ecs_lookup(ecs, name);
  if (mat_ent != 0) {
    return mat_ent;
  }

  // Create a material entity
  mat_ent = ecs_new_entity(ecs, 0);
  ecs_set_name(ecs, mat_ent, name);

  // Need to copy strings for task safety
  // Tasks are responsible for freeing these names
  const size_t path_len = SDL_strnlen(path, 256) + 1;
  char *path_cpy = tb_alloc_nm_tp(tb_global_alloc, path_len, char);
  SDL_strlcpy(path_cpy, path, path_len);

  const size_t name_len = SDL_strnlen(name, 256) + 1;
  char *name_cpy = tb_alloc_nm_tp(tb_global_alloc, name_len, char);
  SDL_strlcpy(name_cpy, name, name_len);

  // It is a child of the texture system context singleton
  ecs_add_pair(ecs, mat_ent, EcsChildOf, ecs_id(TbMaterialCtx));

  // Append a texture load request onto the entity to schedule loading
  ecs_set(ecs, mat_ent, TbMaterialGLTFLoadRequest, {path_cpy, name_cpy});
  ecs_set(ecs, mat_ent, TbMaterialUsage, {usage});

  return mat_ent;
}

bool tb_is_material_ready(ecs_world_t *ecs, TbMaterial2 mat) {
  return ecs_has(ecs, mat, TbMaterialLoaded) &&
         ecs_has(ecs, mat, TbMaterialComponent);
}

TbTexture tb_get_default_mat(ecs_world_t *ecs, TbMaterialUsage usage) {
  tb_auto ctx = ecs_singleton_get_mut(ecs, TbMaterialCtx);
  // TODO: Determine default material by usage
  return ctx->default_mat;
}
