#include "rotatorsystem.h"

#include "profiling.h"
#include "transformcomponent.h"
#include "transformercomponents.h"
#include "world.h"

#include <SDL2/SDL_stdinc.h>

#include <flecs.h>

void rotator_system_tick(ecs_iter_t *it) {
  TracyCZoneNC(ctx, "Rotator System", TracyCategoryColorCore, true);
  TbRotatorComponent *rotators = ecs_field(it, TbRotatorComponent, 1);
  TbTransformComponent *transforms = ecs_field(it, TbTransformComponent, 2);

  for (int32_t i = 0; i < it->count; ++i) {
    TbRotatorComponent *rotator = &rotators[i];
    TbTransformComponent *trans = &transforms[i];

    TbQuaternion rot = tb_angle_axis_to_quat(
        tb_f3tof4(rotator->axis, rotator->speed * it->delta_time));

    trans->transform.rotation = tb_mulq(trans->transform.rotation, rot);
    tb_transform_mark_dirty(it->world, trans);
  }
  TracyCZoneEnd(ctx);
}

void tb_register_rotator_sys(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;
  ECS_COMPONENT(ecs, TbRotatorComponent);
  ECS_COMPONENT(ecs, TbTransformComponent);

  ECS_SYSTEM(ecs, rotator_system_tick,
             EcsOnUpdate, [in] TbRotatorComponent, [out] TbTransformComponent);

  tb_register_rotator_component(world);
}

void tb_unregister_rotator_sys(TbWorld *world) { (void)world; }
