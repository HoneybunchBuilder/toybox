#pragma once

#include "allocator.h"
#include "world.h"

#define InputSystemId 0x8BADF00D

typedef struct SDL_Window SDL_Window;
typedef struct _SDL_GameController SDL_GameController;

typedef struct InputSystemDescriptor {
  Allocator tmp_alloc;
  SDL_Window *window;
} InputSystemDescriptor;

#define TB_MAX_GAME_CONTROLLERS 4

typedef struct InputSystem {
  Allocator tmp_alloc;
  SDL_Window *window;

  SDL_GameController *controllers[TB_MAX_GAME_CONTROLLERS];
} InputSystem;

void tb_input_system_descriptor(SystemDescriptor *desc,
                                const InputSystemDescriptor *input_desc);
