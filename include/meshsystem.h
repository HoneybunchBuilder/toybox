#pragma once

#include "SDL2/SDL_stdinc.h"
#include "allocator.h"
#include "dynarray.h"
#include "gltf.hlsli"
#include "meshcomponent.h"
#include "rendersystem.h"
#include "rendertargetsystem.h"
#include "tbcommon.h"
#include "tbrendercommon.h"

#define MeshSystemId 0xBEEFBABE

typedef struct SystemDescriptor SystemDescriptor;
typedef struct RenderSystem RenderSystem;
typedef struct MaterialSystem MaterialSystem;
typedef struct ViewSystem ViewSystem;
typedef struct RenderObjectSystem RenderObjectSystem;
typedef struct RenderPipelineSystem RenderPipelineSystem;
typedef struct cgltf_mesh cgltf_mesh;
typedef struct VkBuffer_T *VkBuffer;
typedef struct CameraComponent CameraComponent;
typedef struct MeshComponent MeshComponent;

typedef struct ecs_world_t ecs_world_t;
typedef struct ecs_query_t ecs_query_t;

typedef uint64_t TbMeshId;
typedef uint64_t TbMaterialPerm;
typedef uint32_t TbDrawContextId;
typedef struct TbMesh TbMesh;
static const TbMeshId InvalidMeshId = SDL_MAX_UINT64;

static const uint32_t TB_MESH_CMD_PAGE_SIZE = 64;
typedef struct MeshCommandPool MeshCommandPool;

typedef struct PrimitiveDraw {
  VkIndexType index_type;
  uint32_t index_count;
  uint64_t index_offset;
  uint32_t vertex_binding_count;
  uint64_t vertex_binding_offsets[TB_VERTEX_BINDING_MAX];
  uint32_t instance_count;
} PrimitiveDraw;

typedef struct PrimitiveBatch {
  uint64_t perm;
  VkDescriptorSet view_set;
  VkDescriptorSet inst_set;
  VkDescriptorSet trans_set;
  VkDescriptorSet mat_set;
  VkBuffer geom_buffer;
} PrimitiveBatch;

typedef TB_DYN_ARR_OF(int32_t) IndirectionList;

typedef TB_DYN_ARR_OF(PrimitiveBatch) PrimitiveBatchList;
typedef TB_DYN_ARR_OF(IndirectionList) PrimIndirectList;

typedef struct MeshSystem {
  TbAllocator std_alloc;
  TbAllocator tmp_alloc;

  RenderSystem *render_system;
  MaterialSystem *material_system;
  ViewSystem *view_system;
  RenderObjectSystem *render_object_system;
  RenderPipelineSystem *render_pipe_system;

  ecs_query_t *camera_query;
  ecs_query_t *mesh_query;
  ecs_query_t *dir_light_query;

  TbDrawContextId prepass_draw_ctx2;
  TbDrawContextId opaque_draw_ctx2;
  TbDrawContextId transparent_draw_ctx2;

  VkDescriptorSetLayout obj_set_layout;
  VkDescriptorSetLayout view_set_layout;
  VkPipelineLayout pipe_layout;

  VkPipelineLayout prepass_layout;
  VkPipeline prepass_pipe;

  uint32_t pipe_count;
  VkPipeline *opaque_pipelines;
  VkPipeline *transparent_pipelines;

  VkPipelineLayout shadow_pipe_layout;
  VkPipeline shadow_pipeline;

  TB_DYN_ARR_OF(TbMesh) meshes;
  FrameDescriptorPoolList desc_pool_list;
} MeshSystem;

void tb_register_mesh_sys(ecs_world_t *ecs, TbAllocator std_alloc,
                          TbAllocator tmp_alloc);
void tb_unregister_mesh_sys(ecs_world_t *ecs);

TbMeshId tb_mesh_system_load_mesh(MeshSystem *self, const char *path,
                                  const cgltf_node *node);
bool tb_mesh_system_take_mesh_ref(MeshSystem *self, TbMeshId id);
VkBuffer tb_mesh_system_get_gpu_mesh(MeshSystem *self, TbMeshId id);
void tb_mesh_system_release_mesh_ref(MeshSystem *self, TbMeshId id);
