#pragma once

#include <stdint.h>

#define NoClipComponentId 0xC00010FF
#define NoClipComponentIdStr "0xC00010FF"

typedef struct ComponentDescriptor ComponentDescriptor;

typedef struct NoClipComponentDescriptor {
  float move_speed;
  float look_speed;
} NoClipComponentDescriptor;

typedef struct NoClipComponent {
  float move_speed;
  float look_speed;
} NoClipComponent;

void tb_noclip_component_descriptor(ComponentDescriptor *desc);
