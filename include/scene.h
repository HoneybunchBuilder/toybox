#pragma once

#include <SDL2/SDL_stdinc.h>

#include "dynarray.h"

typedef uint64_t ecs_entity_t;

typedef struct TbScene {
  TB_DYN_ARR_OF(ecs_entity_t) entities;
} TbScene;
