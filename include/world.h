#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "allocator.h"
#include "dynarray.h"
#include "scene.h"

#include "blocks/Block.h"

#include <SDL3/SDL_stdinc.h>

typedef struct ecs_world_t ecs_world_t;
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

// clang-format off
#define TB_REGISTER_SYS(namespace, name, priority)                             \
  __attribute__((__constructor__))                                             \
  void namespace##_construct_##name##_sys(void) {                              \
    tb_register_system(#name, (priority),                                      \
                       &namespace##_register_##name##_sys,                     \
                       &namespace##_unregister_##name##_sys);                  \
  }

// clang-format on

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

bool tb_create_world(const TbWorldDesc *desc, TbWorld *world);
bool tb_tick_world(TbWorld *world, float delta_seconds);
void tb_clear_world(TbWorld *world);
void tb_destroy_world(TbWorld *world);

bool tb_load_scene(TbWorld *world, const char *scene_path);
void tb_unload_scene(TbWorld *world, TbScene *scene);

#ifdef __cplusplus
}
#endif
