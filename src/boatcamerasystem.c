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
