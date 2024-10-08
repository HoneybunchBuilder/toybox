#pragma once

#include "tb_allocator.h"
#include "tb_common.slangh"
#include "tb_descriptor_buffer.h"
#include "tb_dynarray.h"
#include "tb_lighting.slangh"
#include "tb_render_common.h"
#include "tb_render_pipeline_system.h"
#include "tb_texture_system.h"
#include "tb_view.slangh"

#include <flecs.h>

#define TB_VIEW_SYS_PRIO (TB_TEX_SYS_PRIO + 1)

typedef struct TbRenderSystem TbRenderSystem;
typedef struct TbTextureSystem TbTextureSystem;
typedef struct TbRenderTargetSystem TbRenderTargetSystem;
typedef struct VkDescriptorSetLayout_T *VkDescriptorSetLayout;
typedef struct VkDescriptorPool_T *VkDescriptorPool;
typedef struct VkDescriptorSet_T *VkDescriptorSet;
typedef struct TbWorld TbWorld;
typedef uint32_t TbViewId;
typedef uint32_t TbRenderTargetId;
static const TbViewId TbInvalidViewId = SDL_MAX_UINT32;

typedef struct TbViewSystemFrameState {
  uint32_t set_count;
  VkDescriptorPool set_pool;
  VkDescriptorSet *sets;
} TbViewSystemFrameState;

#define TB_MAX_PASSES_PER_VIEW 4

typedef struct TbView {
  TbRenderTargetId target;
  TbViewData view_data;
  TbLightData light_data;
  TbFrustum frustum;
  TbDescriptorBuffer desc_buffer;
} TbView;

typedef struct TbViewSystem {
  TbRenderSystem *rnd_sys;
  TbRenderTargetSystem *rt_sys;
  TbAllocator gp_alloc;
  TbAllocator tmp_alloc;

  VkSampler brdf_sampler;
  VkSampler filtered_env_sampler;
  VkDescriptorSetLayout set_layout;
  TbViewSystemFrameState frame_states[TB_MAX_FRAME_STATES];

  VkDescriptorSetLayout set_layout2;
  TbDescriptorBuffer desc_buffer;

  TB_DYN_ARR_OF(TbView) views;
} TbViewSystem;
extern ECS_COMPONENT_DECLARE(TbViewSystem);

TbViewId tb_view_system_create_view(TbViewSystem *self);
void tb_view_system_set_view_target(TbViewSystem *self, TbViewId view,
                                    TbRenderTargetId target);
void tb_view_system_set_view_data(TbViewSystem *self, TbViewId view,
                                  const TbViewData *data);
void tb_view_system_set_light_data(TbViewSystem *self, TbViewId view,
                                   const TbLightData *data);
void tb_view_system_set_view_frustum(TbViewSystem *self, TbViewId view,
                                     const TbFrustum *frust);
VkDescriptorSet tb_view_system_get_descriptor(TbViewSystem *self,
                                              TbViewId view);
VkDescriptorSet tb_view_sys_get_set(TbViewSystem *self);
const TbView *tb_get_view(TbViewSystem *self, TbViewId view);

VkDescriptorSetLayout tb_view_sys_get_set_layout(ecs_world_t *ecs);
VkDescriptorBufferBindingInfoEXT tb_view_sys_get_table_addr(ecs_world_t *ecs,
                                                            TbViewId view);
