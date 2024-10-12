#include "tb_input_system.h"

#include "tb_common.h"
#include "tb_profiling.h"
#include "tb_sdl.h"

ECS_COMPONENT_DECLARE(TbInputSystem);

void tb_register_input_sys(TbWorld *world);
void tb_unregister_input_sys(TbWorld *world);

TB_REGISTER_SYS(tb, input, TB_INPUT_SYS_PRIO)

// Get axis from an SDL gamepad in a 0 to 1 range
float get_axis_float(SDL_Gamepad *gamepad, SDL_GamepadAxis axis) {
  float raw_axis = (float)SDL_GetGamepadAxis(gamepad, axis);
  return raw_axis / (float)SDL_MAX_SINT16;
}

float2 axis_deadzone(float2 axis, float deadzone) {
  return axis;
  float mag = tb_magf2(axis);
  if (mag < deadzone) {
    return tb_f2(0, 0);
  }
  return axis;
}

void input_update_tick(ecs_iter_t *it) {
  TB_TRACY_SCOPEC("Input System Tick", TracyCategoryColorInput);
  tb_auto self = ecs_field(it, TbInputSystem, 1);

  self->mouse.axis = (float2){0}; // Must always clear axes
  self->mouse.wheel = (float2){0};
  for (uint32_t i = 0; i < TB_MAX_GAME_CONTROLLERS; ++i) {
    self->gamepad_states[i] = (TbGameControllerState){0};
  }

  // Read up-to InputSystemMaxEvents events from SDL and store them
  // in a buffer
  uint32_t event_index = 0;
  // Note that we must check the event index first or else we risk overrunning
  // the buffer. mimalloc should catch these cases.
  while (event_index < TB_MAX_EVENTS &&
         SDL_PollEvent(&self->events[event_index])) {
    event_index++;
  }
  self->event_count = event_index;

  for (uint32_t event_idx = 0; event_idx < self->event_count; ++event_idx) {
    tb_auto event = self->events[event_idx];
    // Translate keyboard events into input events that we care about
    {
      if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
        bool value = event.type == SDL_EVENT_KEY_DOWN;
        tb_auto scancode = event.key.scancode;

        if (scancode == SDL_SCANCODE_W) {
          self->keyboard.key_W = value;
        } else if (scancode == SDL_SCANCODE_A) {
          self->keyboard.key_A = value;
        } else if (scancode == SDL_SCANCODE_S) {
          self->keyboard.key_S = value;
        } else if (scancode == SDL_SCANCODE_D) {
          self->keyboard.key_D = value;
        } else if (scancode == SDL_SCANCODE_SPACE) {
          self->keyboard.key_space = value;
        }
      }

      if (event.type == SDL_EVENT_MOUSE_MOTION) {
        tb_auto mouse_motion = &event.motion;
        self->mouse.axis = (float2){
            (float)mouse_motion->xrel / 5,
            (float)mouse_motion->yrel / 5,
        };
      }
      if (event.type == SDL_EVENT_MOUSE_WHEEL) {

        if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
          self->mouse.wheel = (float2){event.wheel.x, event.wheel.y};
        } else {
          self->mouse.wheel = (float2){-event.wheel.x, -event.wheel.y};
        }
      }
      if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
          event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        bool value = false;
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
          value = true;
        }
        if (event.button.button == 1) {
          self->mouse.left = value;
        }
        if (event.button.button == 3) {
          self->mouse.right = value;
        }
        if (event.button.button == 2) {
          self->mouse.middle = value;
        }
      }
    }

    // Controller events
    {
      if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
        if (self->gamepad[event.gdevice.which] == NULL) {
          SDL_Gamepad *gamepad = SDL_OpenGamepad(event.gdevice.which);
          int32_t player_idx = SDL_GetGamepadPlayerIndex(gamepad);
          self->gamepad[player_idx] = gamepad;
          self->gamepad_count++;
        }
      }

      if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
        SDL_Gamepad *gamepad = self->gamepad[event.gdevice.which];
        if (gamepad != NULL) {
          int32_t player_idx = SDL_GetGamepadPlayerIndex(gamepad);
          SDL_CloseGamepad(gamepad);
          self->gamepad[player_idx] = NULL;
          self->gamepad_count--;
        }
      }
    }
  }

  // Write that input component to the output input component(s)
  // There should only be one but if there is more than one just write to them
  // and issue a warning that this isn't intended. Maybe in the future it
  // could be a neat feature to be able to filter input based on player at
  // this level?

  // Query game gamepad state and apply it to the component
  for (uint32_t ctl_idx = 0; ctl_idx < TB_MAX_GAME_CONTROLLERS; ++ctl_idx) {
    SDL_Gamepad *gamepad = self->gamepad[ctl_idx];

    if (gamepad == NULL) {
      continue;
    }

    float deadzone = 0.075f; // TODO: Make configurable?

    TbGameControllerState *ctl_state = &self->gamepad_states[ctl_idx];
    ctl_state->left_stick =
        axis_deadzone(tb_f2(get_axis_float(gamepad, SDL_GAMEPAD_AXIS_LEFTX),
                            get_axis_float(gamepad, SDL_GAMEPAD_AXIS_LEFTY)),
                      deadzone);
    ctl_state->left_trigger =
        get_axis_float(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    ctl_state->right_stick =
        axis_deadzone(tb_f2(get_axis_float(gamepad, SDL_GAMEPAD_AXIS_RIGHTX),
                            get_axis_float(gamepad, SDL_GAMEPAD_AXIS_RIGHTY)),
                      deadzone);
    ctl_state->right_trigger =
        get_axis_float(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);

    ctl_state->buttons = 0;
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_SOUTH)) {
      ctl_state->buttons |= TB_BUTTON_A;
    }
    if (SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_EAST)) {
      ctl_state->buttons |= TB_BUTTON_B;
    }
  }
}

void tb_register_input_sys(TbWorld *world) {
  TB_TRACY_SCOPE("Register Input Sys");
  ecs_world_t *ecs = world->ecs;
  ECS_COMPONENT_DEFINE(ecs, TbInputSystem);

  ecs_singleton_set(ecs, TbInputSystem,
                    {
                        .tmp_alloc = world->tmp_alloc,
                        .window = world->window,
                    });
  ECS_SYSTEM(ecs, input_update_tick, EcsOnLoad, TbInputSystem($));
}

void tb_unregister_input_sys(TbWorld *world) {
  ecs_singleton_remove(world->ecs, TbInputSystem);
}
