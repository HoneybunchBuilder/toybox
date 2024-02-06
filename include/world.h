#pragma once

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

typedef void (^TbCreateWorldSystemsFn)(TbWorld *, TbRenderThread *,
                                       SDL_Window *);
extern TbCreateWorldSystemsFn tb_create_default_world;

typedef void (^TbCreateSystemFn)(TbWorld *, SDL_Window *);
typedef void (^TbDestroySystemFn)(TbWorld *);
extern void tb_register_system(const char *name, TbCreateSystemFn create_fn,
                               TbDestroySystemFn destroy_fn);

typedef struct TbWorldDesc {
  const char *name;
  int32_t argc;
  char **argv;
  SDL_Window *window;
  TbAllocator gp_alloc;
  TbAllocator tmp_alloc;
  TbCreateWorldSystemsFn create_fn;
} TbWorldDesc;

typedef struct TbWorld {
  ecs_world_t *ecs;
  TbAllocator gp_alloc;
  TbAllocator tmp_alloc;
  TbRenderThread *render_thread;
  TB_DYN_ARR_OF(TbScene) scenes;
} TbWorld;

TbWorld tb_create_world(const TbWorldDesc *desc);
bool tb_tick_world(TbWorld *world, float delta_seconds);
void tb_clear_world(TbWorld *world);
void tb_destroy_world(TbWorld *world);

bool tb_load_scene(TbWorld *world, const char *scene_path);
void tb_unload_scene(TbWorld *world, TbScene *scene);
