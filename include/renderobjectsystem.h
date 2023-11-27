#pragma once

#include "allocator.h"
#include "dynarray.h"
#include "rendersystem.h"
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

typedef struct ecs_world_t ecs_world_t;
typedef struct ecs_query_t ecs_query_t;

typedef struct RenderObject {
  int32_t index;
} RenderObject;
static const uint64_t InvalidRenderObjectId = SDL_MAX_UINT64;

typedef struct TransformsBuffer {
  int32_t obj_count;
  TbBuffer gpu;
  TbHostBuffer host;
} TransformsBuffer;

typedef struct RenderObjectSystem {
  RenderSystem *render_system;
  TbAllocator std_alloc;
  TbAllocator tmp_alloc;

  VkDescriptorSetLayout set_layout;
  FrameDescriptorPool pools[TB_MAX_FRAME_STATES];

  TransformsBuffer trans_buffers[TB_MAX_FRAME_STATES];

  ecs_query_t *obj_query;
} RenderObjectSystem;

void tb_register_render_object_sys(ecs_world_t *ecs, TbAllocator std_alloc,
                                   TbAllocator tmp_alloc);
VkDescriptorSet tb_render_object_sys_get_set(RenderObjectSystem *sys);
void tb_unregister_render_object_sys(ecs_world_t *ecs);
