#pragma once

#include "allocator.h"
#include "tbrendercommon.h"

#define RenderObjectSystemId 0xFEE1DEAD

typedef struct RenderSystem RenderSystem;
typedef struct SystemDescriptor SystemDescriptor;
typedef struct VkDescriptorSetLayout_T *VkDescriptorSetLayout;
typedef struct VkDescriptorSetPool_T *VkDescriptorSetPool;
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
  VkDescriptorSetPool set_pool;
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

TbRenderObjectId tb_render_object_system_create(RenderObjectSystem *self,
                                                const CommonObjectData *data);

void tb_render_object_system_set(RenderObjectSystem *self,
                                 TbRenderObjectId object,
                                 const CommonObjectData *data);
