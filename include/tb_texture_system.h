#pragma once

#include <flecs.h>

#include "texturesystem.h" // For TbTextureUsage

typedef ecs_entity_t TbTexture2; // Entities can be handles to textures
extern ECS_COMPONENT_DECLARE(TbTextureComponent2);

VkDescriptorSet tb_tex_sys_get_set2(ecs_world_t *ecs);

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
bool tb_is_texture_ready(ecs_world_t *ecs, TbTexture2 tex_ent);
