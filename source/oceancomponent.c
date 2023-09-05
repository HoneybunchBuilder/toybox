#include "oceancomponent.h"

#include "SDL2/SDL_stdinc.h"
#include "json-c/json_object.h"
#include "json-c/linkhash.h"
#include "transformcomponent.h"
#include "world.h"

float4 make_wave(float2 dir, float steepness, float wavelength) {
  return f4(dir[0], dir[1], steepness, wavelength);
}

bool create_ocean_component(OceanComponent *comp,
                            const OceanComponentDescriptor *desc,
                            uint32_t system_dep_count,
                            System *const *system_deps) {
  (void)system_dep_count;
  (void)system_deps;
  *comp = (OceanComponent){
      .wave_count = desc->wave_count,
  };
  SDL_memcpy(comp->waves, desc->waves, sizeof(OceanWave) * desc->wave_count);

  // Creating some randomly generated but artistically driven waves
  comp->wave_count = TB_WAVE_MAX;
  OceanWave waves[TB_WAVE_MAX] = {{0}};
  float iter = 0;
  float wavelength = 76.0f;
  float steep = 0.7f;
  for (uint32_t i = 0; i < TB_WAVE_MAX; ++i) {
    float2 dir = (float2){sin(iter), cos(iter)};

    waves[i] = make_wave(dir, steep, wavelength),

    wavelength *= 0.74;
    steep *= 1.04;
    steep = clampf(steep, 0, 1);
    iter += 132.963;
  }

  SDL_memcpy(comp->waves, waves, sizeof(OceanWave) * TB_WAVE_MAX);

  return true;
}

bool deserialize_ocean_component(json_object *json, void *out_desc) {
  (void)json;
  (void)out_desc;
  /* Come back to this when we have a better solution for component markup
  OceanComponentDescriptor *desc = (OceanComponentDescriptor *)out_desc;
  desc->wave_count = 1;
  OceanWave *wave = &desc->waves[0];
  json_object_object_foreach(json, key, value) {
    if (SDL_strcmp(key, "steepness") == 0) {
      (*wave)[2] = (float)json_object_get_double(value);
    } else if (SDL_strcmp(key, "wavelength") == 0) {
      (*wave)[3] = (float)json_object_get_double(value);
    } else if (SDL_strcmp(key, "direction_x") == 0) {
      (*wave)[0] = (float)json_object_get_double(value);
    } else if (SDL_strcmp(key, "direction_y") == 0) {
      (*wave)[1] = (float)json_object_get_double(value);
    }
  }
  */
  return true;
}

void destroy_ocean_component(OceanComponent *comp, uint32_t system_dep_count,
                             System *const *system_deps) {
  (void)system_dep_count;
  (void)system_deps;
  *comp = (OceanComponent){0};
}

TB_DEFINE_COMPONENT(ocean, OceanComponent, OceanComponentDescriptor)

void tb_ocean_component_descriptor(ComponentDescriptor *desc) {
  *desc = (ComponentDescriptor){
      .name = "Ocean",
      .size = sizeof(OceanComponent),
      .desc_size = sizeof(OceanComponentDescriptor),
      .id = OceanComponentId,
      .id_str = OceanComponentIdStr,
      .create = tb_create_ocean_component,
      .deserialize = deserialize_ocean_component,
      .destroy = tb_destroy_ocean_component,
  };
}

float gerstner_wave(OceanWave wave, float time, float2 pos) {
  float steepness = wave[2];
  float k = 2 * PI / wave[3];
  float c = sqrt(9.8 / k);
  float2 d = normf2((float2){wave[0], wave[1]});
  float f = k * (dotf2(d, pos) - c * time);
  float a = steepness / k;

  float sinf = SDL_sinf(f);

  return a * sinf;
}

float iter_wave_height(float2 pos, const OceanComponent *ocean) {
  float time = ocean->time;
  uint32_t count = (uint32_t)ocean->wave_count;
  if (count > TB_WAVE_MAX) {
    count = TB_WAVE_MAX;
  }

  float weight = 1.0f;
  float time_mul = 1.0f;
  float value_sum = 0.0f;
  float weight_sum = 0.0f;

  for (uint32_t i = 0; i < count; ++i) {
    float wave = gerstner_wave(ocean->waves[i], time * time_mul, pos);

    value_sum += wave * weight;
    weight_sum += weight;

    wave *= 0.82;
    time_mul *= 1.09;
  }
  return value_sum / weight_sum;
}

float3 calc_wave_pos(float2 pos, const OceanComponent *ocean) {
  float H = iter_wave_height(pos, ocean);
  return (float3){pos[0], H, pos[1]};
}

float3 calc_wave_normal(float2 pos, const OceanComponent *ocean) {
  float e = 0.01;
  float2 ex = (float2){e, 0};
  float H = iter_wave_height(pos, ocean);
  float3 a = (float3){pos[0], H, pos[1]};
  return normf3(crossf3(
      a - (float3){pos[0] - e, iter_wave_height(pos - ex, ocean), pos[1]},
      a - (float3){pos[0], iter_wave_height(pos + (float2){0, e}, ocean),
                   pos[1] + e}));
}

OceanSample tb_sample_ocean(const OceanComponent *ocean,
                            TransformComponent *transform, float2 pos) {
  float4x4 mat = tb_transform_get_world_matrix(transform);

  uint32_t wave_count = ocean->wave_count;
  if (wave_count > TB_WAVE_MAX) {
    wave_count = TB_WAVE_MAX;
  }

  float3 p = f4tof3(mulf44(mat, (float4){pos[0], 0, pos[1], 1}));
  pos = (float2){p[0], p[2]};

  OceanSample sample = {
      .pos = calc_wave_pos(pos, ocean),
      .normal = calc_wave_normal(pos, ocean),

  };
  return sample;
}
