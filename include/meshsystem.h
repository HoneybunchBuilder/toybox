#pragma once

#include "SDL2/SDL_stdinc.h"
#include "allocator.h"
#include "dynarray.h"
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

typedef enum GLTF_PERMUTATIONS {
  GLTF_PERM_NONE = 0x00000000,
  GLTF_PERM_BASE_COLOR_MAP = 0x00000001,
  GLTF_PERM_NORMAL_MAP = 0x00000002,
  GLTF_PERM_PBR_METALLIC_ROUGHNESS = 0x00000004,
  GLTF_PERM_PBR_METAL_ROUGH_TEX = 0x00000008,
  GLTF_PERM_PBR_SPECULAR_GLOSSINESS = 0x0000010,
  GLTF_PERM_CLEARCOAT = 0x00000020,
  GLTF_PERM_TRANSMISSION = 0x00000040,
  GLTF_PERM_VOLUME = 0x00000080,
  GLTF_PERM_IOR = 0x00000100,
  GLTF_PERM_SPECULAR = 0x00000200,
  GLTF_PERM_SHEEN = 0x000000400,
  GLTF_PERM_UNLIT = 0x00000800,
  GLTF_PERM_ALPHA_CLIP = 0x00001000,
  GLTF_PERM_ALPHA_BLEND = 0x00002000,
  GLTF_PERM_DOUBLE_SIDED = 0x00004000,
  GLTF_PERM_FLAG_COUNT = 15,
  GLTF_PERM_COUNT = 1 << GLTF_PERM_FLAG_COUNT,
} GLTF_PERMUTATIONS;

static const GLTF_PERMUTATIONS MaterialPermutations[GLTF_PERM_COUNT] = {
    GLTF_PERM_BASE_COLOR_MAP,
    GLTF_PERM_NORMAL_MAP,
    GLTF_PERM_PBR_METALLIC_ROUGHNESS,
    GLTF_PERM_PBR_METAL_ROUGH_TEX,
    GLTF_PERM_PBR_SPECULAR_GLOSSINESS,
    GLTF_PERM_CLEARCOAT,
    GLTF_PERM_TRANSMISSION,
    GLTF_PERM_VOLUME,
    GLTF_PERM_IOR,
    GLTF_PERM_SPECULAR,
    GLTF_PERM_SHEEN,
    GLTF_PERM_UNLIT,
    GLTF_PERM_ALPHA_CLIP,
    GLTF_PERM_ALPHA_BLEND,
    GLTF_PERM_DOUBLE_SIDED,
};

typedef struct MeshSystem {
  Allocator std_alloc;
  Allocator tmp_alloc;

  RenderSystem *render_system;
  MaterialSystem *material_system;
  ViewSystem *view_system;
  RenderObjectSystem *render_object_system;
  RenderPipelineSystem *render_pipe_system;

  ecs_query_t *camera_query;
  ecs_query_t *mesh_query;
  ecs_query_t *dir_light_query;

  TbDrawContextId prepass_draw_ctx;
  TbDrawContextId opaque_draw_ctx;
  TbDrawContextId transparent_draw_ctx;
  TbDrawContextId shadow_draw_ctx;

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

void tb_register_mesh_sys(ecs_world_t *ecs, Allocator std_alloc,
                          Allocator tmp_alloc);
void tb_unregister_mesh_sys(ecs_world_t *ecs);

TbMeshId tb_mesh_system_load_mesh(MeshSystem *self, const char *path,
                                  const cgltf_node *node);
bool tb_mesh_system_take_mesh_ref(MeshSystem *self, TbMeshId id);
VkBuffer tb_mesh_system_get_gpu_mesh(MeshSystem *self, TbMeshId id);
void tb_mesh_system_release_mesh_ref(MeshSystem *self, TbMeshId id);
