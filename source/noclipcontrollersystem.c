#include "noclipcontrollersystem.h"

#include "inputcomponent.h"
#include "noclipcomponent.h"
#include "profiling.h"
#include "tbcommon.h"
#include "tbsdl.h"
#include "transformcomponent.h"
#include "world.h"

bool create_noclip_system(NoClipControllerSystem *self,
                          const NoClipControllerSystemDescriptor *desc,
                          uint32_t system_dep_count,
                          System *const *system_deps) {
  (void)system_dep_count;
  (void)system_deps;
  if (!desc) {
    return false;
  }

  *self = (NoClipControllerSystem){
      .tmp_alloc = desc->tmp_alloc,
  };
  return true;
}

void destroy_noclip_system(NoClipControllerSystem *self) {
  *self = (NoClipControllerSystem){0};
}

void tick_noclip_system(NoClipControllerSystem *self, const SystemInput *input,
                        SystemOutput *output, float delta_seconds) {
  TracyCZoneN(tick_ctx, "NoClip System Tick", true);
  TracyCZoneColor(tick_ctx, TracyCategoryColorGame);

  const uint32_t dep_count = input->dep_set_count > MAX_DEPENDENCY_SET_COUNT
                                 ? MAX_DEPENDENCY_SET_COUNT
                                 : input->dep_set_count;
  if (dep_count > 0) {
    // Expecting one dependency set with all entities that have a transform,
    // camera and noclip component attached
    EntityId *entities = tb_get_column_entity_ids(input, 0);
    uint32_t entity_count = tb_get_column_component_count(input, 0);
    const PackedComponentStore *transform_store =
        tb_get_column_check_id(input, 0, 0, TransformComponentId);
    const PackedComponentStore *noclip_store =
        tb_get_column_check_id(input, 0, 1, NoClipComponentId);

    const PackedComponentStore *input_store =
        tb_get_column_check_id(input, 1, 0, InputComponentId);

    // Expecting one dependency set with one input component
    const uint32_t input_comp_count = tb_get_column_component_count(input, 1);

    // Make sure all dependent stores were found
    if (transform_store != NULL && noclip_store != NULL &&
        input_store != NULL) {

      // Copy the noclip component for output
      NoClipComponent *out_noclips =
          tb_alloc_nm_tp(self->tmp_alloc, entity_count, NoClipComponent);
      SDL_memcpy(out_noclips, noclip_store->components,
                 entity_count * sizeof(NoClipComponent));

      // Make a copy of the transform input as the output
      TransformComponent *out_transforms =
          tb_alloc_nm_tp(self->tmp_alloc, entity_count, TransformComponent);
      SDL_memcpy(out_transforms, transform_store->components,
                 entity_count * sizeof(TransformComponent));

      for (uint32_t trans_idx = 0; trans_idx < entity_count; ++trans_idx) {
        NoClipComponent *noclip = &out_noclips[trans_idx];
        TransformComponent *transform = &out_transforms[trans_idx];

        float2 look_axis = {0};
        float2 move_axis = {0};

        // Based on the input, modify all the transform components for each
        // entity
        const InputComponent *input_comps =
            (const InputComponent *)input_store->components;
        for (uint32_t in_idx = 0; in_idx < input_comp_count; ++in_idx) {
          const InputComponent *input_comp = &input_comps[in_idx];

          // Keyboard and mouse input
          {
            const TBKeyboard *keyboard = &input_comp->keyboard;
            if (keyboard->key_W) {
              move_axis[1] += 1.0f;
            }
            if (keyboard->key_A) {
              move_axis[0] -= 1.0f;
            }
            if (keyboard->key_S) {
              move_axis[1] -= 1.0f;
            }
            if (keyboard->key_D) {
              move_axis[0] += 1.0f;
            }
            const TBMouse *mouse = &input_comp->mouse;
            if (mouse->left || mouse->right || mouse->middle) {
              look_axis = mouse->axis;
            }
          }

          // Go through game controller input
          // Just controller 0 for now but only if keyboard input wasn't
          // specified
          {
            const TBGameControllerState *ctl_state =
                &input_comp->controller_states[0];
            if (look_axis[0] == 0 && look_axis[1] == 0) {
              look_axis = ctl_state->right_stick;
            }
            if (move_axis[0] == 0 && move_axis[1] == 0) {
              move_axis = ctl_state->left_stick;
            }
          }
        }

        float3 forward = -transform_get_forward(&transform->transform);
        float3 right = crossf3((float3){0, 1, 0}, forward);
        float3 up = crossf3(forward, right);

        float3 velocity = {0};
        {
          float delta_move_speed = noclip->move_speed * delta_seconds;

          velocity += forward * delta_move_speed * move_axis[1];
          velocity += right * delta_move_speed * move_axis[0];
        }

        Quaternion angular_velocity = {0};
        {
          float delta_look_speed = noclip->look_speed * delta_seconds;

          Quaternion av0 =
              angle_axis_to_quat(f3tof4(up, look_axis[0] * delta_look_speed));
          Quaternion av1 = angle_axis_to_quat(
              f3tof4(right, look_axis[1] * delta_look_speed));

          angular_velocity = mulq(av0, av1);
        }

        translate(&transform->transform, velocity);
        rotate(&transform->transform, angular_velocity);
      }

      // Report output
      output->set_count = 2;
      output->write_sets[0] = (SystemWriteSet){
          .id = NoClipComponentId,
          .count = entity_count,
          .components = (uint8_t *)out_noclips,
          .entities = entities,
      };
      output->write_sets[1] = (SystemWriteSet){
          .id = TransformComponentId,
          .count = entity_count,
          .components = (uint8_t *)out_transforms,
          .entities = entities,
      };
    }
  }

  TracyCZoneEnd(tick_ctx);
}

TB_DEFINE_SYSTEM(noclip, NoClipControllerSystem,
                 NoClipControllerSystemDescriptor)

void tb_noclip_controller_system_descriptor(
    SystemDescriptor *desc,
    const NoClipControllerSystemDescriptor *noclip_desc) {
  *desc = (SystemDescriptor){
      .name = "NoClip",
      .size = sizeof(NoClipControllerSystem),
      .id = NoClipControllerSystemId,
      .desc = (InternalDescriptor)noclip_desc,
      .dep_count = 2,
      .deps[0] = {2, {TransformComponentId, NoClipComponentId}},
      .deps[1] = {1, {InputComponentId}},
      .create = tb_create_noclip_system,
      .destroy = tb_destroy_noclip_system,
      .tick = tb_tick_noclip_system,
  };
}
