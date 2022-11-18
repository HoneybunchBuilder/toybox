#include "inputsystem.h"

#include "inputcomponent.h"
#include "profiling.h"
#include "tbcommon.h"
#include "tbsdl.h"

bool create_input_system(InputSystem *self, const InputSystemDescriptor *desc,
                         uint32_t system_dep_count,
                         System *const *system_deps) {
  (void)system_dep_count;
  (void)system_deps;
  TB_CHECK_RETURN(desc, "Invalid descriptor", false);

  *self = (InputSystem){
      .tmp_alloc = desc->tmp_alloc,
      .window = desc->window,
  };
  return true;
}

void destroy_input_system(InputSystem *self) { *self = (InputSystem){0}; }

// Get axis from an SDL controller in a 0 to 1 range
float get_axis_float(SDL_GameController *controller,
                     SDL_GameControllerAxis axis) {
  float raw_axis = (float)SDL_GameControllerGetAxis(controller, axis);
  return raw_axis / (float)SDL_MAX_SINT16;
}

void tick_input_system(InputSystem *self, const SystemInput *input,
                       SystemOutput *output, float delta_seconds) {
  (void)input;  // We have no input
  (void)output; // Results of this system output to the system itself rather
                // than an output column
  (void)delta_seconds;
  TracyCZoneN(tick_ctx, "Input System Tick", true);
  TracyCZoneColor(tick_ctx, TracyCategoryColorInput);

  // Get which entities we need to write out to
  EntityId *entities = NULL;
  uint32_t entity_count = 0;
  const InputComponent *incoming_comp = NULL;
  for (uint32_t dep_idx = 0; dep_idx < input->dep_set_count; ++dep_idx) {
    const SystemDependencySet *set = &input->dep_sets[dep_idx];

    for (uint32_t col_idx = 0; col_idx < set->column_count; ++col_idx) {
      const PackedComponentStore *column = &set->columns[col_idx];
      if (column->id == InputComponentId) {
        entities = set->entity_ids;
        entity_count = set->entity_count;
        incoming_comp = tb_get_component(column, 0, InputComponent);
        break;
      }
    }
  }

  // Make a copy of the component we want to update

  InputComponent input_comp = *incoming_comp;
  input_comp.mouse.axis = (float2){0}; // Must always clear mouse axis

  // Read up-to InputSystemMaxEvents events from SDL and store them
  // in a buffer
  uint32_t event_index = 0;
  // Note that we must check the event index first or else we risk overrunning
  // the buffer. mimalloc should catch these cases.
  while (event_index < InputComponentMaxEvents &&
         SDL_PollEvent(&input_comp.events[event_index])) {
    event_index++;
  }
  input_comp.event_count = event_index;

  for (uint32_t event_idx = 0; event_idx < input_comp.event_count;
       ++event_idx) {
    SDL_Event event = input_comp.events[event_idx];
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
          input_comp.keyboard.key_W = value;
        } else if (scancode == SDL_SCANCODE_A) {
          input_comp.keyboard.key_A = value;
        } else if (scancode == SDL_SCANCODE_S) {
          input_comp.keyboard.key_S = value;
        } else if (scancode == SDL_SCANCODE_D) {
          input_comp.keyboard.key_D = value;
        }
      }

      if (event.type == SDL_MOUSEMOTION) {
        const SDL_MouseMotionEvent *mouse_motion = &event.motion;
        input_comp.mouse.axis = (float2){
            (float)mouse_motion->xrel / 5,
            (float)mouse_motion->yrel / 5,
        };
      }
      if (event.type == SDL_MOUSEBUTTONDOWN ||
          event.type == SDL_MOUSEBUTTONUP) {
        bool value = false;
        if (event.type == SDL_MOUSEBUTTONDOWN) {
          value = true;
        }
        if (event.button.button == 1) {
          input_comp.mouse.left = value;
        }
        if (event.button.button == 3) {
          input_comp.mouse.right = value;
        }
        if (event.button.button == 2) {
          input_comp.mouse.middle = value;
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
        }
      }

      if (event.type == SDL_CONTROLLERDEVICEREMOVED) {
        SDL_GameController *controller = self->controllers[event.cdevice.which];
        if (controller != NULL) {
          SDL_GameControllerClose(controller);
          self->controllers[event.cdevice.which] = NULL;
        }
      }
    }
  }

  // Write that input component to the output input component(s)
  // There should only be one but if there is more than one just write to them
  // and issue a warning that this isn't intended. Maybe in the future it
  // could be a neat feature to be able to filter input based on player at
  // this level?
  const uint32_t out_count = entity_count;
  if (out_count > 0) {
    InputComponent *components =
        tb_alloc_nm_tp(self->tmp_alloc, out_count, InputComponent);

    // Query game controller state and apply it to the component
    for (uint32_t ctl_idx = 0; ctl_idx < TB_MAX_GAME_CONTROLLERS; ++ctl_idx) {
      SDL_GameController *controller = self->controllers[ctl_idx];

      if (controller == NULL) {
        continue;
      }

      TBGameControllerState *ctl_state = &input_comp.controller_states[ctl_idx];
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

    for (uint32_t i = 0; i < out_count; ++i) {
      components[i] = input_comp;
    }

    output->set_count = 1;
    output->write_sets[0] = (SystemWriteSet){
        .id = InputComponentId,
        .count = out_count,
        .components = (uint8_t *)components,
        .entities = entities,
    };
  }

  TracyCZoneEnd(tick_ctx);
}

TB_DEFINE_SYSTEM(input, InputSystem, InputSystemDescriptor)

void tb_input_system_descriptor(SystemDescriptor *desc,
                                const InputSystemDescriptor *input_desc) {
  desc->name = "Input";
  desc->size = sizeof(InputSystem);
  desc->id = InputSystemId;
  desc->desc = (InternalDescriptor)input_desc;
  SDL_memset(desc->deps, 0,
             sizeof(SystemComponentDependencies) * MAX_DEPENDENCY_SET_COUNT);
  desc->dep_count = 1;
  desc->deps[0] = (SystemComponentDependencies){
      .count = 1,
      .dependent_ids = {InputComponentId},
  };
  desc->create = tb_create_input_system;
  desc->destroy = tb_destroy_input_system;
  desc->tick = tb_tick_input_system;
}
