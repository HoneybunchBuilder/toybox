#include "rotatorsystem.h"

#include "transformcomponent.h"
#include "transformercomponents.h"

#include <SDL2/SDL_log.h>
#include <SDL2/SDL_stdinc.h>

#include <flecs.h>

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

TB_DEFINE_SYSTEM(rotator, RotatorSystem, RotatorSystemDescriptor)

void tick_rotator_system_internal(RotatorSystem *self, const SystemInput *input,
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

void tick_rotator_system(void *self, const SystemInput *input,
                         SystemOutput *output, float delta_seconds) {
  SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Tick Rotator System");
  tick_rotator_system_internal((RotatorSystem *)self, input, output,
                               delta_seconds);
}

void tb_rotator_system_descriptor(SystemDescriptor *desc,
                                  const RotatorSystemDescriptor *rotator_desc) {
  *desc = (SystemDescriptor){
      .name = "Rotator",
      .size = sizeof(RotatorSystem),
      .id = RotatorSystemId,
      .desc = (InternalDescriptor)rotator_desc,
      .system_dep_count = 0,
      .create = tb_create_rotator_system,
      .destroy = tb_destroy_rotator_system,
      .tick_fn_count = 1,
      .tick_fns[0] =
          {
              .dep_count = 1,
              .deps[0] = {2, {RotatorComponentId, TransformComponentId}},
              .system_id = RotatorSystemId,
              .order = E_TICK_PRE_PHYSICS,
              .function = tick_rotator_system,
          },
  };
}

void flecs_rotator_tick(ecs_iter_t *it) {
  SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Tick Rotator System");
  RotatorComponent *rotators = ecs_field(it, RotatorComponent, 1);
  TransformComponent *transforms = ecs_field(it, TransformComponent, 2);

  for (int32_t i = 0; i < it->count; ++i) {
    RotatorComponent *rotator = &rotators[i];
    TransformComponent *trans = &transforms[i];

    Quaternion rot = angle_axis_to_quat(
        f3tof4(rotator->axis, rotator->speed * it->delta_time));

    trans->transform.rotation = mulq(trans->transform.rotation, rot);
  }
}

void tb_register_rotator_sys(ecs_world_t *ecs, Allocator tmp_alloc) {
  ECS_COMPONENT(ecs, RotatorSystem);
  ECS_COMPONENT(ecs, RotatorComponent);
  ECS_COMPONENT(ecs, TransformComponent);

  RotatorSystem sys = {
      .tmp_alloc = tmp_alloc,
  };

  // Sets a singleton by ptr
  ecs_set_ptr(ecs, ecs_id(RotatorSystem), RotatorSystem, &sys);

  ECS_SYSTEM(ecs, flecs_rotator_tick,
             EcsOnUpdate, [in] RotatorComponent, [out] TransformComponent);
}

void tb_unregister_rotator_sys(ecs_world_t *ecs) {
  ECS_COMPONENT(ecs, RotatorSystem);
  RotatorSystem *sys = ecs_singleton_get_mut(ecs, RotatorSystem);
  destroy_rotator_system(sys);
}
