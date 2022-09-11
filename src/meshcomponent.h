#pragma once

#include "tbrendercommon.h"

#define MeshComponentId 0x0D15EA5E
#define MeshComponentIdStr "0x0D15EA5E"

typedef struct ComponentDescriptor ComponentDescriptor;

typedef struct cgltf_mesh cgltf_mesh;

typedef struct MeshComponentDescriptor {
  const char *source_path;
  const cgltf_mesh *mesh;
} MeshComponentDescriptor;

typedef struct MeshComponent {
  TbBuffer geom_buffer;
  uint32_t index_count;
  uint32_t index_offset;
  uint32_t vertex_offset;
} MeshComponent;

void tb_mesh_component_descriptor(ComponentDescriptor *desc);
