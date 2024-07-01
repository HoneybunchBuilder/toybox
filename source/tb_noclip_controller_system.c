#include "json.h"
#include "tb_common.h"
#include "tb_input_system.h"
#include "tb_noclip_component.h"
#include "tb_profiling.h"
#include "tb_sdl.h"
#include "tb_transform_component.h"
#include "tb_world.h"

#include <flecs.h>

void noclip_update_tick(ecs_iter_t *it) {
  TracyCZoneNC(ctx, "Noclip Update System", TracyCategoryColorCore, true);
  ecs_world_t *ecs = it->world;

  TbInputSystem *input = ecs_singleton_get_mut(ecs, TbInputSystem);

  TbTransformComponent *transforms = ecs_field(it, TbTransformComponent, 1);
  TbNoClipComponent *noclips = ecs_field(it, TbNoClipComponent, 2);

  for (int32_t i = 0; i < it->count; ++i) {
    TbTransformComponent *transform = &transforms[i];
    TbNoClipComponent *noclip = &noclips[i];

    float2 look_axis = {0};
    float2 move_axis = {0};

    // Based on the input, modify all the transform components for each
    // entity
    // Keyboard and mouse input
    {
      const TbKeyboard *keyboard = &input->keyboard;
      if (keyboard->key_W) {
        move_axis.y += 1.0f;
      }
      if (keyboard->key_A) {
        move_axis.x -= 1.0f;
      }
      if (keyboard->key_S) {
        move_axis.y -= 1.0f;
      }
      if (keyboard->key_D) {
        move_axis.x += 1.0f;
      }
      const TbMouse *mouse = &input->mouse;
      if (mouse->left || mouse->right || mouse->middle) {
        look_axis = -mouse->axis;
      }
    }

    // Go through game gamepad input
    // Just gamepad 0 for now but only if keyboard input wasn't
    // specified
    {
      const TbGameControllerState *ctl_state = &input->gamepad_states[0];
      if (look_axis.x == 0 && look_axis.y == 0) {
        look_axis = -ctl_state->right_stick;
      }
      if (move_axis.x == 0 && move_axis.y == 0) {
        move_axis = ctl_state->left_stick;
        move_axis.y = -move_axis.y;
      }
    }

    float3 forward = tb_transform_get_forward(&transform->transform);
    float3 right = tb_crossf3(forward, TB_UP);
    float3 up = tb_crossf3(right, forward);

    float3 velocity = {0};
    {
      float delta_move_speed = noclip->move_speed * it->delta_time;

      velocity += forward * delta_move_speed * move_axis.y;
      velocity += right * delta_move_speed * move_axis.x;
    }

    TbQuaternion angular_velocity = {0};
    {
      float delta_look_speed = noclip->look_speed * it->delta_time;

      TbQuaternion av0 =
          tb_angle_axis_to_quat(tb_f3tof4(up, look_axis.x * delta_look_speed));
      TbQuaternion av1 = tb_angle_axis_to_quat(
          tb_f3tof4(right, look_axis.y * delta_look_speed));

      angular_velocity = tb_mulq(av0, av1);
    }

    tb_translate(&transform->transform, velocity);
    tb_rotate(&transform->transform, angular_velocity);
    transform->dirty = true;
  }
  TracyCZoneEnd(ctx);
}

void tb_register_noclip_sys(TbWorld *world) {
  TracyCZoneN(ctx, "Register Noclip Sys", true);
  ecs_world_t *ecs = world->ecs;

  ECS_SYSTEM(ecs, noclip_update_tick,
             EcsOnUpdate, [out] TbTransformComponent, [out] TbNoClipComponent);
  TracyCZoneEnd(ctx);
}

void tb_unregister_noclip_sys(TbWorld *world) { (void)world; }

TB_REGISTER_SYS(tb, noclip, TB_SYSTEM_NORMAL)
