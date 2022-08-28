#pragma once

#include "tbsdl.h"

#define InputComponentId 0xF00DBABE

typedef struct ComponentDescriptor ComponentDescriptor;

#define InputComponentMaxEvents 5

typedef struct InputComponent {
  uint32_t event_count;
  SDL_Event events[InputComponentMaxEvents];
} InputComponent;

void tb_input_component_descriptor(ComponentDescriptor *desc);
