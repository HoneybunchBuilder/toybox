#pragma once

#include "allocator.h"
#include "common.hlsli"
#include "rendersystem.h"

typedef struct TbWorld TbWorld;
typedef uint32_t TbDrawContextId;

typedef struct VkPipelineLayout_T *VkPipelineLayout;
typedef struct VkPipeline_T *VkPipeline;

typedef struct ecs_world_t ecs_world_t;
typedef struct ecs_query_t ecs_query_t;

typedef struct TbShadowSystem {
  TbAllocator std_alloc;
  TbAllocator tmp_alloc;

  TbDrawContextId draw_ctxs[TB_CASCADE_COUNT];
  VkPipelineLayout pipe_layout;
  VkPipeline pipeline;

  ecs_query_t *dir_light_query;
  TbFrameDescriptorPoolList desc_pool_list;
} TbShadowSystem;

void tb_register_shadow_sys(TbWorld *world);
void tb_unregister_shadow_sys(TbWorld *world);
