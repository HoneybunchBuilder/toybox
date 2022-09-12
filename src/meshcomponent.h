#pragma once

#include "stdint.h"

#define MeshComponentId 0x0D15EA5E
#define MeshComponentIdStr "0x0D15EA5E"

typedef struct ComponentDescriptor ComponentDescriptor;

typedef struct cgltf_mesh cgltf_mesh;

typedef struct VkBuffer_T *VkBuffer;

typedef uint32_t TbMeshId;

typedef struct MeshComponentDescriptor {
  const char *source_path;
  const cgltf_mesh *mesh;
} MeshComponentDescriptor;

typedef struct TbMaterial TbMaterial;

typedef struct SubMesh {
  uint32_t index_count;
  uint64_t index_offset;
  TbMaterial *material;
} SubMesh;

typedef struct MeshComponent {
  TbMeshId mesh_id;
  VkBuffer geom_buffer;
  uint64_t vertex_offset;
  uint32_t submesh_count;
  SubMesh *submeshes;
} MeshComponent;

void tb_mesh_component_descriptor(ComponentDescriptor *desc);
