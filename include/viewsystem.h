#pragma once

#include "allocator.h"
#include "common.hlsli"
#include "tbrendercommon.h"

#define ViewSystemId 0xFEE1DEAD

typedef struct RenderSystem RenderSystem;
typedef struct TextureSystem TextureSystem;
typedef struct RenderTargetSystem RenderTargetSystem;
typedef struct SystemDescriptor SystemDescriptor;
typedef struct VkDescriptorSetLayout_T *VkDescriptorSetLayout;
typedef struct VkDescriptorPool_T *VkDescriptorPool;
typedef struct VkDescriptorSet_T *VkDescriptorSet;

typedef uint32_t TbViewId;
typedef uint32_t TbRenderTargetId;
static const TbViewId InvalidViewId = SDL_MAX_UINT32;

typedef struct ViewSystemDescriptor {
  Allocator std_alloc;
  Allocator tmp_alloc;
} ViewSystemDescriptor;

typedef struct ViewSystemFrameState {
  uint32_t set_count;
  VkDescriptorPool set_pool;
  VkDescriptorSet *sets;
} ViewSystemFrameState;

#define TB_MAX_PASSES_PER_VIEW 4

typedef struct View {
  TbRenderTargetId target;

  CommonViewData view_data;
  CommonLightData light_data;
  Frustum frustum;
} View;

typedef struct ViewSystem {
  RenderSystem *render_system;
  RenderTargetSystem *render_target_system;
  TextureSystem *texture_system;
  Allocator std_alloc;
  Allocator tmp_alloc;

  VkDescriptorSetLayout set_layout;
  ViewSystemFrameState frame_states[TB_MAX_FRAME_STATES];

  uint32_t view_count;
  View *views;
  uint32_t view_max;
} ViewSystem;

void tb_view_system_descriptor(SystemDescriptor *desc,
                               const ViewSystemDescriptor *view_desc);

TbViewId tb_view_system_create_view(ViewSystem *self);
void tb_view_system_set_view_target(ViewSystem *self, TbViewId view,
                                    TbRenderTargetId target);
void tb_view_system_set_view_data(ViewSystem *self, TbViewId view,
                                  const CommonViewData *data);
void tb_view_system_set_light_data(ViewSystem *self, TbViewId view,
                                   const CommonLightData *data);
void tb_view_system_set_view_frustum(ViewSystem *self, TbViewId view,
                                     const Frustum *frust);
VkDescriptorSet tb_view_system_get_descriptor(ViewSystem *self, TbViewId view);
const View *tb_get_view(ViewSystem *self, TbViewId view);
