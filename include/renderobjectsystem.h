#pragma once

#include "allocator.h"
#include "dynarray.h"
#include "tbrendercommon.h"

/*
  Next step:
    Every frame, collect the changed render object transforms and sphere bounds
  into arrays that are uploaded to the GPU. Only deltas in the collection need
  uploading. Meshes and other render objects will look up the render object
  index from this system and provide them to the various draws in a per-instance
  buffer. This prevents the need for moving any transform related data to the
  GPU.
    A frustum culling compute shader should be able to generate a list of only
  the visible render objects by looking up the transform and the sphere bounds
  of every drawable object. Thus enabling GPU driven indirect drawing.
*/

#define RenderObjectSystemId 0xFEEDC0DE

typedef struct RenderSystem RenderSystem;
typedef struct SystemDescriptor SystemDescriptor;
typedef struct VkDescriptorSetLayout_T *VkDescriptorSetLayout;
typedef struct VkDescriptorPool_T *VkDescriptorPool;
typedef struct VkDescriptorSet_T *VkDescriptorSet;
typedef struct CommonObjectData CommonObjectData;

typedef struct ecs_world_t ecs_world_t;

typedef uint64_t TbRenderObjectId;
static const uint64_t InvalidRenderObjectId = SDL_MAX_UINT64;

typedef struct RenderObjectSystemFrameState {
  VkDescriptorPool pool;
  TB_DYN_ARR_OF(VkDescriptorSet) sets;
} RenderObjectSystemFrameState;

typedef struct RenderObjectSystem {
  RenderSystem *render_system;
  Allocator std_alloc;
  Allocator tmp_alloc;

  VkDescriptorSetLayout set_layout;
  RenderObjectSystemFrameState frame_states[TB_MAX_FRAME_STATES];

  TB_DYN_ARR_OF(CommonObjectData) render_object_data;
} RenderObjectSystem;

void tb_register_render_object_sys(ecs_world_t *ecs, Allocator std_alloc,
                                   Allocator tmp_alloc);
void tb_unregister_render_object_sys(ecs_world_t *ecs);

TbRenderObjectId tb_render_object_system_create(RenderObjectSystem *self);

void tb_render_object_system_set_object_data(RenderObjectSystem *self,
                                             TbRenderObjectId object,
                                             const CommonObjectData *data);
VkDescriptorSet tb_render_object_system_get_descriptor(RenderObjectSystem *self,
                                                       TbRenderObjectId object);
const CommonObjectData *
tb_render_object_system_get_data(RenderObjectSystem *self,
                                 TbRenderObjectId object);
