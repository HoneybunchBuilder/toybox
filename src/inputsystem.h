#pragma once

#include "allocator.h"
#include "world.h"

#define InputSystemId 0x8BADF00D

typedef struct SDL_Window SDL_Window;

typedef struct InputSystemDescriptor {
  Allocator tmp_alloc;
  SDL_Window *window;
} InputSystemDescriptor;

typedef struct InputSystem {
  Allocator tmp_alloc;
  SDL_Window *window;
} InputSystem;

void tb_input_system_descriptor(SystemDescriptor *desc,
                                const InputSystemDescriptor *input_desc);
