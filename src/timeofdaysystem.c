#include "timeofdaysystem.h"

#include "common.hlsli"
#include "lightcomponent.h"
#include "profiling.h"
#include "skycomponent.h"
#include "tbcommon.h"
#include "transformcomponent.h"
#include "viewsystem.h"
#include "world.h"

/*
  HACK:
  This time of day system makes a few assumptions:
  1) That the scene has one entity with a Directional Light and Transform
  component attached and one entity with a Sky component attached. Otherwise it
  will not work as intended. Note that the time of day system doesn't require
  any explicit dependency on the lighting or sky systems.
  2) That there is only one of each of these entities in the scene.
  3) That this system ticks before any other system that derives state; the Sky
  system implictly needs to tick after this otherwise the directional light
  transform will be a frame behind.

  I dislike that the coupling with the sky system is so detached that the
  intended behavior cannot be enforced. The decoupling is neat in theory but
  seems like a gap where a user could make an unnecessary mistake. The only
  mechanism that hints at this coupling is the requirement on the entity to have
  a Sky component attached but misconfiguration seems hard to message to a user.
*/

bool create_time_of_day_system(TimeOfDaySystem *self,
                               const TimeOfDaySystemDescriptor *desc,
                               uint32_t system_dep_count,
                               System *const *system_deps) {

  ViewSystem *view_system =
      tb_get_system(system_deps, system_dep_count, ViewSystem);
  TB_CHECK_RETURN(view_system,
                  "Failed to find view system which time of day depends on",
                  false);

  *self = (TimeOfDaySystem){
      .tmp_alloc = desc->tmp_alloc,
      .std_alloc = desc->std_alloc,
      .view_system = view_system,
  };

  return true;
}

void destroy_time_of_day_system(TimeOfDaySystem *self) {
  *self = (TimeOfDaySystem){0};
}

void tick_time_of_day_system(TimeOfDaySystem *self, const SystemInput *input,
                             SystemOutput *output, float delta_seconds) {
  TracyCZoneNC(ctx, "Time Of Day Tick", TracyCategoryColorGame, true);
  self->time += delta_seconds;

  EntityId *sky_entities = tb_get_column_entity_ids(input, 0);
  EntityId *sun_entities = tb_get_column_entity_ids(input, 1);

  // Find components
  const uint32_t sky_count = tb_get_column_component_count(input, 0);
  const PackedComponentStore *skys =
      tb_get_column_check_id(input, 0, 0, SkyComponentId);

  const uint32_t sun_count = tb_get_column_component_count(input, 1);
  const PackedComponentStore *sun_transforms =
      tb_get_column_check_id(input, 1, 1, TransformComponentId);

  if (sky_count != 1 || sun_count != 1) {
    // Note: I wish C had a defer statement
    TracyCZoneEnd(ctx);
    return;
  }

  // Copy the sky component for output
  SkyComponent *out_sky =
      tb_alloc_nm_tp(self->tmp_alloc, sky_count, SkyComponent);
  {
    const SkyComponent *in = tb_get_component(skys, 0, SkyComponent);
    SDL_memcpy(out_sky, in, sky_count * sizeof(SkyComponent));
  }

  // Copy the sun transforms for output
  TransformComponent *out_sun_trans =
      tb_alloc_nm_tp(self->tmp_alloc, sun_count, TransformComponent);
  {
    const TransformComponent *in =
        tb_get_component(sun_transforms, 0, TransformComponent);
    SDL_memcpy(out_sun_trans, in, sun_count * sizeof(TransformComponent));
  }

  const float time = self->time * 0.1f;
  out_sun_trans->transform.rotation = (EulerAngles){time, 1.0f, 0.0f};

  float4x4 rot_mat = euler_to_trans(out_sun_trans->transform.rotation);
  float3 sun_dir = f4tof3(rot_mat.row2);

  // Update sky component's time and sun direction
  out_sky->time = self->time;
  out_sky->sun_dir = sun_dir;

  // Write out sky(s)
  {
    output->set_count = 2;
    output->write_sets[0] = (SystemWriteSet){
        .id = SkyComponentId,
        .count = sky_count,
        .components = (uint8_t *)out_sky,
        .entities = sky_entities,
    };
    output->write_sets[1] = (SystemWriteSet){
        .id = TransformComponentId,
        .count = sun_count,
        .components = (uint8_t *)out_sun_trans,
        .entities = sun_entities,
    };
  }

  TracyCZoneEnd(ctx);
}

TB_DEFINE_SYSTEM(time_of_day, TimeOfDaySystem, TimeOfDaySystemDescriptor)

void tb_time_of_day_system_descriptor(
    SystemDescriptor *desc, const TimeOfDaySystemDescriptor *tod_desc) {
  *desc = (SystemDescriptor){
      .name = "TimeOfDay",
      .size = sizeof(TimeOfDaySystem),
      .id = TimeOfDaySystemId,
      .desc = (InternalDescriptor)tod_desc,
      .system_dep_count = 1,
      .system_deps[0] = ViewSystemId,
      .dep_count = 2,
      .deps[0] = {1, {SkyComponentId}},
      .deps[1] = {2, {DirectionalLightComponentId, TransformComponentId}},
      .create = tb_create_time_of_day_system,
      .destroy = tb_destroy_time_of_day_system,
      .tick = tb_tick_time_of_day_system,
  };
}
