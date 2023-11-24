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
      color.r = 255.0f;
    } else {
      color.r = temp - 60.0f;
      color.r = 329.698727446f * SDL_powf(color.r, -0.1332047592f);
    }
    color.r /= 255.0f;
  }

  // Calc Green
  {
    if (temp <= 66.0f) {
      color.g = temp;
      color.g = 99.4708025861f * SDL_logf(color.g) - 161.1195681661f;
    } else {
      color.g = temp - 60.0f;
      color.g = 288.1221695283f * SDL_powf(color.g, -0.0755148492f);
    }
    if (color.g < 0.0f) {
      color.g = 0.0f;
    }
    if (color.g > 255.0f) {
      color.g = 255.0f;
    }
    color.g /= 255.0f;
  }

  // Calc Blue
  {
    if (temp >= 66.0f) {
      color.b = 255.0f;
    } else {
      if (temp <= 19.0f) {
        color.b = 0.0f;
      } else {
        color.b = temp - 10;
        color.b = 138.5177312231f * SDL_logf(color.b) - 305.0447927307f;
        if (color.b < 0.0f) {
          color.b = 0.0f;
        }
        if (color.b > 255.0f) {
          color.b = 255.0f;
        }
      }
    }
    color.b /= 255.0f;
  }

  return color;
}

void time_of_day_tick(ecs_iter_t *it) {
  TracyCZoneNC(ctx, "TimeOfDay System", TracyCategoryColorCore, true);
  TimeOfDaySystem *sys = ecs_field(it, TimeOfDaySystem, 1);
  sys->time +=
      it->delta_time * 0.002f; // go a litte slower than everything else

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
  TracyCZoneEnd(ctx);
}

void tb_register_time_of_day_sys(ecs_world_t *ecs) {
  ECS_COMPONENT(ecs, SkyComponent);
  ECS_COMPONENT(ecs, DirectionalLightComponent);
  ECS_COMPONENT(ecs, TransformComponent);
  ECS_COMPONENT(ecs, TimeOfDaySystem);

  TimeOfDaySystem sys = {
      .time = 0.3f, // Start with some time so it's not pitch black
  };

  // Sets a singleton by ptr
  ecs_set_ptr(ecs, ecs_id(TimeOfDaySystem), TimeOfDaySystem, &sys);

  ECS_SYSTEM(ecs, time_of_day_tick, EcsPreUpdate,
             TimeOfDaySystem(TimeOfDaySystem), [inout] SkyComponent,
             [inout] DirectionalLightComponent, [inout] TransformComponent);
}

void tb_unregister_time_of_day_sys(ecs_world_t *ecs) {
  ECS_COMPONENT(ecs, TimeOfDaySystem);
  TimeOfDaySystem *sys = ecs_singleton_get_mut(ecs, TimeOfDaySystem);
  destroy_time_of_day_system(sys);
  ecs_singleton_remove(ecs, TimeOfDaySystem);
}
