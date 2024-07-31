#pragma once

#include "SDL3/SDL_stdinc.h"
#include "gltf.hlsli"
#include "tb_allocator.h"
#include "tb_common.h"
#include "tb_dynarray.h"
#include "tb_mesh_component.h"
#include "tb_render_common.h"
#include "tb_render_system.h"
#include "tb_render_target_system.h"
#include "tb_view_system.h"

#include <flecs.h>

#ifndef TB_MESH_SYS_PRIO
#define TB_MESH_SYS_PRIO (TB_RP_SYS_PRIO + 1)
#endif

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
#if TB_USE_DESC_BUFFER == 1
  VkDescriptorBufferBindingInfoEXT view_addr;
  VkDescriptorBufferBindingInfoEXT mat_addr;
  VkDescriptorBufferBindingInfoEXT draw_addr;
  VkDescriptorBufferBindingInfoEXT obj_addr;
  VkDescriptorBufferBindingInfoEXT tex_addr;
  VkDescriptorBufferBindingInfoEXT idx_addr;
  VkDescriptorBufferBindingInfoEXT pos_addr;
  VkDescriptorBufferBindingInfoEXT norm_addr;
  VkDescriptorBufferBindingInfoEXT tan_addr;
  VkDescriptorBufferBindingInfoEXT uv0_addr;
#else
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
#endif
} TbPrimitiveBatch;

typedef struct TbMeshSystem {
  TbAllocator gp_alloc;
  TbAllocator tmp_alloc;

  TbRenderSystem *rnd_sys;
  TbViewSystem *view_sys;
  TbRenderObjectSystem *render_object_system;
  TbRenderPipelineSystem *rp_sys;

  ecs_query_t *camera_query;
  ecs_query_t *mesh_query;
  ecs_query_t *dir_light_query;

  TbDrawContextId prepass_draw_ctx2;
  TbDrawContextId opaque_draw_ctx2;
  TbDrawContextId transparent_draw_ctx2;

  VkDescriptorSetLayout draw_set_layout;
  VkPipelineLayout pipe_layout;
  VkPipelineLayout prepass_layout;

  TbShader opaque_shader;
  TbShader transparent_shader;
  TbShader prepass_shader;

  // Re-used by shadows
  TbDrawBatch *opaque_batch;

  TB_DYN_ARR_OF(TbMesh) meshes;
  // For per draw data
  TbFrameDescriptorPoolList draw_pools;

  TbDescriptorBuffer opaque_draw_descs;
  TbDescriptorBuffer trans_draw_descs;
} TbMeshSystem;
extern ECS_COMPONENT_DECLARE(TbMeshSystem);

void tb_register_mesh_sys(TbWorld *world);
void tb_unregister_mesh_sys(TbWorld *world);

VkDescriptorSet tb_mesh_system_get_pos_set(TbMeshSystem *self);
VkDescriptorSet tb_mesh_system_get_norm_set(TbMeshSystem *self);
VkDescriptorSet tb_mesh_system_get_tan_set(TbMeshSystem *self);
VkDescriptorSet tb_mesh_system_get_uv0_set(TbMeshSystem *self);
