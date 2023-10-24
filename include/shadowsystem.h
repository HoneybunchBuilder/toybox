#pragma once

#include "allocator.h"

#define ShadowSystemId 0xB105F00D

typedef struct TbWorld TbWorld;
typedef uint32_t TbDrawContextId;

typedef struct VkPipelineLayout_T *VkPipelineLayout;
typedef struct VkPipeline_T *VkPipeline;

typedef struct ecs_world_t ecs_world_t;
typedef struct ecs_query_t ecs_query_t;

typedef struct ShadowSystem {
  Allocator std_alloc;
  Allocator tmp_alloc;

  TbDrawContextId draw_ctx;
  VkPipelineLayout pipe_layout;
  VkPipeline pipeline;

  ecs_query_t *dir_light_query;
} ShadowSystem;

void tb_register_shadow_sys(TbWorld *world);
void tb_unregister_shadow_sys(ecs_world_t *ecs);
