#pragma once

#include "SDL3/SDL_stdinc.h"
#include "gltf.hlsli"
#include "tb_allocator.h"
#include "tb_common.h"
#include "tb_dynarray.h"
#include "tb_mesh_component.h"
#include "tb_render_common.h"
#include "tb_render_system.h"
#include "tb_render_target_system.h"
#include "tb_view_system.h"

#include <flecs.h>

#ifndef TB_MESH_SYS_PRIO
#define TB_MESH_SYS_PRIO (TB_RP_SYS_PRIO + 1)
#endif

typedef ecs_entity_t TbMesh2;
typedef struct ecs_query_t ecs_query_t;

typedef uint32_t TbMeshComponent2;
extern ECS_COMPONENT_DECLARE(TbMeshComponent2);

VkDescriptorSetLayout tb_mesh_sys_get_set_layout(ecs_world_t *ecs);

VkDescriptorSet tb_mesh_sys_get_pos_set(ecs_world_t *ecs);
VkDescriptorSet tb_mesh_sys_get_norm_set(ecs_world_t *ecs);
VkDescriptorSet tb_mesh_sys_get_tan_set(ecs_world_t *ecs);
VkDescriptorSet tb_mesh_sys_get_uv0_set(ecs_world_t *ecs);

TbMesh2 tb_mesh_sys_load_gltf_mesh(ecs_world_t *ecs, const char *path,
                                   const char *name);

bool tb_is_mesh_ready(ecs_world_t *ecs, TbMesh2 mesh_ent);
