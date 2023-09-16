#pragma once

#include "simd.h"
#include <stdint.h>

#define MeshComponentId 0x0D15EA5E
#define MeshComponentIdStr "0x0D15EA5E"

typedef struct ComponentDescriptor ComponentDescriptor;
typedef struct cgltf_mesh cgltf_mesh;

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

typedef struct MeshComponentDescriptor {
  const char *source_path;
  const cgltf_node *node;
} MeshComponentDescriptor;

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

void tb_mesh_component_descriptor(ComponentDescriptor *desc);

typedef struct ecs_world_t ecs_world_t;
typedef struct ecs_iter_t ecs_iter_t;
typedef uint64_t ecs_entity_t;
typedef struct cgltf_node cgltf_node;
typedef struct json_object json_object;
bool tb_create_mesh_component2(ecs_world_t *ecs, ecs_entity_t e,
                               const char *source_path, const cgltf_node *node,
                               json_object *extra);
void tb_destroy_mesh_component2(ecs_world_t *ecs);
