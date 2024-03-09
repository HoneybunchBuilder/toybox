#include "oceancomponent.h"

#include "oceansystem.h"
#include "tbcommon.h"
#include "transformcomponent.h"
#include "world.h"

#include <SDL3/SDL_stdinc.h>
#include <flecs.h>
#include <json.h>

ECS_COMPONENT_DECLARE(TbOceanComponent);

ECS_COMPONENT_DECLARE(TbOceanWave);
typedef struct TbOceanDescriptor {
  int32_t wave_count;
  TbOceanWave waves[8];
} TbOceanDescriptor;
ECS_COMPONENT_DECLARE(TbOceanDescriptor);

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
                              ecs_entity_t entity, float2 pos) {
  float4x4 mat = tb_transform_get_world_matrix(ecs, entity);

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

ecs_entity_t tb_register_ocean_comp(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;
  ECS_COMPONENT_DEFINE(ecs, TbOceanComponent);
  ECS_COMPONENT_DEFINE(ecs, TbOceanWave);
  ECS_COMPONENT_DEFINE(ecs, TbOceanDescriptor);

  ecs_struct(ecs, {
                      .entity = ecs_id(TbOceanWave),
                      .members =
                          {
                              {.name = "dir_x", .type = ecs_id(ecs_f32_t)},
                              {.name = "dir_y", .type = ecs_id(ecs_f32_t)},
                              {.name = "steepness", .type = ecs_id(ecs_f32_t)},
                              {.name = "wavelength", .type = ecs_id(ecs_f32_t)},
                          },
                  });
  ecs_struct(
      ecs, {
               .entity = ecs_id(TbOceanDescriptor),
               .members =
                   {
                       {.name = "wave_count", .type = ecs_id(ecs_u32_t)},
                       {.name = "waves",
                        .type = ecs_vector(ecs, {.type = ecs_id(TbOceanWave)})},
                   },
           });
  return ecs_id(TbOceanDescriptor);
}

bool tb_load_ocean_comp(TbWorld *world, ecs_entity_t ent,
                        const char *source_path, const cgltf_node *node,
                        json_object *json) {
  (void)source_path;
  (void)node;
  (void)json;
  tb_auto ecs = world->ecs;
  TbOceanComponent comp = create_ocean_component_internal();
  ecs_set_ptr(ecs, ent, TbOceanComponent, &comp);
  return true;
}

void tb_destroy_ocean_comp(TbWorld *world, ecs_entity_t ent) {
  ecs_remove(world->ecs, ent, TbOceanComponent);
}

TB_REGISTER_COMP(tb, ocean)
