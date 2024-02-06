#pragma once

#include "allocator.h"
#include "common.hlsli"
#include "dynarray.h"
#include "tbrendercommon.h"

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
  TbCommonViewData view_data;
  TbCommonLightData light_data;
  TbFrustum frustum;
} TbView;

typedef struct TbViewSystem {
  TbRenderSystem *rnd_sys;
  TbRenderTargetSystem *rt_sys;
  TbTextureSystem *texture_system;
  TbAllocator gp_alloc;
  TbAllocator tmp_alloc;

  VkSampler brdf_sampler;
  VkSampler filtered_env_sampler;
  VkDescriptorSetLayout set_layout;
  TbViewSystemFrameState frame_states[TB_MAX_FRAME_STATES];

  TB_DYN_ARR_OF(TbView) views;
} TbViewSystem;

void tb_register_view_sys(TbWorld *world);
void tb_unregister_view_sys(TbWorld *world);

TbViewId tb_view_system_create_view(TbViewSystem *self);
void tb_view_system_set_view_target(TbViewSystem *self, TbViewId view,
                                    TbRenderTargetId target);
void tb_view_system_set_view_data(TbViewSystem *self, TbViewId view,
                                  const TbCommonViewData *data);
void tb_view_system_set_light_data(TbViewSystem *self, TbViewId view,
                                   const TbCommonLightData *data);
void tb_view_system_set_view_frustum(TbViewSystem *self, TbViewId view,
                                     const TbFrustum *frust);
VkDescriptorSet tb_view_system_get_descriptor(TbViewSystem *self,
                                              TbViewId view);
VkDescriptorSet tb_view_sys_get_set(TbViewSystem *self);
const TbView *tb_get_view(TbViewSystem *self, TbViewId view);
