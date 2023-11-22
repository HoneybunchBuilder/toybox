#include "thirdpersonsystems.h"

#include "cameracomponent.h"
#include "inputsystem.h"
#include "physicssystem.hpp"
#include "profiling.h"
#include "rigidbodycomponent.h"
#include "thirdpersoncomponents.h"
#include "transformcomponent.h"
#include "world.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <flecs.h>

void update_tp_movement(flecs::world &ecs, const InputSystem &input,
                        ThirdPersonMovementComponent &move,
                        TransformComponent &trans) {
  const auto &phys_sys = ecs.get<TbPhysicsSystem>();
  auto &body_iface = phys_sys->jolt_phys->GetBodyInterface();
  const auto &rb = *ecs.entity(move.body).get<TbRigidbodyComponent>();

  const auto &transform = tb_transform_get_world_trans(ecs, &trans);

  float3 accel = {0};

  JPH::BodyID body = (JPH::BodyID)rb.body;
  JPH::Vec3 jph_vel = body_iface.GetLinearVelocity(body);
  float3 velocity = f3(jph_vel.GetX(), jph_vel.GetY(), jph_vel.GetZ());

  if (input.keyboard.key_W) {
    accel += TB_FORWARD;
  }
  if (input.keyboard.key_A) {
    accel += TB_LEFT;
  }
  if (input.keyboard.key_S) {
    accel += TB_BACKWARD;
  }
  if (input.keyboard.key_D) {
    accel += TB_RIGHT;
  }

  if (magsqf3(accel) != 0) {
    accel = normf3(qrotf3(transform.rotation, accel));
    velocity += accel * move.speed;
  }

  if (input.keyboard.key_space && SDL_fabsf(velocity.y) <= 0.001f) {
    velocity += f3(0, 10, 0);
  }

  if (magsqf3(velocity) != 0) {
    float max_speed = 10.0f;
    if (magf2(velocity.xz) > max_speed) {
      float2 v = normf2(velocity.xz) * max_speed;
      velocity = f3(v.x, velocity.y, v.y);
    }

    velocity.xz *= 0.97f; // Drag
  }

  body_iface.SetLinearVelocity(body,
                               JPH::Vec3(velocity.x, velocity.y, velocity.z));

  // We don't want the body to rotate at all
  body_iface.SetAngularVelocity(body, JPH::Vec3(0, 0, 0));
}

void tp_movement_update_tick(flecs::iter &it,
                             ThirdPersonMovementComponent *move,
                             TransformComponent *trans) {
  auto ecs = it.world();
  const auto &input_sys = *ecs.get<InputSystem>();
  for (auto i : it) {
    update_tp_movement(ecs, input_sys, move[i], trans[i]);
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
