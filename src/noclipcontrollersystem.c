#include "noclipcontrollersystem.h"

#include "inputcomponent.h"
#include "noclipcomponent.h"
#include "profiling.h"
#include "tbcommon.h"
#include "tbsdl.h"
#include "transformcomponent.h"
#include "world.h"

bool create_noclip_system(NoClipControllerSystem *self,
                          const NoClipControllerSystemDescriptor *desc) {
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

  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Ticking NoClip System");

  const uint32_t dep_count = input->dep_set_count > MAX_DEPENDENCY_SET_COUT
                                 ? MAX_DEPENDENCY_SET_COUT
                                 : input->dep_set_count;
  if (dep_count > 0) {
    // Expecting one dependency set with all entities that have a transform,
    // camera and noclip component attached
    EntityId *entities = NULL;
    uint32_t entity_count = 0;
    PackedComponentStore transform_store = {0};
    PackedComponentStore noclip_store = {0};

    // Expecting one dependency set with one input component
    PackedComponentStore input_store = {0};
    uint32_t input_comp_count = 0;

    for (uint32_t dep_idx = 0; dep_idx < dep_count; ++dep_idx) {
      const SystemDependencySet *dep_set = &input->dep_sets[dep_idx];

      const uint32_t col_count = dep_set->column_count > MAX_COLUMN_COUNT
                                     ? MAX_COLUMN_COUNT
                                     : dep_set->column_count;
      for (uint32_t col_idx = 0; col_idx < col_count; ++col_idx) {
        const PackedComponentStore *column = &dep_set->columns[col_idx];
        if (column->id == TransformComponentId ||
            column->id == NoClipComponentId) {
          entities = dep_set->entity_ids;
          entity_count = dep_set->entity_count;
          if (column->id == TransformComponentId) {
            transform_store = *column;
            continue;
          } else if (column->id == NoClipComponentId) {
            noclip_store = *column;
            continue;
          }
        }

        if (column->id == InputComponentId) {
          input_store = *column;
          input_comp_count = dep_set->column_count;
          continue;
        }
      }
    }

    // Make sure all dependent stores were found
    if (transform_store.components != NULL && noclip_store.components != NULL &&
        input_store.components != NULL) {

      const NoClipComponent *noclip_comps =
          (const NoClipComponent *)noclip_store.components;

      // Make a copy of the transform input as the output
      TransformComponent *out_transforms =
          tb_alloc_nm_tp(self->tmp_alloc, entity_count, TransformComponent);
      SDL_memcpy(out_transforms, transform_store.components,
                 entity_count * sizeof(TransformComponent));

      // Based on the input, modify all the transform components for each
      // entity
      const InputComponent *input_comps =
          (const InputComponent *)input_store.components;
      for (uint32_t in_idx = 0; in_idx < input_comp_count; ++in_idx) {
        const InputComponent *input_comp = &input_comps[in_idx];

        for (uint32_t event_idx = 0; event_idx < input_comp->event_count;
             ++event_idx) {
          const SDL_Event *event = &input_comp->events[event_idx];
          const uint32_t event_type = event->type;

          NoClipState state = 0;
          int32_t mouse_x_delta = 0;
          int32_t mouse_y_delta = 0;

          switch (event_type) {
          case SDL_KEYDOWN:
          case SDL_KEYUP: {
            NoClipState edit_state = 0;
            const SDL_Keysym *keysym = &event->key.keysym;
            SDL_Scancode scancode = keysym->scancode;

            if (scancode == SDL_SCANCODE_W) {
              edit_state = NOCLIP_MOVING_FORWARD;
            } else if (scancode == SDL_SCANCODE_A) {
              edit_state = NOCLIP_MOVING_LEFT;
            } else if (scancode == SDL_SCANCODE_S) {
              edit_state = NOCLIP_MOVING_BACKWARD;
            } else if (scancode == SDL_SCANCODE_D) {
              edit_state = NOCLIP_MOVING_RIGHT;
            }

            if (event_type == SDL_KEYDOWN) {
              state |= edit_state;
            } else if (event_type == SDL_KEYUP) {
              state &= ~edit_state;
            }
            break;
          }
          case SDL_MOUSEMOTION: {
            const SDL_MouseMotionEvent *mouse_motion = &event->motion;
            // What buttons are down while the mouse is moving?
            uint32_t button_state = mouse_motion->state;

            // if SOME button is pressed
            if (button_state != 0) {
              mouse_x_delta += mouse_motion->xrel;
              mouse_y_delta += mouse_motion->yrel;

              if (mouse_x_delta > 0) {
                state |= NOCLIP_LOOKING_RIGHT;
              } else if (mouse_x_delta < 0) {
                state |= NOCLIP_LOOKING_RIGHT;
              }
              if (mouse_y_delta > 0) {
                state |= NOCLIP_LOOKING_DOWN;
              } else if (mouse_y_delta < 0) {
                state |= NOCLIP_LOOKING_UP;
              }
            }

            break;
          }
          default:
            break;
          }

          // If we have a state to process, transform each output
          // transform appropriately
          if (state) {
            for (uint32_t trans_idx = 0; trans_idx < entity_count;
                 ++trans_idx) {
              const NoClipComponent *noclip = &noclip_comps[trans_idx];
              TransformComponent *out_transform = &out_transforms[trans_idx];

              float4x4 mat = {.row0 = {0}};
              transform_to_matrix(&mat, &out_transform->transform);

              float3 right = f4tof3(mat.row0);
              float3 forward = f4tof3(mat.row2);

              float3 velocity = {0};
              {
                float delta_move_speed = noclip->move_speed * delta_seconds;
                if (state & NOCLIP_MOVING_FORWARD) {
                  velocity -= forward * delta_move_speed;
                }
                if (state & NOCLIP_MOVING_LEFT) {
                  velocity -= right * delta_move_speed;
                }
                if (state & NOCLIP_MOVING_BACKWARD) {
                  velocity += forward * delta_move_speed;
                }
                if (state & NOCLIP_MOVING_RIGHT) {
                  velocity += right * delta_move_speed;
                }
              }

              float3 angular_velocity = {0};
              if (state & NOCLIP_LOOKING) {
                float delta_look_speed = noclip->look_speed * delta_seconds;
                if (state & NOCLIP_LOOKING_RIGHT ||
                    state & NOCLIP_LOOKING_LEFT) {
                  angular_velocity[1] +=
                      (float)mouse_x_delta * delta_look_speed;
                }
                if (state & NOCLIP_LOOKING_DOWN || state & NOCLIP_LOOKING_UP) {
                  angular_velocity[0] -=
                      (float)mouse_y_delta * delta_look_speed;
                }
              }

              out_transform->transform.position += velocity;
              out_transform->transform.rotation += angular_velocity;
            }
          }
        }
      }

      // Report output
      output->set_count = 1;
      output->write_sets[0] = (SystemWriteSet){
          .id = TransformComponentId,
          .count = entity_count,
          .components = (uint8_t *)out_transforms,
          .entities = entities,
      };

    } else {
      TB_CHECK(false, "Failed to retrieve dependent component stores for "
                      "NoClipControllerSystem");
    }
  }

  TracyCZoneEnd(tick_ctx);
}

TB_DEFINE_SYSTEM(noclip, NoClipControllerSystem,
                 NoClipControllerSystemDescriptor)

void tb_noclip_controller_system_descriptor(
    SystemDescriptor *desc,
    const NoClipControllerSystemDescriptor *noclip_desc) {
  desc->name = "NoClip";
  desc->size = sizeof(NoClipControllerSystem);
  desc->id = NoClipControllerSystemId;
  desc->desc = (InternalDescriptor)noclip_desc;
  SDL_memset(desc->deps, 0,
             sizeof(SystemComponentDependencies) * MAX_DEPENDENCY_SET_COUT);
  desc->dep_count = 2;
  desc->deps[0] = (SystemComponentDependencies){2,
                                                {
                                                    TransformComponentId,
                                                    NoClipComponentId,
                                                }};
  desc->deps[1] = (SystemComponentDependencies){1,
                                                {
                                                    InputComponentId,
                                                }};
  desc->create = tb_create_noclip_system;
  desc->destroy = tb_destroy_noclip_system;
  desc->tick = tb_tick_noclip_system;
}
