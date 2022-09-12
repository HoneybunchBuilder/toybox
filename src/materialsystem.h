#pragma once

#include "allocator.h"
#include "tbcommon.h"
#include "tbrendercommon.h"

#define MaterialSystemId 0x1BADB002

typedef struct SystemDescriptor SystemDescriptor;
typedef struct RenderSystem RenderSystem;
typedef struct cgltf_material cgltf_material;
typedef struct VkBuffer_T *VkBuffer;

typedef uint64_t TbMaterialId;
static const TbMaterialId InvalidMaterialId = 0xFFFFFFFF;

typedef struct MaterialSystemDescriptor {
  Allocator std_alloc;
  Allocator tmp_alloc;
} MaterialSystemDescriptor;

typedef struct MaterialSystem {
  Allocator std_alloc;
  Allocator tmp_alloc;

  RenderSystem *render_system;

  uint32_t mat_count;
  TbMaterialId *mat_ids;
  uint32_t *mat_ref_counts;
  uint32_t mat_max;
} MaterialSystem;

void tb_material_system_descriptor(SystemDescriptor *desc,
                                   const MaterialSystemDescriptor *mat_desc);

TbMaterialId tb_mat_system_load_material(MaterialSystem *self, const char *path,
                                         const cgltf_material *material);
