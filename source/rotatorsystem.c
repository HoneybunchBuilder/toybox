#include "rotatorsystem.h"

#include "profiling.h"
#include "transformcomponent.h"
#include "transformercomponents.h"

#include <SDL2/SDL_stdinc.h>

#include <flecs.h>

void rotator_system_tick(ecs_iter_t *it) {
  TracyCZoneNC(ctx, "Rotator System", TracyCategoryColorCore, true);
  RotatorComponent *rotators = ecs_field(it, RotatorComponent, 1);
  TransformComponent *transforms = ecs_field(it, TransformComponent, 2);

  for (int32_t i = 0; i < it->count; ++i) {
    RotatorComponent *rotator = &rotators[i];
    TransformComponent *trans = &transforms[i];

    Quaternion rot = angle_axis_to_quat(
        f3tof4(rotator->axis, rotator->speed * it->delta_time));

    trans->transform.rotation = mulq(trans->transform.rotation, rot);
  }
  TracyCZoneEnd(ctx);
}

void tb_register_rotator_sys(ecs_world_t *ecs, TbAllocator tmp_alloc) {
  ECS_COMPONENT(ecs, RotatorSystem);
  ECS_COMPONENT(ecs, RotatorComponent);
  ECS_COMPONENT(ecs, TransformComponent);

  RotatorSystem sys = {
      .tmp_alloc = tmp_alloc,
  };

  // Sets a singleton by ptr
  ecs_set_ptr(ecs, ecs_id(RotatorSystem), RotatorSystem, &sys);

  ECS_SYSTEM(ecs, rotator_system_tick,
             EcsOnUpdate, [in] RotatorComponent, [out] TransformComponent);

  tb_register_rotator_component(ecs);
}

void tb_unregister_rotator_sys(ecs_world_t *ecs) {
  ECS_COMPONENT(ecs, RotatorSystem);
  RotatorSystem *sys = ecs_singleton_get_mut(ecs, RotatorSystem);
  *sys = (RotatorSystem){0};
  ecs_singleton_remove(ecs, RotatorSystem);
}
