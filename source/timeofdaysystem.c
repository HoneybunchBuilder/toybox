#include "timeofdaysystem.h"

#include "common.hlsli"
#include "lightcomponent.h"
#include "profiling.h"
#include "skycomponent.h"
#include "tbcommon.h"
#include "transformcomponent.h"
#include "viewsystem.h"
#include "world.h"

#include <math.h>

#include <flecs.h>

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

float3 lookup_sun_color(float norm) {
  // Convert normalized time of day into a color temp
  float temperature = 0.0f;
  if (norm < 0.25f) {
    // As sun rises, so does the color temp
    temperature = lerpf(1000, 12000, norm * 4.0f);
  } else if (norm < 0.5f) {
    // As the sun sets, the color temp goes back down
    temperature = lerpf(12000, 1000, (norm - 0.25f) * 4.0f);
  } else {
    // When the sun is set, until it rises, we just bail as the sun should
    // not be providing any light
    return (float3){0};
  }

  // Convert kelvin to RGB
  // https://tannerhelland.com/2012/09/18/convert-temperature-rgb-algorithm-code.html
  float3 color = {0};
  const float temp = temperature / 100.0f;

  // Calc Red
  {
    if (temp <= 66.0f) {
      color[0] = 255.0f;
    } else {
      color[0] = temp - 60.0f;
      color[0] = 329.698727446f * SDL_powf(color[0], -0.1332047592f);
    }
    color[0] /= 255.0f;
  }

  // Calc Green
  {
    if (temp <= 66.0f) {
      color[1] = temp;
      color[1] = 99.4708025861f * SDL_logf(color[1]) - 161.1195681661f;
    } else {
      color[1] = temp - 60.0f;
      color[1] = 288.1221695283f * SDL_powf(color[1], -0.0755148492f);
    }
    if (color[1] < 0.0f) {
      color[1] = 0.0f;
    }
    if (color[1] > 255.0f) {
      color[1] = 255.0f;
    }
    color[1] /= 255.0f;
  }

  // Calc Blue
  {
    if (temp >= 66.0f) {
      color[2] = 255.0f;
    } else {
      if (temp <= 19.0f) {
        color[2] = 0.0f;
      } else {
        color[2] = temp - 10;
        color[2] = 138.5177312231f * SDL_logf(color[2]) - 305.0447927307f;
        if (color[2] < 0.0f) {
          color[2] = 0.0f;
        }
        if (color[2] > 255.0f) {
          color[2] = 255.0f;
        }
      }
    }
    color[2] /= 255.0f;
  }

  return color;
}

void tick_time_of_day_system_internal(TimeOfDaySystem *self,
                                      const SystemInput *input,
                                      SystemOutput *output,
                                      float delta_seconds) {
  TracyCZoneNC(ctx, "Time Of Day Tick", TracyCategoryColorGame, true);
  const float time_scale = 0.05f;
  self->time += (delta_seconds * time_scale);

  EntityId *sky_entities = tb_get_column_entity_ids(input, 0);
  EntityId *sun_entities = tb_get_column_entity_ids(input, 1);

  // Find components
  const uint32_t sky_count = tb_get_column_component_count(input, 0);
  const PackedComponentStore *skys =
      tb_get_column_check_id(input, 0, 0, SkyComponentId);

  const uint32_t sun_count = tb_get_column_component_count(input, 1);
  const PackedComponentStore *sun_lights =
      tb_get_column_check_id(input, 1, 0, DirectionalLightComponentId);
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

  // Copy the sun lights & transforms for output
  DirectionalLightComponent *out_sun_lights =
      tb_alloc_nm_tp(self->tmp_alloc, sun_count, DirectionalLightComponent);
  TransformComponent *out_sun_trans =
      tb_alloc_nm_tp(self->tmp_alloc, sun_count, TransformComponent);
  {
    const DirectionalLightComponent *in_light =
        tb_get_component(sun_lights, 0, DirectionalLightComponent);
    SDL_memcpy(out_sun_lights, in_light,
               sun_count * sizeof(DirectionalLightComponent));
    const TransformComponent *in_trans =
        tb_get_component(sun_transforms, 0, TransformComponent);
    SDL_memcpy(out_sun_trans, in_trans, sun_count * sizeof(TransformComponent));
  }

  self->time = self->time > TAU ? self->time - TAU : self->time;
  const float time_norm = self->time / TAU;
  out_sun_trans->transform.rotation =
      angle_axis_to_quat((float4){-1.0f, 0.0f, 0.0f, self->time});
  out_sun_lights->color = lookup_sun_color(time_norm);

  float3 sun_dir = -transform_get_forward(&out_sun_trans->transform);

  // Update sky component's time and sun direction
  out_sky->time = self->time;
  out_sky->sun_dir = sun_dir;

  // Write out sky(s)
  {
    output->set_count = 3;
    output->write_sets[0] = (SystemWriteSet){
        .id = SkyComponentId,
        .count = sky_count,
        .components = (uint8_t *)out_sky,
        .entities = sky_entities,
    };
    output->write_sets[1] = (SystemWriteSet){
        .id = TransformComponentId,
        .count = sun_count,
        .components = (uint8_t *)out_sun_lights,
        .entities = sun_entities,
    };
    output->write_sets[2] = (SystemWriteSet){
        .id = TransformComponentId,
        .count = sun_count,
        .components = (uint8_t *)out_sun_trans,
        .entities = sun_entities,
    };
  }

  TracyCZoneEnd(ctx);
}

TB_DEFINE_SYSTEM(time_of_day, TimeOfDaySystem, TimeOfDaySystemDescriptor)

void tick_time_of_day_system(void *self, const SystemInput *input,
                             SystemOutput *output, float delta_seconds) {
  SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Tick TimeOfDay System");
  tick_time_of_day_system_internal((TimeOfDaySystem *)self, input, output,
                                   delta_seconds);
}

void tb_time_of_day_system_descriptor(
    SystemDescriptor *desc, const TimeOfDaySystemDescriptor *tod_desc) {
  *desc = (SystemDescriptor){
      .name = "TimeOfDay",
      .size = sizeof(TimeOfDaySystem),
      .id = TimeOfDaySystemId,
      .desc = (InternalDescriptor)tod_desc,
      .system_dep_count = 1,
      .system_deps[0] = ViewSystemId,
      .create = tb_create_time_of_day_system,
      .destroy = tb_destroy_time_of_day_system,
      .tick_fn_count = 1,
      .tick_fns[0] =
          {
              .dep_count = 2,
              .deps[0] = {1, {SkyComponentId}},
              .deps[1] = {2,
                          {DirectionalLightComponentId, TransformComponentId}},
              .system_id = TimeOfDaySystemId,
              .order = E_TICK_PRE_PHYSICS,
              .function = tick_time_of_day_system,
          },
  };
}

void flecs_time_of_day_tick(ecs_iter_t *it) {
  TimeOfDaySystem *sys = ecs_field(it, TimeOfDaySystem, 1);
  sys->time += it->delta_time;

  SkyComponent *skys = ecs_field(it, SkyComponent, 2);
  DirectionalLightComponent *lights =
      ecs_field(it, DirectionalLightComponent, 3);
  TransformComponent *transforms = ecs_field(it, TransformComponent, 4);

  for (int32_t i = 0; i < it->count; ++i) {
    SkyComponent *sky = &skys[i];
    DirectionalLightComponent *light = &lights[i];
    TransformComponent *trans = &transforms[i];

    const float time_norm =
        (sys->time > TAU ? sys->time - TAU : sys->time) / TAU;
    trans->transform.rotation =
        angle_axis_to_quat((float4){-1.0f, 0.0f, 0.0f, sys->time});
    light->color = lookup_sun_color(time_norm);

    float3 sun_dir = -transform_get_forward(&trans->transform);

    // Update sky component's time and sun direction
    sky->time = sys->time;
    sky->sun_dir = sun_dir;
  }
}

void tb_register_time_of_day_sys(ecs_world_t *ecs) {
  ECS_COMPONENT(ecs, ViewSystem);
  ECS_COMPONENT(ecs, SkyComponent);
  ECS_COMPONENT(ecs, DirectionalLightComponent);
  ECS_COMPONENT(ecs, TransformComponent);
  ECS_COMPONENT(ecs, TimeOfDaySystem);

  TimeOfDaySystem sys = {
      .time = 0.0f,
  };

  // Sets a singleton by ptr
  ecs_set_ptr(ecs, ecs_id(TimeOfDaySystem), TimeOfDaySystem, &sys);

  ECS_SYSTEM(ecs, flecs_time_of_day_tick, EcsOnUpdate,
             TimeOfDaySystem(TimeOfDaySystem), [inout] SkyComponent,
             [inout] DirectionalLightComponent, [inout] TransformComponent);
}

void tb_unregister_time_of_day_sys(ecs_world_t *ecs) {
  ECS_COMPONENT(ecs, TimeOfDaySystem);
  TimeOfDaySystem *sys = ecs_singleton_get_mut(ecs, TimeOfDaySystem);
  destroy_time_of_day_system(sys);
}
