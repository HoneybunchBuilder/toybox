#pragma once

#include "simd.h"

#define NoClipComponentId 0xC00010FF

typedef struct ComponentDescriptor ComponentDescriptor;

enum NoClipStateFlags {
  NOCLIP_NONE = 0x000,

  NOCLIP_MOVING_FORWARD = 0x001,
  NOCLIP_MOVING_BACKWARD = 0x002,
  NOCLIP_MOVING_LEFT = 0x004,
  NOCLIP_MOVING_RIGHT = 0x008,
  NOCLIP_MOVING_UP = 0x010,
  NOCLIP_MOVING_DOWN = 0x020,
  NOCLIP_MOVING = NOCLIP_MOVING_FORWARD | NOCLIP_MOVING_BACKWARD |
                  NOCLIP_MOVING_LEFT | NOCLIP_MOVING_RIGHT | NOCLIP_MOVING_UP |
                  NOCLIP_MOVING_DOWN,

  NOCLIP_LOOKING_LEFT = 0x040,
  NOCLIP_LOOKING_RIGHT = 0x080,
  NOCLIP_LOOKING_UP = 0x100,
  NOCLIP_LOOKING_DOWN = 0x200,
  NOCLIP_LOOKING = NOCLIP_LOOKING_LEFT | NOCLIP_LOOKING_RIGHT |
                   NOCLIP_LOOKING_UP | NOCLIP_LOOKING_DOWN,
};
typedef uint32_t NoClipState;

typedef struct NoClipComponentDescriptor {
  float move_speed;
  float look_speed;
} NoClipComponentDescriptor;

typedef struct NoClipComponent {
  float move_speed;
  float look_speed;
  NoClipState state;
} NoClipComponent;

void tb_noclip_component_descriptor(ComponentDescriptor *desc);
