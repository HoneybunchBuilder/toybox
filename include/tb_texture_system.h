#pragma once

#include <flecs.h>

#include "texturesystem.h" // For TbTextureUsage

typedef ecs_entity_t TbTexture2; // Entities can be handles to textures
extern ECS_COMPONENT_DECLARE(TbTextureComponent2);

TbTexture2 tb_tex_sys_load_mat_tex(ecs_world_t *ecs, const char *path,
                                   const char *mat_name, TbTextureUsage usage);
bool tb_is_tex_loaded(ecs_world_t *ecs, ecs_entity_t tex_ent);
