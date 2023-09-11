#pragma once

#include <SDL2/SDL_stdinc.h>

#include "dynarray.h"

typedef uint64_t entity_id_t;

typedef struct TbScene {
  TB_DYN_ARR_OF(entity_id_t) entities;
} TbScene;
