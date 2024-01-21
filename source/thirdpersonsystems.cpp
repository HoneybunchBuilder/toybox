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
                        const TbInputSystem &input,
                        TbThirdPersonMovementComponent &move,
                        flecs::entity entity) {
  auto &camera_trans_comp =
      *ecs.entity(move.camera).get_mut<TbTransformComponent>();
  auto &camera_trans = camera_trans_comp.transform;

  // Update camera positioning and rotation
  // Stays in local space
  {
    // The camera is parented to the body, so the normalized position of the
    // camera is the local space vector from the body to the camera
    auto body_to_cam = tb_normf3(camera_trans.position);

    // Read mouse/gamepad input to rotate the vector to determine the
    // direction we want the camera to live at

    if (move.fixed_rotation)
    // Camera stays focused on the character from a specific
    {
      auto rotation =
          tb_angle_axis_to_quat(tb_f3tof4(TB_RIGHT, tb_deg_to_rad(move.angle)));
      body_to_cam = tb_normf3(tb_qrotf3(rotation, TB_FORWARD));
    } else
    // Arcball the camera around the character
    {
      auto look_speed = 5.0f;
      auto look_yaw = 0.0f;
      auto look_pitch = 0.0f;
      if (input.mouse.left || input.mouse.right || input.mouse.middle) {
        auto look_axis = input.mouse.axis;
        look_yaw = -look_axis.x * delta_time * look_speed;
        look_pitch = -look_axis.y * delta_time * look_speed;
      } else if (input.gamepad_count > 0) {
        const auto *ctl_state = &input.gamepad_states[0];
        auto look_axis = ctl_state->right_stick;
        auto deadzone = 0.15f;
        if (look_axis.x > -deadzone && look_axis.x < deadzone) {
          look_axis.x = 0.0f;
        }
        if (look_axis.y > -deadzone && look_axis.y < deadzone) {
          look_axis.y = 0.0f;
        }
        look_yaw = look_axis.x * delta_time;
        look_pitch = look_axis.y * delta_time;
      }

      auto yaw_quat = tb_angle_axis_to_quat(tb_f4(0, 1, 0, look_yaw));
      body_to_cam = tb_normf3(tb_qrotf3(yaw_quat, body_to_cam));
      auto right = tb_normf3(tb_crossf3(TB_UP, body_to_cam));
      auto pitch_quat = tb_angle_axis_to_quat(tb_f3tof4(right, look_pitch));
      body_to_cam = tb_normf3(tb_qrotf3(pitch_quat, body_to_cam));
    }

    // Construct target position to move the camera to
    // TODO: Allow other modifiers
    float distance = move.distance;
    float3 camera_pos = body_to_cam * distance;

    // TODO: The local space offset we want the camera to focus on
    // So the camera can break away from the player a bit
    float3 target_pos = {0};
    camera_pos += target_pos;

    camera_trans = tb_look_forward_transform(camera_pos, -body_to_cam, TB_UP);
    camera_trans_comp.dirty = true;
  }

  // Handle movement of body
  // Direciton of movement depends on camera's forward
  {
    const auto &phys_sys = ecs.get<TbPhysicsSystem>();
    auto &body_iface = phys_sys->jolt_phys->GetBodyInterface();
    const auto &rb = *ecs.entity(move.body).get<TbRigidbodyComponent>();

    TbQuaternion move_rot = {};
    {
      TbTransform camera_world_trans =
          tb_transform_get_world_trans(ecs.c_ptr(), move.camera);
      TbTransform body_world_trans =
          tb_transform_get_world_trans(ecs.c_ptr(), entity);
      float3 dir =
          tb_normf3(body_world_trans.position - camera_world_trans.position);
      float2 planar_dir = tb_normf2(dir.xz);
      float3 move_forward = tb_f3(planar_dir.x, 0, planar_dir.y);
      move_rot = tb_look_forward_quat(move_forward, TB_UP);
    }

    float3 accel = {0};

    JPH::BodyID body = (JPH::BodyID)rb.body;
    JPH::Vec3 jph_vel = body_iface.GetLinearVelocity(body);
    float3 velocity = tb_f3(jph_vel.GetX(), jph_vel.GetY(), jph_vel.GetZ());

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

    if (input.gamepad_count > 0) {
      float2 stick = input.gamepad_states[0].left_stick;
      accel += tb_f3(stick.x, 0, stick.y);
    }

    // Apply input as acceleration to the body's velocity
    auto sq_accel_mag = tb_magsqf3(accel);
    if (sq_accel_mag > 0) {
      auto accel_mag = SDL_sqrtf(sq_accel_mag);
      accel *= delta_time;
      accel = tb_normf3(tb_qrotf3(move_rot, accel));
      auto accel_alpha = tb_clampf(accel_mag, 0.0f, 1.0f);
      auto speed = tb_lerpf(0.0f, move.speed, accel_alpha);
      velocity += accel * speed;
    }

    // Jump
    if (move.jump) {
      bool jump_input = input.keyboard.key_space;
      if (input.gamepad_count > 0) {
        jump_input = jump_input ||
                     ((input.gamepad_states[0].buttons & TB_BUTTON_A) > 0);
      }
      if (jump_input && SDL_fabsf(velocity.y) <= 0.001f) {
        velocity += tb_f3(0, move.jump_velocity, 0);
      }
    }

    // Clamp planar speed without affecting jump velocity
    if (tb_magsqf3(velocity) != 0) {
      float max_speed = 10.0f;
      if (tb_magf2(velocity.xz) > max_speed) {
        float2 v = tb_normf2(velocity.xz) * max_speed;
        velocity = tb_f3(v.x, velocity.y, v.y);
      }

      velocity.xz *= 0.90f; // Drag
    }

    body_iface.SetLinearAndAngularVelocity(
        body, JPH::Vec3(velocity.x, velocity.y, velocity.z),
        JPH::Vec3(0, 0, 0));
  }
}

void tp_movement_update_tick(flecs::iter &it,
                             TbThirdPersonMovementComponent *move) {
  auto ecs = it.world();
  const auto &input_sys = *ecs.get<TbInputSystem>();
  for (auto i : it) {
    update_tp_movement(ecs, it.delta_time(), input_sys, move[i], it.entity(i));
  }
}

void tb_register_third_person_systems(TbWorld *world) {
  flecs::world ecs(world->ecs);

  ecs.system<TbThirdPersonMovementComponent>("ThirdPersonMovementSystem")
      .kind(EcsPreUpdate)
      .iter(tp_movement_update_tick);

  tb_register_third_person_components(world);
}
