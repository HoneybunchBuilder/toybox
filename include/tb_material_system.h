#pragma once

#include "rendersystem.h"

#include <flecs.h>

// HACK: +2 because this needs to be after the texture system
#define TB_MAT_SYS_PRIO (TB_RND_SYS_PRIO + 2)

typedef ecs_entity_t TbMaterial2; // Entities can be handles to materials

typedef struct cgltf_material cgltf_material;

// Material usage maps a material to the expected shader layout and usage
// Similar to unreal's Material Domain concept
typedef enum TbMaterialUsage {
  TB_MAT_USAGE_UNKNOWN = 0,
  TB_MAT_USAGE_SCENE,
  TB_MAT_USAGE_EFFECTS,
  TB_MAT_USAGE_POSTPROCESSING,
  TB_MAT_USAGE_CUSTOM,
} TbMaterialUsage;
extern ECS_COMPONENT_DECLARE(TbMaterialUsage);

// A function that parses a material asset and fills out a pointer to a block
// of memory that represents that material
typedef bool TbMatParseFn(ecs_world_t *ecs, const char *path, const char *name,
                          const cgltf_material *material, void *out_mat_data);

bool tb_register_mat_usage(ecs_world_t *ecs, const char *domain_name,
                           TbMaterialUsage usage, TbMatParseFn parse_fn,
                           void *default_data, size_t size);

// Begins an async material load from a path to a given glb file and the name of
// the material to load
TbMaterial2 tb_mat_sys_load_gltf_mat(ecs_world_t *ecs, const char *path,
                                     const char *name, TbMaterialUsage usage);

// Returns true if the material is ready to be used
bool tb_is_material_ready(ecs_world_t *ecs, TbMaterial2 mat_ent);

TbMaterial2 tb_get_default_mat(ecs_world_t *ecs, TbMaterialUsage usage);
