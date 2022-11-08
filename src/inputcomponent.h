#pragma once

#include "simd.h"
#include "tbsdl.h"

#define InputComponentId 0xF00DBABE

typedef struct ComponentDescriptor ComponentDescriptor;

#define InputComponentMaxEvents 5

typedef enum TBButtonBits {
  TB_BUTTON_A = 0x00000001,
  TB_BUTTON_B = 0x00000002,
  TB_BUTTON_X = 0x00000004,
  TB_BUTTON_Y = 0x00000008,

  TB_BUTTON_UP = 0x00000010,
  TB_BUTTON_DOWN = 0x00000020,
  TB_BUTTON_LEFT = 0x00000040,
  TB_BUTTON_RIGHT = 0x00000080,

  // Shoulder buttons
  TB_BUTTON_L1 = 0x00000100,
  TB_BUTTON_R1 = 0x00000200,
  // Clicking sticks in
  TB_BUTTON_L3 = 0x00000400,
  TB_BUTTON_R3 = 0x00000800,

  TB_BUTTON_START = 0x00001000,
  TB_BUTTON_BACK = 0x00002000,
  TB_BUTTON_GUIDE = 0x00004000,
  TB_BUTTON_MISC = 0x00008000,

  TB_BUTTON_PADDLE1 = 0x00010000,
  TB_BUTTON_PADDLE2 = 0x00020000,
  TB_BUTTON_PADDLE3 = 0x00040000,
  TB_BUTTON_PADDLE4 = 0x00080000,
} TBButtonBits;
typedef uint32_t TBButtons;

typedef struct TBGameControllerState {
  float2 left_stick;
  float2 right_stick;
  TBButtons buttons;
  float left_trigger;
  float right_trigger;
} TBGameControllerState;

typedef struct InputComponent {
  uint32_t event_count;
  SDL_Event events[InputComponentMaxEvents];

  uint32_t controller_count;
  TBGameControllerState *controller_states;
} InputComponent;

void tb_input_component_descriptor(ComponentDescriptor *desc);
