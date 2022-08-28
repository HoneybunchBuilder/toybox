#include "inputsystem.h"

#include "inputcomponent.h"
#include "profiling.h"
#include "tbsdl.h"

bool create_input_system(InputSystem *self, const InputSystemDescriptor *desc) {
  if (!desc) {
    return false;
  }

  *self = (InputSystem){
      .tmp_alloc = desc->tmp_alloc,
      .window = desc->window,
  };
  return true;
}

void destroy_input_system(InputSystem *self) { *self = (InputSystem){0}; }

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
  for (uint32_t dep_idx = 0; dep_idx < input->dep_set_count; ++dep_idx) {
    const SystemDependencySet *set = &input->dep_sets[dep_idx];

    for (uint32_t col_idx = 0; col_idx < set->column_count; ++col_idx) {
      const PackedComponentStore *column = &set->columns[col_idx];
      if (column->id == InputComponentId) {
        entities = set->entity_ids;
        entity_count = set->entity_count;
        break;
      }
    }
  }

  // Gather all input into a single input component
  InputComponent input_comp = (InputComponent){0};

  // Read up-to InputSystemMaxEvents events from SDL and store them
  // in a buffer
  uint32_t event_index = 0;
  // Note that we must check the event index first or else we risk overrunning
  // the buffer. mimalloc should catch these cases.
  while (event_index < InputComponentMaxEvents &&
         SDL_PollEvent(&input_comp.events[event_index])) {
    SDL_LogInfo(SDL_LOG_CATEGORY_INPUT, "Polled event %d", event_index);
    event_index++;
  }
  input_comp.event_count = event_index;

  // Write that input component to the output input component(s)
  // There should only be one but if there is more than one just write to them
  // and issue a warning that this isn't intended. Maybe in the future it could
  // be a neat feature to be able to filter input based on player at this level?
  const uint32_t out_count = entity_count;
  if (out_count > 0) {
    InputComponent *components =
        tb_alloc_nm_tp(self->tmp_alloc, out_count, InputComponent);

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
             sizeof(SystemComponentDependencies) * MAX_DEPENDENCY_SET_COUT);
  desc->dep_count = 1;
  desc->deps[0] = (SystemComponentDependencies){
      .count = 1,
      .dependent_ids = {InputComponentId},
  };
  desc->create = tb_create_input_system;
  desc->destroy = tb_destroy_input_system;
  desc->tick = tb_tick_input_system;
}
