#include "oceancomponent.h"

#include "assetsystem.h"
#include "oceansystem.h"
#include "transformcomponent.h"
#include "world.h"

#include <SDL2/SDL_stdinc.h>
#include <flecs.h>
#include <json.h>

float4 make_wave(float2 dir, float steepness, float wavelength) {
  return tb_f4(dir.x, dir.y, steepness, wavelength);
}

TbOceanComponent create_ocean_component_internal(void) {
  TbOceanComponent comp = {
      .wave_count = TB_WAVE_MAX,
  };

  // Creating some randomly generated but artistically driven waves
  TbOceanWave waves[TB_WAVE_MAX] = {{0}};
  float iter = 0;
  float wavelength = 48.0f;
  float steep = 0.03;
  for (uint32_t i = 0; i < TB_WAVE_MAX; ++i) {
    float2 dir = (float2){SDL_sinf(iter), SDL_cosf(iter)};

    waves[i] = make_wave(dir, steep, wavelength),

    wavelength *= 0.68;
    steep *= 1.04;
    steep = tb_clampf(steep, 0, 1);
    iter += 1323.963;
  }
  SDL_memcpy(comp.waves, waves, sizeof(TbOceanWave) * TB_WAVE_MAX);

  return comp;
}

bool create_ocean_component(ecs_world_t *ecs, ecs_entity_t e,
                            const char *source_path, const cgltf_node *node,
                            json_object *extra) {
  (void)source_path;
  (void)node;
  if (extra) {
    json_object_object_foreach(extra, key, value) {
      if (SDL_strcmp(key, "id") == 0) {
        const char *id_str = json_object_get_string(value);
        if (SDL_strcmp(id_str, TbOceanComponentIdStr) == 0) {
          ECS_COMPONENT(ecs, TbOceanComponent);
          TbOceanComponent comp = create_ocean_component_internal();
          ecs_set_ptr(ecs, e, TbOceanComponent, &comp);
        }
      }
    }
  }
  return true;
}

void destroy_ocean_components(ecs_world_t *ecs) {
  ECS_COMPONENT(ecs, TbOceanComponent);

  ecs_filter_t *filter =
      ecs_filter(ecs, {
                          .terms =
                              {
                                  {.id = ecs_id(TbOceanComponent)},
                              },
                      });

  ecs_iter_t it = ecs_filter_iter(ecs, filter);
  while (ecs_filter_next(&it)) {
    TbOceanComponent *comp = ecs_field(&it, TbOceanComponent, 1);
    for (int32_t i = 0; i < it.count; ++i) {
      *comp = (TbOceanComponent){0};
    }
  }
  ecs_filter_fini(filter);
}

void tb_register_ocean_component(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;
  ECS_COMPONENT(ecs, TbAssetSystem);
  ECS_COMPONENT(ecs, TbOceanSystem);

  // Register asset system for parsing ocean components
  TbAssetSystem asset = {
      .add_fn = create_ocean_component,
      .rem_fn = destroy_ocean_components,
  };
  ecs_set_ptr(ecs, ecs_id(TbOceanSystem), TbAssetSystem, &asset);
}

// Simplified from the one in ocean.hlsli to not bother wtih tangent and
// bitangent
TbOceanSample gerstner_wave(TbOceanWave wave, TbOceanSample sample,
                            float time) {
  float3 p = sample.pos;

  float steepness = wave.z;
  float k = 2.0f * TB_PI / wave.w;
  float c = SDL_sqrtf(9.8f / k);
  float2 d = tb_normf2(wave.xy);
  float f = k * (tb_dotf2(d, p.xz) - c * time);
  float a = steepness / k;

  float sinf = SDL_sinf(f);
  float cosf = SDL_cosf(f);

  p = (float3){d.x * (a * cosf), a * sinf, d.y * (a * cosf)};
  float3 t = {-d.x * d.x * (steepness * sinf), d.x * (steepness * cosf),
              -d.x * d.y * (steepness * sinf)};
  float3 b = {-d.x * d.y * (steepness * sinf), d.y * (steepness * cosf),
              -d.y * d.y * (steepness * sinf)};

  return (TbOceanSample){
      .pos = sample.pos + p,
      .tangent = sample.tangent + t,
      .binormal = sample.binormal + b,
  };
}

TbOceanSample tb_sample_ocean(const TbOceanComponent *ocean, ecs_world_t *ecs,
                              TbTransformComponent *transform, float2 pos) {
  float4x4 mat = tb_transform_get_world_matrix(ecs, transform);

  uint32_t wave_count = ocean->wave_count;
  if (wave_count > TB_WAVE_MAX) {
    wave_count = TB_WAVE_MAX;
  }

  TbOceanSample sample = {
      .pos = tb_f4tof3(tb_mulf44f4(mat, (float4){pos.x, 0, pos.y, 1})),
      .tangent = TB_RIGHT,
      .binormal = TB_FORWARD,
  };
  for (uint32_t i = 0; i < wave_count; ++i) {
    sample = gerstner_wave(ocean->waves[i], sample, ocean->time);
  }
  sample.tangent = tb_normf3(sample.tangent);
  sample.binormal = tb_normf3(sample.binormal);

  return sample;
}
