#pragma once

#include "stdint.h"

#define MeshComponentId 0x0D15EA5E
#define MeshComponentIdStr "0x0D15EA5E"

typedef struct ComponentDescriptor ComponentDescriptor;
typedef struct cgltf_mesh cgltf_mesh;

#define TB_SUBMESH_MAX 16
#define TB_VERTEX_BINDING_MAX 4

typedef uint64_t TbMeshId;
typedef uint64_t TbMaterialId;
typedef enum TbVertexInput {
  VI_P3N3 = 0x00000001,
  VI_P3N3U2 = 0x00000002,
  VI_P3N3T4U2 = 0x00000004,
  VI_Count = 3,
} TbVertexInput;

typedef struct MeshComponentDescriptor {
  const char *source_path;
  const cgltf_mesh *mesh;
} MeshComponentDescriptor;

typedef struct SubMesh {
  int32_t index_type;
  uint32_t index_count;
  uint64_t index_offset;
  uint64_t vertex_offset;
  TbVertexInput vertex_input;
  TbMaterialId material;
} SubMesh;

typedef struct MeshComponent {
  TbMeshId mesh_id;
  uint32_t submesh_count;
  SubMesh submeshes[TB_SUBMESH_MAX];
} MeshComponent;

void tb_mesh_component_descriptor(ComponentDescriptor *desc);
