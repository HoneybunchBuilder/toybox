#pragma once

#include "allocator.h"
#include "tbsdl.h"
#include "world.h"

#define InputSystemId 0x8BADF00D

typedef struct SDL_Window SDL_Window;

typedef struct InputSystemDescriptor {
  SDL_Window *window;
} InputSystemDescriptor;

#define InputSystemMaxEvents 5

typedef struct InputSystem {
  SDL_Window *window;
  uint32_t event_count;
  SDL_Event events[InputSystemMaxEvents];
} InputSystem;

bool tb_input_system_get_events(const InputSystem *system,
                                uint32_t *event_count, SDL_Event *events);

void tb_input_system_descriptor(SystemDescriptor *desc,
                                const InputSystemDescriptor *input_desc);
