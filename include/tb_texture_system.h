#pragma once

#include "tb_render_system.h"

#include <flecs.h>

#define TB_TEX_SYS_PRIO (TB_RND_SYS_PRIO + 1)

typedef struct VkDescriptorSetLayout_T *VkDescriptorSetLayout;
typedef struct VkDescriptorSet_T *VkDescriptorSet;

typedef ecs_entity_t TbTexture; // Entities can be handles to textures
typedef struct cgltf_data cgltf_data;

typedef uint32_t TbTextureComponent;
extern ECS_COMPONENT_DECLARE(TbTextureComponent);

typedef enum TbTextureUsage {
  TB_TEX_USAGE_UNKNOWN = 0,
  TB_TEX_USAGE_COLOR,
  TB_TEX_USAGE_NORMAL,
  TB_TEX_USAGE_METAL_ROUGH,
  TB_TEX_USAGE_BRDF,
} TbTextureUsage;

VkDescriptorSetLayout tb_tex_sys_get_set_layout(ecs_world_t *ecs);
VkDescriptorSetLayout tb_tex_sys_get_set_layout2(ecs_world_t *ecs);

// Returns the descriptor set that can be used to access textures by index from
// a shader
VkDescriptorSet tb_tex_sys_get_set(ecs_world_t *ecs);

// Returns the address of the texture system's descriptor buffer
VkDescriptorBufferBindingInfoEXT
tb_tex_sys_get_tex_table_addr(ecs_world_t *ecs);

// Returns the image view of the given texture if it is ready
VkImageView tb_tex_sys_get_image_view2(ecs_world_t *ecs, TbTexture tex);

void tb_tex_sys_begin_load(ecs_world_t *ecs);

// Begins an async texture load from a given set of pixels
// The given pixles are assumed to be kept live by the caller until
// the loading task finishes
TbTexture tb_tex_sys_load_raw_tex(ecs_world_t *ecs, const char *name,
                                  const uint8_t *pixels, uint64_t size,
                                  uint32_t width, uint32_t height,
                                  TbTextureUsage usage);
// Begins an async texture load from a loaded glb file, the material,
// and the texture usage so the task can parse the gltf data and find the
// expected image
TbTexture tb_tex_sys_load_mat_tex(ecs_world_t *ecs, const cgltf_data *data,
                                  const char *mat_name, TbTextureUsage usage);
// Begins an async texture load from a path to a given ktx file and the texture
// usage so the task can open the ktx file and find the expected image
TbTexture tb_tex_sys_load_ktx_tex(ecs_world_t *ecs, const char *path,
                                  const char *name, TbTextureUsage usage);

// Returns true if the texture is ready to be used
bool tb_is_texture_ready(ecs_world_t *ecs, TbTexture tex_ent);

TbTexture tb_get_default_color_tex(ecs_world_t *ecs);
TbTexture tb_get_default_normal_tex(ecs_world_t *ecs);
TbTexture tb_get_default_metal_rough_tex(ecs_world_t *ecs);
TbTexture tb_get_brdf_tex(ecs_world_t *ecs);
