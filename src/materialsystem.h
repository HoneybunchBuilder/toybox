#pragma once

#include "SDL2/SDL_stdinc.h"
#include "allocator.h"
#include "tbcommon.h"
#include "tbrendercommon.h"

#define MaterialSystemId 0x1BADB002

typedef struct SystemDescriptor SystemDescriptor;
typedef struct RenderSystem RenderSystem;
typedef struct cgltf_material cgltf_material;
typedef struct VkSampler_T *VkSampler;
typedef struct VkDescriptorSetLayout_T *VkDescriptorSetLayout;

typedef uint64_t TbMaterialId;
typedef uint64_t TbTextureId;

static const TbMaterialId InvalidMaterialId = SDL_MAX_UINT64;

typedef struct MaterialSystemDescriptor {
  Allocator std_alloc;
  Allocator tmp_alloc;
} MaterialSystemDescriptor;

typedef struct MaterialSystem {
  Allocator std_alloc;
  Allocator tmp_alloc;

  RenderSystem *render_system;

  VkSampler sampler; // Immutable sampler for material descriptor sets
  VkDescriptorSetLayout set_layout;
  VkDescriptorPool mat_set_pool;

  uint32_t mat_count;
  TbMaterialId *mat_ids;
  uint32_t *mat_ref_counts;
  TbBuffer *mat_gpu_buffers;
  TbTextureId *mat_color_maps;
  TbTextureId *mat_normal_maps;
  TbTextureId *mat_metal_rough_maps;
  VkDescriptorSet *mat_sets;
  uint32_t mat_max;
} MaterialSystem;

void tb_material_system_descriptor(SystemDescriptor *desc,
                                   const MaterialSystemDescriptor *mat_desc);

VkDescriptorSetLayout tb_mat_system_get_set_layout(MaterialSystem *self);

TbMaterialId tb_mat_system_load_material(MaterialSystem *self, const char *path,
                                         const cgltf_material *material);
void tb_mat_system_release_material_ref(MaterialSystem *self, TbMaterialId mat);
