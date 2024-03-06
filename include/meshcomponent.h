#pragma once

#include "dynarray.h"
#include "simd.h"
#include "tbrendercommon.h"

#include <flecs.h>

typedef struct TbWorld TbWorld;

#define TB_VERTEX_BINDING_MAX 4

typedef TbResourceId TbMeshId;
typedef TbResourceId TbMaterialId;
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
  TB_DYN_ARR_OF(TbSubMesh) submeshes;
  TbAABB local_aabb;
} TbMeshComponent;
extern ECS_COMPONENT_DECLARE(TbMeshComponent);
