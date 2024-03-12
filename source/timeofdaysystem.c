#include "timeofdaysystem.h"

#include "common.hlsli"
#include "lightcomponent.h"
#include "profiling.h"
#include "skycomponent.h"
#include "tbcommon.h"
#include "transformcomponent.h"
#include "viewsystem.h"
#include "world.h"

#include <flecs.h>
#include <json.h>
#include <math.h>

ECS_COMPONENT_DECLARE(TbTimeOfDayComponent);

typedef struct TbTimeOfDayDescriptor {
  float start_time;
  float time_scale;
} TbTimeOfDayDescriptor;
ECS_COMPONENT_DECLARE(TbTimeOfDayDescriptor);

float3 lookup_sun_color(float norm) {
  // Convert normalized time of day into a color temp
  float temperature = 0.0f;
  if (norm < 0.25f) {
    // As sun rises, so does the color temp
    temperature = tb_lerpf(1000, 12000, norm * 4.0f);
  } else if (norm < 0.5f) {
    // As the sun sets, the color temp goes back down
    temperature = tb_lerpf(12000, 1000, (norm - 0.25f) * 4.0f);
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
  TbWorld *world = ecs_singleton_get_mut(it->world, TbWorldRef)->world;
  tb_auto tods = ecs_field(it, TbTimeOfDayComponent, 1);
  tb_auto skys = ecs_field(it, TbSkyComponent, 2);
  tb_auto lights = ecs_field(it, TbDirectionalLightComponent, 3);
  tb_auto transforms = ecs_field(it, TbTransformComponent, 4);

  for (int32_t i = 0; i < it->count; ++i) {
    tb_auto tod = &tods[i];
    tb_auto sky = &skys[i];
    tb_auto light = &lights[i];
    tb_auto trans = &transforms[i];
    (void)sky; // Sky is unused but required

    tod->time = world->time * tod->time_scale;

    const float time_norm =
        (tod->time > TB_TAU ? tod->time - TB_TAU : tod->time) / TB_TAU;
    trans->transform.rotation =
        tb_angle_axis_to_quat((float4){-1.0f, 0.0f, 0.0f, tod->time});
    light->color = lookup_sun_color(time_norm);
  }
  TracyCZoneEnd(ctx);
}

void tb_register_time_of_day_sys(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;
  ECS_SYSTEM(ecs, time_of_day_tick,
             EcsOnUpdate, [inout] TbTimeOfDayComponent, [inout] TbSkyComponent,
             [inout] TbDirectionalLightComponent, [inout] TbTransformComponent);
}
void tb_unregister_time_of_day_sys(TbWorld *world) { (void)world; }

TB_REGISTER_SYS(tb, time_of_day, TB_TOD_SYS_PRIO)

ecs_entity_t tb_register_time_of_day_comp(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;
  ECS_COMPONENT_DEFINE(ecs, TbTimeOfDayComponent);
  ECS_COMPONENT_DEFINE(ecs, TbTimeOfDayDescriptor);

  ecs_struct(ecs, {.entity = ecs_id(TbTimeOfDayDescriptor),
                   .members = {
                       {.name = "start_time", .type = ecs_id(ecs_f32_t)},
                       {.name = "time_scale", .type = ecs_id(ecs_f32_t)},
                   }});

  return ecs_id(TbTimeOfDayDescriptor);
}

bool tb_load_time_of_day_comp(TbWorld *world, ecs_entity_t ent,
                              const char *source_path, const cgltf_node *node,
                              json_object *object) {
  (void)source_path;
  (void)node;

  TbTimeOfDayComponent comp = {0};
  json_object_object_foreach(object, key, value) {
    if (SDL_strcmp(key, "start_time") == 0) {
      comp.time = (float)json_object_get_double(value);
    }
    if (SDL_strcmp(key, "time_scale") == 0) {
      comp.time_scale = (float)json_object_get_double(value);
    }
  }
  ecs_set_ptr(world->ecs, ent, TbTimeOfDayComponent, &comp);

  return true;
}

void tb_destroy_time_of_day_comp(TbWorld *world, ecs_entity_t ent) {
  ecs_remove(world->ecs, ent, TbTimeOfDayComponent);
}

TB_REGISTER_COMP(tb, time_of_day)
