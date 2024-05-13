#pragma once

#include <flecs.h>

#include "texturesystem.h" // For TbTextureUsage

typedef ecs_entity_t TbTexture2; // Entities can be handles to textures
extern ECS_COMPONENT_DECLARE(TbTextureComponent2);
#define TbInvalidTexComp ((TbTextureComponent2)0xFFFFFFFF)

// Returns the descriptor set that can be used to access textures by index from
// a shader
VkDescriptorSet tb_tex_sys_get_set2(ecs_world_t *ecs);

// Returns the image view of the given texture if it is ready
VkImageView tb_tex_sys_get_image_view2(ecs_world_t *ecs, TbTexture2 tex);

// Begins an async texture load from a given set of pixels
// The given pixles are assumed to be kept live by the caller until
// the loading task finishes
TbTexture2 tb_tex_sys_load_raw_tex(ecs_world_t *ecs, const char *name,
                                   const uint8_t *pixels, uint64_t size,
                                   uint32_t width, uint32_t height,
                                   TbTextureUsage usage);
// Begins an async texture load from a path to a given glb file, the material,
// and the texture usage so the task can open the glb file and find the expected
// image
TbTexture2 tb_tex_sys_load_mat_tex(ecs_world_t *ecs, const char *path,
                                   const char *mat_name, TbTextureUsage usage);
// Begins an async texture load from a path to a given ktx file and the texture
// usage so the task can open the ktx file and find the expected image
TbTexture2 tb_tex_sys_load_ktx_tex(ecs_world_t *ecs, const char *path,
                                   const char *name, TbTextureUsage usage);

// Returns true if the texture is ready to be used
bool tb_is_texture_ready(ecs_world_t *ecs, TbTexture2 tex_ent);

TbTexture2 tb_get_default_color_tex(ecs_world_t *ecs);
TbTexture2 tb_get_default_normal_tex(ecs_world_t *ecs);
TbTexture2 tb_get_default_metal_rough_tex(ecs_world_t *ecs);
TbTexture2 tb_get_brdf_tex(ecs_world_t *ecs);