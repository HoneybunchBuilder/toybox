#pragma once

#include "simd.h"
#include "tbsdl.h"

#define InputComponentId 0xF00DBABE

typedef struct ComponentDescriptor ComponentDescriptor;

#define TbMaxControllers 4
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

typedef struct TBKeyboard {
  uint8_t key_A : 1;
  uint8_t key_B : 1;
  uint8_t key_C : 1;
  uint8_t key_D : 1;
  uint8_t key_E : 1;
  uint8_t key_F : 1;
  uint8_t key_G : 1;
  uint8_t key_H : 1;
  uint8_t key_I : 1;
  uint8_t key_J : 1;
  uint8_t key_K : 1;
  uint8_t key_L : 1;
  uint8_t key_M : 1;
  uint8_t key_N : 1;
  uint8_t key_O : 1;
  uint8_t key_P : 1;
  uint8_t key_Q : 1;
  uint8_t key_R : 1;
  uint8_t key_S : 1;
  uint8_t key_T : 1;
  uint8_t key_U : 1;
  uint8_t key_V : 1;
  uint8_t key_W : 1;
  uint8_t key_X : 1;
  uint8_t key_Y : 1;
  uint8_t key_Z : 1;
} TBKeyboard;

typedef struct TBMouse {
  uint8_t left : 1;
  uint8_t middle : 1;
  uint8_t right : 1;
  float2 axis;
} TBMouse;

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

  TBKeyboard keyboard;
  TBMouse mouse;

  uint32_t controller_count;
  TBGameControllerState controller_states[TbMaxControllers];
} InputComponent;

void tb_input_component_descriptor(ComponentDescriptor *desc);