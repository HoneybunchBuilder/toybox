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

void update_tp_movement(flecs::world &ecs, float delta_time,
                        const InputSystem &input,
                        ThirdPersonMovementComponent &move,
                        TransformComponent &trans) {
  const auto &body_world_trans = tb_transform_get_world_trans(ecs, &trans);

  // Handle movement of body
  {
    const auto &phys_sys = ecs.get<TbPhysicsSystem>();
    auto &body_iface = phys_sys->jolt_phys->GetBodyInterface();
    const auto &rb = *ecs.entity(move.body).get<TbRigidbodyComponent>();

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

    // Apply input as acceleration to the body's velocity
    if (magsqf3(accel) != 0) {
      accel = normf3(qrotf3(body_world_trans.rotation, accel));
      velocity += accel * move.speed;
    }

    // Jump
    if (input.keyboard.key_space && SDL_fabsf(velocity.y) <= 0.001f) {
      velocity += f3(0, 10, 0);
    }

    // Clamp planar speed without affecting jump velocity
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
  }

  // Handle updating camera
  {
    auto &camera_trans_comp =
        *ecs.entity(move.camera).get_mut<TransformComponent>();
    // The camera is parented to the body, so the normalized position of the
    // camera is the local space vector from the body to the camera
    float3 body_to_cam = normf3(camera_trans_comp.transform.position);

    // Read mouse/controller input to rotate the vector to determine the
    // direction we want the camera to live at
    // Arcball the camera around the boat
    {
      float look_speed = 5.0f;

      float look_yaw = 0.0f;
      float look_pitch = 0.0f;
      if (input.mouse.left || input.mouse.right || input.mouse.middle) {
        float2 look_axis = input.mouse.axis;
        look_yaw = look_axis.x * delta_time * look_speed;
        look_pitch = look_axis.y * delta_time * look_speed;
      } else if (input.controller_count > 0) {
        const TBGameControllerState *ctl_state = &input.controller_states[0];
        float2 look_axis = ctl_state->right_stick;
        float deadzone = 0.15f;
        if (look_axis.x > -deadzone && look_axis.x < deadzone) {
          look_axis.x = 0.0f;
        }
        if (look_axis.y > -deadzone && look_axis.y < deadzone) {
          look_axis.y = 0.0f;
        }
        look_yaw = look_axis.x * delta_time;
        look_pitch = look_axis.y * delta_time;
      }

      Quaternion yaw_quat = angle_axis_to_quat(f4(0, 1, 0, look_yaw));
      body_to_cam = normf3(qrotf3(yaw_quat, body_to_cam));
      float3 right = normf3(crossf3(TB_UP, body_to_cam));
      Quaternion pitch_quat = angle_axis_to_quat(f3tof4(right, look_pitch));
      body_to_cam = normf3(qrotf3(pitch_quat, body_to_cam));
    }

    // Construct target position to move the camera to
    float distance = 10.0f;
    float3 camera_pos = body_to_cam * distance;

    // The local space offset we want the camera to focus on
    float3 target_pos = {0};

    // Make sure to offset the target pos too
    camera_pos += target_pos;

    Transform look_trans =
        look_forward_transform(camera_pos, -body_to_cam, TB_UP);
    look_trans.position = camera_pos;

    camera_trans_comp.transform = look_trans;
    camera_trans_comp.dirty = true;
  }
}

void tp_movement_update_tick(flecs::iter &it,
                             ThirdPersonMovementComponent *move,
                             TransformComponent *trans) {
  auto ecs = it.world();
  const auto &input_sys = *ecs.get<InputSystem>();
  for (auto i : it) {
    update_tp_movement(ecs, it.delta_time(), input_sys, move[i], trans[i]);
  }
}

void tb_register_third_person_systems(TbWorld *world) {
  flecs::world ecs(world->ecs);

  ecs.system<ThirdPersonMovementComponent, TransformComponent>(
         "ThirdPersonMovementSystem")
      .kind(EcsPreUpdate)
      .iter(tp_movement_update_tick);

  tb_register_third_person_components(world);
}
