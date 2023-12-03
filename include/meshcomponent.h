#pragma once

#include "dynarray.h"
#include "simd.h"

typedef struct ecs_world_t ecs_world_t;

#define TB_SUBMESH_MAX 8
#define TB_VERTEX_BINDING_MAX 4

typedef uint64_t TbMeshId;
typedef uint64_t TbMaterialId;
typedef uint64_t ecs_entity_t;

typedef struct TbSubMesh {
  uint32_t index_type;
  uint32_t index_count;
  uint64_t index_offset;
  uint64_t vertex_offset;
  uint32_t vertex_count;
  uint32_t vertex_perm;
  TbMaterialId material;
} TbSubMesh;

typedef struct TbMeshComponent {
  TbMeshId mesh_id;
  TB_DYN_ARR_OF(ecs_entity_t) entities;
  uint32_t submesh_count;
  TbSubMesh submeshes[TB_SUBMESH_MAX];
  TbAABB local_aabb;
} TbMeshComponent;

void tb_register_mesh_component(ecs_world_t *ecs);
