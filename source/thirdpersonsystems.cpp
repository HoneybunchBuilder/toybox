#include "thirdpersonsystems.h"

#include "cameracomponent.h"
#include "inputsystem.h"
#include "profiling.h"
#include "rigidbodycomponent.h"
#include "thirdpersoncomponents.h"
#include "transformcomponent.h"
#include "world.h"

#include <flecs.h>

void update_tp_movement(flecs::world &ecs, const InputSystem &input,
                        float delta_time, ThirdPersonMovementComponent &move,
                        TransformComponent &trans) {
  float delta_speed = move.speed * delta_time;

  float3 velocity = {};

  if (input.keyboard.key_W) {
    velocity += TB_FORWARD * delta_speed;
  }

  if (magsqf3(velocity) != 0) {
    trans.transform.position += velocity;
    tb_transform_mark_dirty(ecs.c_ptr(), &trans);
  }
}

void tp_movement_update_tick(flecs::iter &it,
                             ThirdPersonMovementComponent *move,
                             TransformComponent *trans) {
  auto ecs = it.world();
  const auto &input_sys = *ecs.singleton<InputSystem>().get<InputSystem>();
  for (auto i : it) {
    update_tp_movement(ecs, input_sys, it.delta_time(), move[i], trans[i]);
  }
}

void tp_camera_update_tick(flecs::iter &it, ThirdPersonCameraComponent *control,
                           TransformComponent *trans,
                           const CameraComponent *cam) {
  (void)cam; // Don't actually need camera, just wanted to match with it
  for (auto i : it) {
    auto &controller = control[i];
    auto &transform = trans[i];
  }
}

void tb_register_third_person_systems(TbWorld *world) {
  flecs::world ecs(world->ecs);

  ecs.system<ThirdPersonMovementComponent, TransformComponent>(
         "ThirdPersonMovementSystem")
      .kind(EcsPreUpdate)
      .iter(tp_movement_update_tick);

  ecs.system<ThirdPersonCameraComponent, TransformComponent,
             const CameraComponent>("ThirdPersonCameraSystem")
      .kind(EcsPreUpdate)
      .iter(tp_camera_update_tick);

  tb_register_third_person_components(world);
}
