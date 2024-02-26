#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "allocator.h"
#include "dynarray.h"
#include "scene.h"

#include "blocks/Block.h"

#include <SDL3/SDL_stdinc.h>

#include <flecs.h>

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
typedef struct cgltf_node cgltf_node;

typedef ecs_entity_t (*TbRegisterComponentFn)(TbWorld *);
typedef bool (*TbLoadComponentFn)(TbWorld *world, ecs_entity_t ent,
                                  const char *source_path,
                                  const cgltf_node *node, json_object *json);
void tb_register_component(const char *name, TbRegisterComponentFn reg_fn,
                           TbLoadComponentFn load_fn);
#define TB_REGISTER_COMP(namespace, name)                                      \
  __attribute__((                                                              \
      __constructor__)) void __##namespace##_register_##name##_comp(void) {    \
    tb_register_component(#name, &namespace##_register_##name##_comp,          \
                          &namespace##_load_##name##_comp);                    \
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
  TbAllocator gp_alloc;
  TbAllocator tmp_alloc;
  TbRenderThread *render_thread;
  SDL_Window *window;
  TB_DYN_ARR_OF(TbScene) scenes;
} TbWorld;

typedef struct TbWorldRef {
  TbWorld *world;
} TbWorldRef;
extern ECS_COMPONENT_DECLARE(TbWorldRef);

bool tb_create_world(const TbWorldDesc *desc, TbWorld *world);
bool tb_tick_world(TbWorld *world, float delta_seconds);
void tb_clear_world(TbWorld *world);
void tb_destroy_world(TbWorld *world);

bool tb_load_scene(TbWorld *world, const char *scene_path);
void tb_unload_scene(TbWorld *world, TbScene *scene);

extern ECS_COMPONENT_DECLARE(float3);
extern ECS_COMPONENT_DECLARE(float4);
extern ECS_COMPONENT_DECLARE(float4x4);
extern ECS_COMPONENT_DECLARE(TbTransform);

#ifdef __cplusplus
}
#endif
