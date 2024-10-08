#pragma once

#include "tb_allocator.h"
#include "tb_dynarray.h"
#include "tb_scene.h"

#include "blocks/Block.h"

#include <SDL3/SDL_stdinc.h>

#include <flecs.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TbWorld TbWorld;
typedef struct TbRenderThread TbRenderThread;
typedef struct SDL_Window SDL_Window;

static const uint32_t TbInvalidEntityId = 0;
static const uint32_t TbInvalidComponentId = 0;

typedef void (*TbCreateSystemFn)(TbWorld *);
typedef void (*TbDestroySystemFn)(TbWorld *);
void tb_register_system(const char *name, int32_t priority,
                        TbCreateSystemFn create_fn,
                        TbDestroySystemFn destroy_fn);
#define TB_REGISTER_SYS(namespace, name, priority)                             \
  __attribute__((                                                              \
      __constructor__)) void __##namespace##_construct_##name##_sys(void) {    \
    tb_register_system(#name, (priority), &namespace##_register_##name##_sys,  \
                       &namespace##_unregister_##name##_sys);                  \
  }

typedef struct json_object json_object;
typedef struct cgltf_data cgltf_data;
typedef struct cgltf_node cgltf_node;

typedef struct TbComponentRegisterResult {
  ecs_entity_t type_id;
  ecs_entity_t desc_id;
} TbComponentRegisterResult;

typedef TbComponentRegisterResult (*TbRegisterComponentFn)(TbWorld *);
typedef bool (*TbLoadComponentFn)(ecs_world_t *ecs, ecs_entity_t ent,
                                  const char *source_path,
                                  const cgltf_data *data,
                                  const cgltf_node *node, json_object *json);
typedef bool (*TbReadyComponentFn)(ecs_world_t *ecs, ecs_entity_t ent);
void tb_register_component(const char *name, TbRegisterComponentFn reg_fn,
                           TbLoadComponentFn load_fn,
                           TbReadyComponentFn ready_fn);
#define TB_REGISTER_COMP(namespace, name)                                      \
  __attribute__((                                                              \
      __constructor__)) void __##namespace##_register_##name##_comp(void) {    \
    tb_register_component(#name, &namespace##_register_##name##_comp,          \
                          &namespace##_load_##name##_comp,                     \
                          &namespace##_ready_##name##_comp);                   \
  }

typedef struct TbWorldDesc {
  const char *name;
  int32_t argc;
  char **argv;
  SDL_Window *window;
  TbAllocator gp_alloc;
  TbAllocator tmp_alloc;
} TbWorldDesc;

typedef struct TbWorld {
  ecs_world_t *ecs;
  double time;
  TbAllocator gp_alloc;
  TbAllocator tmp_alloc;
  TbRenderThread *render_thread;
  SDL_Window *window;
} TbWorld;

typedef struct TbWorldRef {
  TbWorld *world;
} TbWorldRef;
extern ECS_COMPONENT_DECLARE(TbWorldRef);

bool tb_create_world(const TbWorldDesc *desc, TbWorld *world);
bool tb_tick_world(TbWorld *world, float delta_seconds);
void tb_destroy_world(TbWorld *world);

TbScene tb_load_scene(TbWorld *world, const char *scene_path);
void tb_unload_scene(TbWorld *world, TbScene *scene);

// HACK: Get component load function by name for scene2
TbLoadComponentFn tb_get_component_load_fn(const char *name);
bool tb_enitity_components_ready(ecs_world_t *ecs, ecs_entity_t ent);

extern ECS_COMPONENT_DECLARE(float3);
extern ECS_COMPONENT_DECLARE(float4);
extern ECS_COMPONENT_DECLARE(float4x4);
extern ECS_COMPONENT_DECLARE(TbTransform);

#ifdef __cplusplus
}
#endif
