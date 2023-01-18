#include "boatcamerasystem.h"

#include "inputcomponent.h"
#include "profiling.h"
#include "sailingcomponents.h"
#include "transformcomponent.h"
#include "world.h"

bool create_boat_camera_system(BoatCameraSystem *self,
                               const BoatCameraSystemDescriptor *desc,
                               uint32_t system_dep_count,
                               System *const *system_deps) {
  (void)system_dep_count;
  (void)system_deps;
  *self = (BoatCameraSystem){
      .tmp_alloc = desc->tmp_alloc,
  };
  return true;
}

void destroy_boat_camera_system(BoatCameraSystem *self) {
  *self = (BoatCameraSystem){0};
}

void tick_boat_camera_system(BoatCameraSystem *self, const SystemInput *input,
                             SystemOutput *output, float delta_seconds) {
  TracyCZoneN(ctx, "Boat Camera System Tick", true);
  TracyCZoneColor(ctx, TracyCategoryColorGame);

  (void)self;
  (void)input;
  (void)output;
  (void)delta_seconds;

  EntityId *entities = tb_get_column_entity_ids(input, 0);
  uint32_t entity_count = tb_get_column_component_count(input, 0);
  if (entity_count == 0) {
    TracyCZoneEnd(ctx);
    return;
  }

  const PackedComponentStore *transform_store =
      tb_get_column_check_id(input, 0, 0, TransformComponentId);
  const PackedComponentStore *boat_cam_store =
      tb_get_column_check_id(input, 0, 1, BoatCameraComponentId);

  const PackedComponentStore *input_store =
      tb_get_column_check_id(input, 1, 0, InputComponentId);

  const InputComponent *input_comp =
      tb_get_component(input_store, 0, InputComponent);

  // Copy the boat camera component for output
  BoatCameraComponent *out_boat_cams =
      tb_alloc_nm_tp(self->tmp_alloc, entity_count, BoatCameraComponent);
  SDL_memcpy(out_boat_cams, boat_cam_store->components,
             entity_count * sizeof(BoatCameraComponent));

  // Make a copy of the transform input as the output
  TransformComponent *out_transforms =
      tb_alloc_nm_tp(self->tmp_alloc, entity_count, TransformComponent);
  SDL_memcpy(out_transforms, transform_store->components,
             entity_count * sizeof(TransformComponent));

  for (uint32_t i = 0; i < entity_count; ++i) {
    // Get parent transform to determine where the parent boat hull is that we
    // want to focus on
    TransformComponent *transform_comp = &out_transforms[i];
    BoatCameraComponent *boat_cam = &out_boat_cams[i];

    TransformComponent *hull_transform_comp =
        tb_transform_get_parent(transform_comp);
    float3 hull_pos = hull_transform_comp->transform.position;

    // Determine how far the camera wants to be from the boat
    float3 pos_hull_diff = transform_comp->transform.position - hull_pos;

    float target_distance = boat_cam->target_dist;
    if (target_distance == 0.0f) {
      target_distance = magf3(pos_hull_diff);
    }

    target_distance += input_comp->mouse.wheel[1] * boat_cam->zoom_speed;
    target_distance =
        clampf(target_distance, boat_cam->min_dist, boat_cam->max_dist);

    // Determine the unit vector that describes the direction the camera
    // wants to be offset from the boat
    float3 pos_to_hull = normf3(pos_hull_diff);

    transform_comp->transform.position =
        lerpf3(transform_comp->transform.position,
               pos_to_hull * target_distance, delta_seconds);

    boat_cam->target_dist = target_distance;
  }

  (void)input_store;

  // Report output
  output->set_count = 2;
  output->write_sets[0] = (SystemWriteSet){
      .id = BoatCameraComponentId,
      .count = entity_count,
      .components = (uint8_t *)out_boat_cams,
      .entities = entities,
  };
  output->write_sets[1] = (SystemWriteSet){
      .id = TransformComponentId,
      .count = entity_count,
      .components = (uint8_t *)out_transforms,
      .entities = entities,
  };

  TracyCZoneEnd(ctx);
}

TB_DEFINE_SYSTEM(boat_camera, BoatCameraSystem, BoatCameraSystemDescriptor)

void tb_boat_camera_system_descriptor(
    SystemDescriptor *desc, const BoatCameraSystemDescriptor *cam_desc) {
  *desc = (SystemDescriptor){
      .name = "BoatCamera",
      .size = sizeof(BoatCameraSystem),
      .id = BoatCameraSystemId,
      .desc = (InternalDescriptor)cam_desc,
      .dep_count = 2,
      .deps[0] = {2, {TransformComponentId, BoatCameraComponentId}},
      .deps[1] = {1, {InputComponentId}},
      .create = tb_create_boat_camera_system,
      .destroy = tb_destroy_boat_camera_system,
      .tick = tb_tick_boat_camera_system,
  };
}
