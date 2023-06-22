#include "rotatorsystem.h"

#include "transformcomponent.h"
#include "transformercomponents.h"
#include <SDL2/SDL_stdinc.h>

bool create_rotator_system(RotatorSystem *self,
                           const RotatorSystemDescriptor *desc,
                           uint32_t system_dep_count,
                           System *const *system_deps) {
  // This system has no dependencies
  // and only needs a temporary allocator
  (void)desc;
  (void)system_dep_count;
  (void)system_deps;

  *self = (RotatorSystem){
      .tmp_alloc = desc->tmp_alloc,
  };

  return true;
}

void destroy_rotator_system(RotatorSystem *self) { *self = (RotatorSystem){0}; }

void tick_rotator_system(RotatorSystem *self, const SystemInput *input,
                         SystemOutput *output, float delta_seconds) {
  EntityId *entities = tb_get_column_entity_ids(input, 0);
  const uint32_t entity_count = tb_get_column_component_count(input, 0);
  const PackedComponentStore *rotators =
      tb_get_column_check_id(input, 0, 0, RotatorComponentId);
  const PackedComponentStore *transforms =
      tb_get_column_check_id(input, 0, 1, TransformComponentId);

  if (rotators == NULL || transforms == NULL) {
    return;
  }

  const RotatorComponent *rotator_comps =
      (const RotatorComponent *)rotators->components;
  const TransformComponent *transform_comps =
      (const TransformComponent *)transforms->components;

  // Make a copy of the transform input as the output
  TransformComponent *out_trans =
      tb_alloc_nm_tp(self->tmp_alloc, entity_count, TransformComponent);
  SDL_memcpy(out_trans, transform_comps,
             entity_count * sizeof(TransformComponent));

  for (uint32_t i = 0; i < entity_count; ++i) {
    const RotatorComponent *rotator = &rotator_comps[i];
    TransformComponent *trans = &out_trans[i];

    Quaternion rot = angle_axis_to_quat(
        f3tof4(rotator->axis, rotator->speed * delta_seconds));

    trans->transform.rotation = mulq(trans->transform.rotation, rot);
  }

  // Report output
  output->set_count = 1;
  output->write_sets[0] = (SystemWriteSet){
      .id = TransformComponentId,
      .count = entity_count,
      .components = (uint8_t *)out_trans,
      .entities = entities,
  };
}

TB_DEFINE_SYSTEM(rotator, RotatorSystem, RotatorSystemDescriptor)

void tb_rotator_system_descriptor(SystemDescriptor *desc,
                                  const RotatorSystemDescriptor *rotator_desc) {
  *desc = (SystemDescriptor){
      .name = "Rotator",
      .size = sizeof(RotatorSystem),
      .id = RotatorSystemId,
      .desc = (InternalDescriptor)rotator_desc,
      .dep_count = 1,
      .deps[0] =
          {
              .count = 2,
              .dependent_ids = {RotatorComponentId, TransformComponentId},
          },
      .system_dep_count = 0,
      .create = tb_create_rotator_system,
      .destroy = tb_destroy_rotator_system,
      .tick = tb_tick_rotator_system,
  };
}
