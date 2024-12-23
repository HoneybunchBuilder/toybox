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
  TB_TRACY_SCOPEC("Noclip Update System", TracyCategoryColorCore);
  ecs_world_t *ecs = it->world;

  tb_auto input = ecs_singleton_ensure(ecs, TbInputSystem);

  tb_auto transforms = ecs_field(it, TbTransformComponent, 0);
  tb_auto noclips = ecs_field(it, TbNoClipComponent, 1);

  for (int32_t i = 0; i < it->count; ++i) {
    tb_auto transform = &transforms[i];
    tb_auto noclip = &noclips[i];
    tb_auto entity = it->entities[i];

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

      tb_auto av0 =
          tb_angle_axis_to_quat(tb_f3tof4(up, look_axis.x * delta_look_speed));
      tb_auto av1 = tb_angle_axis_to_quat(
          tb_f3tof4(right, look_axis.y * delta_look_speed));

      angular_velocity = tb_mulq(av0, av1);
    }

    tb_translate(&transform->transform, velocity);
    tb_rotate(&transform->transform, angular_velocity);
    tb_transform_mark_dirty(it->world, entity);
  }
}

void tb_register_noclip_sys(TbWorld *world) {
  TB_TRACY_SCOPE("Register Noclip Sys");
  ECS_SYSTEM(world->ecs, noclip_update_tick,
             EcsOnUpdate, [out] TbTransformComponent, [out] TbNoClipComponent);
}

void tb_unregister_noclip_sys(TbWorld *world) { (void)world; }

TB_REGISTER_SYS(tb, noclip, TB_SYSTEM_NORMAL)
