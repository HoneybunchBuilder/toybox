#pragma once

#include "allocator.h"
#include "tbcommon.h"
#include "tbrendercommon.h"

#define MeshSystemId 0xBEEFBABE

typedef struct SystemDescriptor SystemDescriptor;
typedef struct RenderSystem RenderSystem;
typedef struct cgltf_mesh cgltf_mesh;
typedef struct VkBuffer_T *VkBuffer;

typedef uint32_t TbMeshId;

typedef struct MeshSystemDescriptor {
  Allocator std_alloc;
  Allocator tmp_alloc;
} MeshSystemDescriptor;

typedef struct MeshSystem {
  Allocator std_alloc;
  Allocator tmp_alloc;

  RenderSystem *render_system;

  VkRenderPass pass;
  VkFramebuffer framebuffers[TB_MAX_FRAME_STATES];

  VkDescriptorSetLayout set_layout;
  VkPipelineLayout pipe_layout;
  VkPipeline pipeline;

  uint32_t mesh_count;
  TbMeshId *mesh_ids;
  TbBuffer *mesh_host_buffers;
  TbBuffer *mesh_gpu_buffers;
  uint32_t *mesh_ref_counts;
  uint32_t mesh_max;
} MeshSystem;

void tb_mesh_system_descriptor(SystemDescriptor *desc,
                               const MeshSystemDescriptor *mesh_desc);

bool tb_mesh_system_load_mesh(MeshSystem *self, const char *path,
                              const cgltf_mesh *mesh, TbMeshId *id);
bool tb_mesh_system_take_mesh_ref(MeshSystem *self, TbMeshId id);
VkBuffer tb_mesh_system_get_gpu_mesh(MeshSystem *self, TbMeshId id);
void tb_mesh_system_release_mesh_ref(MeshSystem *self, TbMeshId id);
