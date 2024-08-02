#pragma once

#define PREFILTER_PASS_COUNT 10

#include "tb_render_common.h"
#include "tb_view_system.h"

#include <flecs.h>

typedef ecs_entity_t TbShader;

#define TB_SKY_SYS_PRIO (TB_RP_SYS_PRIO + 1)

typedef struct TbRenderSystem TbRenderSystem;
typedef struct TbRenderPipelineSystem TbRenderPipelineSystem;
typedef struct TbRenderTargetSystem TbRenderTargetSystem;
typedef struct TbViewSystem TbViewSystem;

typedef uint32_t TbDrawContextId;
typedef struct VkDescriptorSetLayout_T *VkDescriptorSetLayout;

typedef struct TbWorld TbWorld;
typedef struct ecs_query_t ecs_query_t;

typedef struct TbSkySystem {
  TbRenderSystem *rnd_sys;
  TbRenderPipelineSystem *rp_sys;
  TbRenderTargetSystem *rt_sys;
  TbViewSystem *view_sys;
  TbAllocator gp_alloc;
  TbAllocator tmp_alloc;

  ecs_query_t *camera_query;

  float time;

  TbFrameDescriptorPoolList pools;

  TbDrawContextId sky_draw_ctx;
  TbDrawContextId env_capture_ctxs[PREFILTER_PASS_COUNT];
  TbDrawContextId irradiance_ctx;
  TbDrawContextId prefilter_ctxs[PREFILTER_PASS_COUNT];

  VkSampler irradiance_sampler;
  VkDescriptorSetLayout sky_set_layout;
  VkDescriptorSetLayout irr_set_layout;
  VkPipelineLayout sky_pipe_layout;
  VkPipelineLayout irr_pipe_layout;
  VkPipelineLayout prefilter_pipe_layout;

  TbShader sky_shader;
  TbShader env_shader;
  TbShader irradiance_shader;
  TbShader prefilter_shader;

  TbBuffer sky_geom_gpu_buffer;
  TbDescriptorBuffer sky_desc_buffer;
  TbDescriptorBuffer irr_desc_buffer;
} TbSkySystem;
extern ECS_COMPONENT_DECLARE(TbSkySystem);
