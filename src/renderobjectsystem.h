#pragma once

#include "allocator.h"
#include "tbrendercommon.h"

#define RenderObjectSystemId 0xFEEDC0DE

typedef struct RenderSystem RenderSystem;
typedef struct SystemDescriptor SystemDescriptor;
typedef struct VkDescriptorSetLayout_T *VkDescriptorSetLayout;
typedef struct VkDescriptorPool_T *VkDescriptorPool;
typedef struct VkDescriptorSet_T *VkDescriptorSet;
typedef struct CommonObjectData CommonObjectData;

typedef uint64_t TbRenderObjectId;
static const uint64_t InvalidRenderObjectId = SDL_MAX_UINT64;

typedef struct RenderObjectSystemDescriptor {
  Allocator std_alloc;
  Allocator tmp_alloc;
} RenderObjectSystemDescriptor;

typedef struct RenderObjectSystemFrameState {
  uint32_t set_count;
  VkDescriptorPool set_pool;
  VkDescriptorSet *sets;
  uint32_t set_max;
} RenderObjectSystemFrameState;

typedef struct RenderObjectSystem {
  RenderSystem *render_system;
  Allocator std_alloc;
  Allocator tmp_alloc;

  VkDescriptorSetLayout set_layout;
  RenderObjectSystemFrameState frame_states[TB_MAX_FRAME_STATES];

  uint32_t render_object_count;
  TbRenderObjectId *render_object_ids;
  CommonObjectData *render_object_data;
  uint32_t render_object_max;
} RenderObjectSystem;

void tb_render_object_system_descriptor(
    SystemDescriptor *desc, const RenderObjectSystemDescriptor *object_desc);

TbRenderObjectId tb_render_object_system_create(RenderObjectSystem *self);

void tb_render_object_system_set_object_data(RenderObjectSystem *self,
                                             TbRenderObjectId object,
                                             const CommonObjectData *data);
VkDescriptorSet tb_render_object_system_get_descriptor(RenderObjectSystem *self,
                                                       TbRenderObjectId object);
const CommonObjectData *
tb_render_object_system_get_data(RenderObjectSystem *self,
                                 TbRenderObjectId object);
