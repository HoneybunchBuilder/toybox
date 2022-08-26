#include "inputsystem.h"

#include "profiling.h"
#include "tbsdl.h"

bool create_input_system(InputSystem *self, const InputSystemDescriptor *desc) {
  if (!desc) {
    return false;
  }

  *self = (InputSystem){
      .window = desc->window,
  };
  return true;
}

void destroy_input_system(InputSystem *self) { *self = (InputSystem){0}; }

void tick_input_system(InputSystem *self, SystemDependencyColumns *columns,
                       SystemOutput *output, float delta_seconds) {
  (void)columns; // We have no dependencies here
  (void)output;  // Results of this system output to the system itself rather
                 // than an output column
  (void)delta_seconds;
  TracyCZoneN(tick_ctx, "Input System Tick", true);
  TracyCZoneColor(tick_ctx, TracyCategoryColorInput);

  SDL_LogInfo(SDL_LOG_CATEGORY_INPUT, "Ticking Input System");

  // Clear out previous frame's inputs
  SDL_memset(self->events, 0, sizeof(SDL_Event) * InputSystemMaxEvents);

  // Read up-to InputSystemMaxEvents events from SDL and store them
  // in a buffer
  uint32_t event_index = 0;
  // Note that we must check the event index first or else we risk overrunning
  // the buffer. mimalloc should catch these cases.
  while (event_index < InputSystemMaxEvents &&
         SDL_PollEvent(&self->events[event_index])) {
    SDL_LogInfo(SDL_LOG_CATEGORY_INPUT, "Polled event %d", event_index);
    event_index++;
  }
  self->event_count = event_index;

  TracyCZoneEnd(tick_ctx);
}

TB_DEFINE_SYSTEM(input, InputSystem, InputSystemDescriptor)

bool tb_input_system_get_events(const InputSystem *system,
                                uint32_t *event_count, SDL_Event *events) {
  if (event_count) {
    if (!events) {
      *event_count = system->event_count;
      return true;
    } else {
      // Ensure we can't copy more than the max number of events
      if ((*event_count) > InputSystemMaxEvents) {
        *event_count = InputSystemMaxEvents;
      }
      SDL_memcpy(events, system->events, (*event_count) * sizeof(SDL_Event));
      return true;
    }
  }
  return false;
}

void tb_input_system_descriptor(SystemDescriptor *desc,
                                const InputSystemDescriptor *input_desc) {
  desc->name = "Input";
  desc->size = sizeof(InputSystem);
  desc->id = InputSystemId;
  desc->desc = (InternalDescriptor)input_desc;
  desc->deps = (SystemComponentDependencies){0, {0}};
  desc->create = tb_create_input_system;
  desc->destroy = tb_destroy_input_system;
  desc->tick = tb_tick_input_system;
}
