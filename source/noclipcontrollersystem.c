#include "noclipcontrollersystem.h"

#include "assetsystem.h"
#include "inputsystem.h"
#include "json.h"
#include "noclipcomponent.h"
#include "profiling.h"
#include "tbcommon.h"
#include "tbsdl.h"
#include "transformcomponent.h"
#include "world.h"

#include <flecs.h>

void noclip_update_tick(ecs_iter_t *it) {
  TracyCZoneNC(ctx, "Noclip Update System", TracyCategoryColorCore, true);
  ecs_world_t *ecs = it->world;
  ECS_COMPONENT(ecs, InputSystem);

  InputSystem *input = ecs_singleton_get_mut(ecs, InputSystem);

  TransformComponent *transforms = ecs_field(it, TransformComponent, 1);
  NoClipComponent *noclips = ecs_field(it, NoClipComponent, 2);

  for (int32_t i = 0; i < it->count; ++i) {
    TransformComponent *transform = &transforms[i];
    NoClipComponent *noclip = &noclips[i];

    float2 look_axis = {0};
    float2 move_axis = {0};

    // Based on the input, modify all the transform components for each
    // entity
    // Keyboard and mouse input
    {
      const TBKeyboard *keyboard = &input->keyboard;
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
      const TBMouse *mouse = &input->mouse;
      if (mouse->left || mouse->right || mouse->middle) {
        look_axis = -mouse->axis;
      }
    }

    // Go through game controller input
    // Just controller 0 for now but only if keyboard input wasn't
    // specified
    {
      const TBGameControllerState *ctl_state = &input->controller_states[0];
      if (look_axis.x == 0 && look_axis.y == 0) {
        look_axis = ctl_state->right_stick;
      }
      if (move_axis.x == 0 && move_axis.y == 0) {
        move_axis = ctl_state->left_stick;
      }
    }

    float3 forward = transform_get_forward(&transform->transform);
    float3 right = crossf3(forward, TB_UP);
    float3 up = crossf3(right, forward);

    float3 velocity = {0};
    {
      float delta_move_speed = noclip->move_speed * it->delta_time;

      velocity += forward * delta_move_speed * move_axis.y;
      velocity += right * delta_move_speed * move_axis.x;
    }

    Quaternion angular_velocity = {0};
    {
      float delta_look_speed = noclip->look_speed * it->delta_time;

      Quaternion av0 =
          angle_axis_to_quat(f3tof4(up, look_axis.x * delta_look_speed));
      Quaternion av1 =
          angle_axis_to_quat(f3tof4(right, look_axis.y * delta_look_speed));

      angular_velocity = mulq(av0, av1);
    }

    translate(&transform->transform, velocity);
    rotate(&transform->transform, angular_velocity);
  }
  TracyCZoneEnd(ctx);
}

void tb_register_noclip_sys(ecs_world_t *ecs) {
  ECS_COMPONENT(ecs, TransformComponent);
  ECS_COMPONENT(ecs, NoClipComponent);
  ECS_COMPONENT(ecs, NoClipControllerSystem);

  ecs_singleton_set(ecs, NoClipControllerSystem, {0});

  ECS_SYSTEM(ecs, noclip_update_tick,
             EcsOnUpdate, [out] TransformComponent, [out] NoClipComponent)

  tb_register_noclip_component(ecs);
}

void tb_unregister_noclip_sys(ecs_world_t *ecs) {
  ECS_COMPONENT(ecs, NoClipControllerSystem);
  NoClipControllerSystem *sys =
      ecs_singleton_get_mut(ecs, NoClipControllerSystem);
  *sys = (NoClipControllerSystem){0};
  ecs_singleton_remove(ecs, NoClipControllerSystem);
}
