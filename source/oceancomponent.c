#include "oceancomponent.h"

#include "SDL2/SDL_stdinc.h"
#include "json-c/json_object.h"
#include "json-c/linkhash.h"
#include "transformcomponent.h"
#include "world.h"

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
  return true;
}

bool deserialize_ocean_component(json_object *json, void *out_desc) {
  OceanComponentDescriptor *desc = (OceanComponentDescriptor *)out_desc;
  desc->wave_count = 1;
  OceanWave *wave = &desc->waves[0];
  json_object_object_foreach(json, key, value) {
    if (SDL_strcmp(key, "steepness") == 0) {
      wave->steepness = (float)json_object_get_double(value);
    } else if (SDL_strcmp(key, "wavelength") == 0) {
      wave->wavelength = (float)json_object_get_double(value);
    } else if (SDL_strcmp(key, "direction_x") == 0) {
      wave->direction[0] = (float)json_object_get_double(value);
    } else if (SDL_strcmp(key, "direction_y") == 0) {
      wave->direction[1] = (float)json_object_get_double(value);
    }
  }
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

// Simplified from the one in ocean.hlsli to not bother wtih tangent and
// bitangent
float3 gerstner_wave(OceanWave wave, float3 p, float time) {
  float steepness = wave.steepness;
  float k = 2.0f * PI / wave.wavelength;
  float c = SDL_sqrtf(9.8f / k);
  float2 d = normf2(wave.direction);
  float f = k * (dotf2(d, (float2){p[0], p[2]}) - c * time);
  float a = steepness / k;

  float sinf = SDL_sinf(f);
  float cosf = SDL_cosf(f);

  return (float3){d[0] * (a * cosf), a * sinf, d[1] * (a * cosf)};
}

float tb_ocean_sample_height(const OceanComponent *ocean,
                             TransformComponent *transform, float2 pos) {
  OceanWave wave_0 = {0.4, 64, (float2){0.8, -1}};
  OceanWave wave_1 = {0.3, 24, (float2){-1, 0.6}};
  OceanWave wave_2 = {0.25, 16, (float2){0.2, 3}};
  OceanWave wave_3 = {0.2, 10, (float2){0.5, 1.7}};
  OceanWave wave_4 = {0.15, 8, (float2){-0.6, .84}};

  float4x4 mat = tb_transform_get_world_matrix(transform);

  uint32_t wave_count = 5;
  OceanWave waves[] = {wave_0, wave_1, wave_2, wave_3, wave_4};

  float3 wave_pos = f4tof3(mul4f44f((float4){pos[0], pos[1], 0, 1}, mat));
  for (uint32_t i = 0; i < wave_count; ++i) {
    wave_pos += gerstner_wave(waves[i], wave_pos, ocean->time);
  }
  return wave_pos[2];
}
