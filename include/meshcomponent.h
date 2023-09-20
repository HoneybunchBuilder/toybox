#pragma once

#include "simd.h"
#include <stdint.h>

#define MeshComponentId 0x0D15EA5E
#define MeshComponentIdStr "0x0D15EA5E"

typedef struct ecs_world_t ecs_world_t;

#define TB_SUBMESH_MAX 32
#define TB_VERTEX_BINDING_MAX 4

typedef uint64_t TbMeshId;
typedef uint64_t TbMaterialId;
typedef uint64_t TbRenderObjectId;

typedef enum TbVertexInput {
  VI_P3N3 = 0,
  VI_P3N3U2 = 1,
  VI_P3N3T4U2 = 2,
  VI_Count = 3,
} TbVertexInput;

typedef struct SubMesh {
  int32_t index_type;
  uint32_t index_count;
  uint64_t index_offset;
  uint64_t vertex_offset;
  uint32_t vertex_count;
  TbVertexInput vertex_input;
  TbMaterialId material;
} SubMesh;

typedef struct MeshComponent {
  TbMeshId mesh_id;
  TbRenderObjectId object_id;
  uint32_t submesh_count;
  SubMesh submeshes[TB_SUBMESH_MAX];
  AABB local_aabb;
} MeshComponent;

void tb_register_mesh_component(ecs_world_t *ecs);
