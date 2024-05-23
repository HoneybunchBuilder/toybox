#pragma once

#include "tb_render_system.h"

#include <flecs.h>

// HACK: +2 because this needs to be after the texture system
#define TB_MAT_SYS_PRIO (TB_RND_SYS_PRIO + 2)

typedef ecs_entity_t TbMaterial2; // Entities can be handles to materials
typedef uint32_t TbMaterialPerm;
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

typedef struct TbMaterialData {
  TbBuffer gpu_buffer;
  void *domain_data;
} TbMaterialData;
extern ECS_COMPONENT_DECLARE(TbMaterialData);

typedef uint32_t TbMaterialComponent;
extern ECS_COMPONENT_DECLARE(TbMaterialComponent);

// A function that parses a material asset and fills out a pointer to a block
// of memory that represents that material
typedef bool TbMatParseFn(const char *path, const char *name,
                          const cgltf_material *material, void **out_mat_data);
typedef void TbMatOnLoadFn(ecs_world_t *ecs, void *mat_data);
typedef bool TbMatIsReadyFn(ecs_world_t *ecs, const TbMaterialData *data);
typedef void *TbMatGetDataFn(ecs_world_t *ecs, const TbMaterialData *data);
typedef size_t TbMatGetSizeFn(void);
typedef bool TbMatIsTransparent(const TbMaterialData *data);

typedef struct TbMaterialDomain {
  TbMatParseFn *parse_fn;
  TbMatOnLoadFn *load_fn;
  TbMatIsReadyFn *ready_fn;
  TbMatGetDataFn *get_data_fn;
  TbMatGetSizeFn *get_size_fn;
  TbMatIsTransparent *is_trans_fn;
} TbMaterialDomain;

bool tb_register_mat_usage(ecs_world_t *ecs, const char *domain_name,
                           TbMaterialUsage usage, TbMaterialDomain domain,
                           void *default_data, size_t size);

VkDescriptorSetLayout tb_mat_sys_get_set_layout(ecs_world_t *ecs);

VkDescriptorSet tb_mat_sys_get_set(ecs_world_t *ecs);

// Begins an async material load from a path to a given glb file and the name of
// the material to load
TbMaterial2 tb_mat_sys_load_gltf_mat(ecs_world_t *ecs, const char *path,
                                     const char *name, TbMaterialUsage usage);

// Returns true if the material is ready to be used
bool tb_is_material_ready(ecs_world_t *ecs, TbMaterial2 mat_ent);

bool tb_is_mat_transparent(ecs_world_t *ecs, TbMaterial2 mat_ent);

TbMaterial2 tb_get_default_mat(ecs_world_t *ecs, TbMaterialUsage usage);
