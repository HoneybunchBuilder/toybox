#include "inputsystem.h"

#include "profiling.h"
#include "tbcommon.h"
#include "tbsdl.h"

#include <flecs.h>

// Get axis from an SDL controller in a 0 to 1 range
float get_axis_float(SDL_GameController *controller,
                     SDL_GameControllerAxis axis) {
  float raw_axis = (float)SDL_GameControllerGetAxis(controller, axis);
  return raw_axis / (float)SDL_MAX_SINT16;
}

void input_update_tick(ecs_iter_t *it) {
  InputSystem *self = ecs_field(it, InputSystem, 1);
  TracyCZoneN(ctx, "Input System Tick", true);
  TracyCZoneColor(ctx, TracyCategoryColorInput);

  self->mouse.axis = (float2){0}; // Must always clear axes
  self->mouse.wheel = (float2){0};
  for (uint32_t i = 0; i < TB_MAX_GAME_CONTROLLERS; ++i) {
    self->controller_states[i] = (TBGameControllerState){0};
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
    SDL_Event event = self->events[event_idx];
    // Translate keyboard events into input events that we care about
    {
      if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
        bool value = false;
        if (event.type == SDL_KEYDOWN) {
          value = true;
        }

        const SDL_Keysym *keysym = &event.key.keysym;
        SDL_Scancode scancode = keysym->scancode;

        if (scancode == SDL_SCANCODE_W) {
          self->keyboard.key_W = value;
        } else if (scancode == SDL_SCANCODE_A) {
          self->keyboard.key_A = value;
        } else if (scancode == SDL_SCANCODE_S) {
          self->keyboard.key_S = value;
        } else if (scancode == SDL_SCANCODE_D) {
          self->keyboard.key_D = value;
        }
      }

      if (event.type == SDL_MOUSEMOTION) {
        const SDL_MouseMotionEvent *mouse_motion = &event.motion;
        self->mouse.axis = (float2){
            (float)mouse_motion->xrel / 5,
            (float)mouse_motion->yrel / 5,
        };
      }
      if (event.type == SDL_MOUSEWHEEL) {

        if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
          self->mouse.wheel =
              (float2){event.wheel.preciseX, event.wheel.preciseY};
        } else {
          self->mouse.wheel =
              (float2){-event.wheel.preciseX, -event.wheel.preciseY};
        }
      }
      if (event.type == SDL_MOUSEBUTTONDOWN ||
          event.type == SDL_MOUSEBUTTONUP) {
        bool value = false;
        if (event.type == SDL_MOUSEBUTTONDOWN) {
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
      if (event.type == SDL_CONTROLLERDEVICEADDED) {
        if (self->controllers[event.cdevice.which] == NULL) {
          SDL_GameController *controller =
              SDL_GameControllerOpen(event.cdevice.which);
          self->controllers[event.cdevice.which] = controller;
          self->controller_count++;
        }
      }

      if (event.type == SDL_CONTROLLERDEVICEREMOVED) {
        SDL_GameController *controller = self->controllers[event.cdevice.which];
        if (controller != NULL) {
          SDL_GameControllerClose(controller);
          self->controllers[event.cdevice.which] = NULL;
          self->controller_count--;
        }
      }
    }
  }

  // Write that input component to the output input component(s)
  // There should only be one but if there is more than one just write to them
  // and issue a warning that this isn't intended. Maybe in the future it
  // could be a neat feature to be able to filter input based on player at
  // this level?

  // Query game controller state and apply it to the component
  for (uint32_t ctl_idx = 0; ctl_idx < TB_MAX_GAME_CONTROLLERS; ++ctl_idx) {
    SDL_GameController *controller = self->controllers[ctl_idx];

    if (controller == NULL) {
      continue;
    }

    TBGameControllerState *ctl_state = &self->controller_states[ctl_idx];
    ctl_state->left_stick = (float2){
        get_axis_float(controller, SDL_CONTROLLER_AXIS_LEFTX),
        get_axis_float(controller, SDL_CONTROLLER_AXIS_LEFTY),
    };
    ctl_state->left_trigger =
        get_axis_float(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
    ctl_state->right_stick = (float2){
        get_axis_float(controller, SDL_CONTROLLER_AXIS_RIGHTX),
        get_axis_float(controller, SDL_CONTROLLER_AXIS_RIGHTY),
    };
    ctl_state->right_trigger =
        get_axis_float(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
  }

  TracyCZoneEnd(ctx);
}

void tb_register_input_sys(ecs_world_t *ecs, Allocator tmp_alloc,
                           SDL_Window *window) {
  ECS_COMPONENT(ecs, InputSystem);
  ecs_singleton_set(ecs, InputSystem,
                    {
                        .tmp_alloc = tmp_alloc,
                        .window = window,
                    });
  ECS_SYSTEM(ecs, input_update_tick, EcsPreUpdate, InputSystem(InputSystem));
}
