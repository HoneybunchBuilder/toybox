#pragma once

#include "SDL2/SDL_stdinc.h"
#include "allocator.h"
#include "tbcommon.h"
#include "tbrendercommon.h"

#define MeshSystemId 0xBEEFBABE

typedef struct SystemDescriptor SystemDescriptor;
typedef struct RenderSystem RenderSystem;
typedef struct cgltf_mesh cgltf_mesh;
typedef struct VkBuffer_T *VkBuffer;
typedef struct CameraComponent CameraComponent;

typedef uint64_t TbMeshId;
static const TbMeshId InvalidMeshId = SDL_MAX_UINT64;

typedef struct MeshSystemDescriptor {
  Allocator std_alloc;
  Allocator tmp_alloc;
} MeshSystemDescriptor;

typedef struct MeshSystem {
  Allocator std_alloc;
  Allocator tmp_alloc;

  RenderSystem *render_system;

  VkRenderPass opaque_pass;
  VkFramebuffer framebuffers[TB_MAX_FRAME_STATES];

  VkDescriptorSetLayout obj_set_layout;
  VkDescriptorSetLayout view_set_layout;
  VkPipelineLayout pipe_layout;
  VkPipeline pipeline;

  // TODO: Consolidate into a view system
  VkDescriptorPool view_set_pool;
  uint32_t view_count;
  CameraComponent *view_ids;
  TbBuffer *view_gpu_buffer;
  VkDescriptorSet *view_sets;
  uint32_t view_max;

  uint32_t mesh_count;
  VkDescriptorPool obj_set_pool;
  TbMeshId *mesh_ids;
  TbHostBuffer *mesh_host_buffers;
  TbBuffer *obj_gpu_buffers;
  VkDescriptorSet *obj_sets;
  TbBuffer *mesh_gpu_buffers;
  uint32_t *mesh_ref_counts;
  uint32_t mesh_max;
} MeshSystem;

void tb_mesh_system_descriptor(SystemDescriptor *desc,
                               const MeshSystemDescriptor *mesh_desc);

TbMeshId tb_mesh_system_load_mesh(MeshSystem *self, const char *path,
                                  const cgltf_mesh *mesh);
bool tb_mesh_system_take_mesh_ref(MeshSystem *self, TbMeshId id);
VkBuffer tb_mesh_system_get_gpu_mesh(MeshSystem *self, TbMeshId id);
void tb_mesh_system_release_mesh_ref(MeshSystem *self, TbMeshId id);
