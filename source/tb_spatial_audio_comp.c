#include "tb_spatial_audio_comp.h"

#include "tb_common.h"
#include "tb_world.h"

#include <flecs.h>
#include <json.h>
#include <phonon.h>

typedef struct TbSpatialAudioSourceDesc {
  char *file_path; // Path to the wav file
} TbSpatialAudioSourceDesc;

typedef struct TbSpatialAudioSource {
  float *raw_source;         // Raw PCM stream loaded into application memory
  IPLAudioBuffer ipl_source; // IPL view of the mono PCM stream
} TbSpatialAudioSource;

ECS_COMPONENT_DECLARE(TbSpatialAudioSourceDesc);
ECS_COMPONENT_DECLARE(TbSpatialAudioSource);

bool tb_load_spatial_audio_source_comp(ecs_world_t *ecs, ecs_entity_t ent,
                                       const char *source_path,
                                       const cgltf_data *data,
                                       const cgltf_node *node,
                                       json_object *json) {
  (void)source_path;
  (void)data;
  (void)node;
  char *file_path = NULL;

  TbSpatialAudioSource comp = {0};

  tb_auto world = ecs_singleton_get(ecs, TbWorldRef)->world;

  json_object_object_foreach(json, key, value) {
    // Determine file path
    if (SDL_strcmp(key, "file_path") == 0) {
      const int32_t path_len = json_object_get_string_len(value);
      const int32_t path_size = path_len + 1; // Null terminator
      file_path = tb_alloc_nm_tp(world->gp_alloc, path_size, char);
      SDL_strlcpy(file_path, json_object_get_string(value), path_len);
    }
  }
  TB_CHECK_RETURN(file_path != NULL, "Invalid file path", false);

  // TODO: Start task to load the audio file

  ecs_set_ptr(ecs, ent, TbSpatialAudioSource, &comp);
  return true;
}

ecs_entity_t tb_register_spatial_audio_source_comp(TbWorld *world) {
  tb_auto ecs = world->ecs;
  ECS_COMPONENT_DEFINE(ecs, TbSpatialAudioSource);
  ECS_COMPONENT_DEFINE(ecs, TbSpatialAudioSourceDesc);

  ecs_struct(ecs, {.entity = ecs_id(TbSpatialAudioSourceDesc),
                   .members = {
                       {.name = "file_path", .type = ecs_id(ecs_string_t)},
                   }});

  return ecs_id(TbSpatialAudioSourceDesc);
}

TB_REGISTER_COMP(tb, spatial_audio_source)
