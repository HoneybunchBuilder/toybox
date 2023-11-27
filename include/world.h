#pragma once

#include "allocator.h"
#include "dynarray.h"
#include "scene.h"

#include "blocks/Block.h"

#include <SDL2/SDL_stdinc.h>

typedef struct ecs_world_t ecs_world_t;

static const uint32_t InvalidEntityId = 0xFFFF;
static const uint32_t InvalidComponentId = 0xFFFF;

typedef struct TbWorld {
  ecs_world_t *ecs;
  TbAllocator std_alloc;
  TbAllocator tmp_alloc;
  TB_DYN_ARR_OF(TbScene) scenes;
} TbWorld;

typedef struct TbRenderThread TbRenderThread;
typedef struct SDL_Window SDL_Window;

typedef void (^TbCreateWorldSystemsFn)(TbWorld *, TbRenderThread *,
                                       SDL_Window *);

extern TbCreateWorldSystemsFn tb_create_default_world;

TbWorld tb_create_world(TbAllocator std_alloc, TbAllocator tmp_alloc,
                        TbCreateWorldSystemsFn create_fn,
                        TbRenderThread *render_thread, SDL_Window *window);
bool tb_tick_world(TbWorld *world, float delta_seconds);
void tb_clear_world(TbWorld *world);
void tb_destroy_world(TbWorld *world);

bool tb_load_scene(TbWorld *world, const char *scene_path);
void tb_unload_scene(TbWorld *world, TbScene *scene);
