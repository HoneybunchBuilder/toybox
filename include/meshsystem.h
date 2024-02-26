#pragma once

#include "SDL3/SDL_stdinc.h"
#include "allocator.h"
#include "dynarray.h"
#include "gltf.hlsli"
#include "meshcomponent.h"
#include "rendersystem.h"
#include "rendertargetsystem.h"
#include "tbcommon.h"
#include "tbrendercommon.h"
#include "viewsystem.h"

#include <flecs.h>

#define TB_MESH_SYS_PRIO (TB_VIEW_SYS_PRIO + 1)

typedef struct TbRenderSystem TbRenderSystem;
typedef struct TbMaterialSystem TbMaterialSystem;
typedef struct TbViewSystem TbViewSystem;
typedef struct TbRenderObjectSystem TbRenderObjectSystem;
typedef struct TbRenderPipelineSystem TbRenderPipelineSystem;
typedef struct TbMesh TbMesh;
typedef struct TbWorld TbWorld;
typedef TbResourceId TbMeshId;
typedef uint32_t TbMaterialPerm;
typedef uint32_t TbDrawContextId;
typedef struct cgltf_mesh cgltf_mesh;
typedef struct VkBuffer_T *VkBuffer;
typedef struct VkWriteDescriptorSet VkWriteDescriptorSet;
typedef struct ecs_query_t ecs_query_t;

static const TbMeshId TbInvalidMeshId = {SDL_MAX_UINT64, SDL_MAX_UINT32};
static const uint32_t TB_MESH_CMD_PAGE_SIZE = 64;

typedef struct TbPrimitiveDraw {
  VkBuffer geom_buffer;
  VkIndexType index_type;
  uint32_t index_count;
  uint64_t index_offset;
  uint32_t vertex_offset;
  uint32_t instance_count;
} TbPrimitiveDraw;

typedef struct TbIndirectDraw {
  VkBuffer buffer;
  uint64_t offset;
  uint32_t draw_count;
  uint32_t stride;
} TbIndirectDraw;

typedef struct TbPrimitiveBatch {
  VkDescriptorSet view_set;
  VkDescriptorSet mat_set;
  VkDescriptorSet draw_set;
  VkDescriptorSet obj_set;
  VkDescriptorSet tex_set;
  VkDescriptorSet idx_set;
  VkDescriptorSet pos_set;
  VkDescriptorSet norm_set;
  VkDescriptorSet tan_set;
  VkDescriptorSet uv0_set;
} TbPrimitiveBatch;

typedef struct TbMeshSystem {
  TbAllocator gp_alloc;
  TbAllocator tmp_alloc;

  TbRenderSystem *rnd_sys;
  TbMaterialSystem *material_system;
  TbViewSystem *view_sys;
  TbRenderObjectSystem *render_object_system;
  TbRenderPipelineSystem *rp_sys;

  ecs_query_t *camera_query;
  ecs_query_t *mesh_query;
  ecs_query_t *dir_light_query;

  TbDrawContextId prepass_draw_ctx2;
  TbDrawContextId opaque_draw_ctx2;
  TbDrawContextId transparent_draw_ctx2;

  VkDescriptorSetLayout mesh_set_layout;
  VkDescriptorSetLayout draw_set_layout;
  VkPipelineLayout pipe_layout;
  VkPipeline opaque_pipeline;
  VkPipeline transparent_pipeline;

  VkPipelineLayout prepass_layout;
  VkPipeline prepass_pipe;

  // Re-used by shadows
  TbDrawBatch *opaque_batch;

  TB_DYN_ARR_OF(TbMesh) meshes;
  // For per draw data
  TbFrameDescriptorPoolList draw_pools;
  // For per mesh bindless vertex buffers
  TbDescriptorPool mesh_pool;
  uint32_t mesh_desc_count;
} TbMeshSystem;
extern ECS_COMPONENT_DECLARE(TbMeshSystem);

void tb_register_mesh_sys(TbWorld *world);
void tb_unregister_mesh_sys(TbWorld *world);

TbMeshId tb_mesh_system_load_mesh(TbMeshSystem *self, const char *path,
                                  const cgltf_node *node);
bool tb_mesh_system_take_mesh_ref(TbMeshSystem *self, TbMeshId id);
VkBuffer tb_mesh_system_get_gpu_mesh(TbMeshSystem *self, TbMeshId id);
void tb_mesh_system_release_mesh_ref(TbMeshSystem *self, TbMeshId id);

VkDescriptorSet tb_mesh_system_get_pos_set(TbMeshSystem *self);
VkDescriptorSet tb_mesh_system_get_norm_set(TbMeshSystem *self);
VkDescriptorSet tb_mesh_system_get_tan_set(TbMeshSystem *self);
VkDescriptorSet tb_mesh_system_get_uv0_set(TbMeshSystem *self);
