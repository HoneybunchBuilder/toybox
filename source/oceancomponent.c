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

  // Creating some randomly generated but artistically driven waves
  comp->wave_count = TB_WAVE_MAX;
  OceanWave waves[TB_WAVE_MAX] = {{0}};
  float iter = 0;
  float wavelength = 512.0f;
  float steep = 0.25f;
  for (uint32_t i = 0; i < TB_WAVE_MAX; ++i) {
    float2 dir = (float2){SDL_sinf(iter), SDL_cosf(iter)};

    waves[i] = make_wave(dir, steep, wavelength),

    wavelength *= 0.65;
    steep *= 1.13;
    steep = clampf(steep, 0, 1);
    iter += 1323.963;
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

// Simplified from the one in ocean.hlsli to not bother wtih tangent and
// bitangent
void gerstner_wave(OceanWave wave, float time, float2 pos, float *height,
                   float3 *tangent, float3 *binormal) {
  float steepness = wave[2];
  float k = 2.0f * PI / wave[3];
  float c = SDL_sqrtf(9.8f / k);
  float2 d = normf2(f2(wave[0], wave[1]));
  float f = k * (dotf2(d, (float2){pos[0], pos[1]}) - c * time);
  float a = steepness / k;

  float sinf = SDL_sinf(f);
  float cosf = SDL_cosf(f);

  *tangent =
      (float3){-d[0] * d[0] * (steepness * sinf), d[0] * (steepness * cosf),
               -d[0] * d[1] * (steepness * sinf)};
  *binormal =
      (float3){-d[0] * d[1] * (steepness * sinf), d[1] * (steepness * cosf),
               -d[1] * d[1] * (steepness * sinf)};

  *height = a * sinf;
}

OceanSample tb_sample_ocean(const OceanComponent *ocean,
                            TransformComponent *transform, float2 pos) {
  float4x4 mat = tb_transform_get_world_matrix(transform);

  uint32_t wave_count = ocean->wave_count;
  if (wave_count > TB_WAVE_MAX) {
    wave_count = TB_WAVE_MAX;
  }

  float height = {0};
  float3 tangent = TB_RIGHT;
  float3 binormal = TB_FORWARD;

  float3 world_pos = f4tof3(mulf44(mat, (float4){pos[0], 0, pos[1], 1}));
  float2 p = f2(world_pos[0], world_pos[2]);

  float weight = 1.0f;
  float time_mul = 1.0f;
  float weight_sum = 0.0f;
  for (uint32_t i = 0; i < wave_count; ++i) {
    float h = 0;
    float3 t = {0};
    float3 b = {0};
    gerstner_wave(ocean->waves[i], ocean->time * time_mul, p, &h, &t, &b);

    height += h * weight;
    tangent += t;
    binormal += b;
    weight_sum += weight;

    weight *= 0.82;
    time_mul *= 1.07;
  }
  height = height / weight_sum;

  OceanSample sample = {
      .pos = (float3){p[0], height, p[2]},
      .tangent = normf3(tangent),
      .binormal = normf3(binormal),
  };

  return sample;
}
