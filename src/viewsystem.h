#pragma once

#include "allocator.h"
#include "tbrendercommon.h"

#define ViewSystemId 0xFEE1DEAD

typedef struct RenderSystem RenderSystem;
typedef struct SystemDescriptor SystemDescriptor;
typedef struct VkDescriptorSetLayout_T *VkDescriptorSetLayout;
typedef struct VkDescriptorPool_T *VkDescriptorPool;
typedef struct VkDescriptorSet_T *VkDescriptorSet;
typedef struct CommonViewData CommonViewData;

typedef uint64_t TbViewId;
static const uint64_t InvalidViewId = SDL_MAX_UINT64;

typedef struct ViewSystemDescriptor {
  Allocator std_alloc;
  Allocator tmp_alloc;
} ViewSystemDescriptor;

typedef struct ViewSystemFrameState {
  uint32_t set_count;
  VkDescriptorPool set_pool;
  VkDescriptorSet *sets;
} ViewSystemFrameState;

typedef struct ViewSystem {
  RenderSystem *render_system;
  Allocator std_alloc;
  Allocator tmp_alloc;

  VkDescriptorSetLayout set_layout;
  ViewSystemFrameState frame_states[TB_MAX_FRAME_STATES];

  uint32_t view_count;
  TbViewId *view_ids;
  CommonViewData *view_data;
  Frustum *view_frustums;
  uint32_t view_max;

} ViewSystem;

void tb_view_system_descriptor(SystemDescriptor *desc,
                               const ViewSystemDescriptor *view_desc);

TbViewId tb_view_system_create_view(ViewSystem *self);
void tb_view_system_set_view_data(ViewSystem *self, TbViewId view,
                                  const CommonViewData *data);
void tb_view_system_set_view_frustum(ViewSystem *self, TbViewId view,
                                     const Frustum *frust);
VkDescriptorSet tb_view_system_get_descriptor(ViewSystem *self, TbViewId view);
const CommonViewData *tb_view_system_get_data(ViewSystem *self, TbViewId view);
const Frustum *tb_view_system_get_frustum(ViewSystem *self, TbViewId view);
