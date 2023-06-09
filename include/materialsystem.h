#pragma once

#include "SDL2/SDL_stdinc.h"
#include "allocator.h"
#include "tbcommon.h"
#include "tbrendercommon.h"

#define MaterialSystemId 0x1BADB002

typedef struct SystemDescriptor SystemDescriptor;
typedef struct RenderSystem RenderSystem;
typedef struct TextureSystem TextureSystem;
typedef struct cgltf_material cgltf_material;
typedef struct VkSampler_T *VkSampler;
typedef struct VkDescriptorSetLayout_T *VkDescriptorSetLayout;

typedef uint64_t TbMaterialId;
typedef uint64_t TbTextureId;
typedef uint64_t TbMaterialPerm;

static const TbMaterialId InvalidMaterialId = SDL_MAX_UINT64;

typedef struct MaterialSystemDescriptor {
  Allocator std_alloc;
  Allocator tmp_alloc;
} MaterialSystemDescriptor;

typedef struct MaterialSystem {
  Allocator std_alloc;
  Allocator tmp_alloc;

  RenderSystem *render_system;
  TextureSystem *texture_system;

  VkSampler sampler; // Immutable sampler for material descriptor sets
  VkDescriptorSetLayout set_layout;
  VkDescriptorPool mat_set_pool;

  const cgltf_material *default_material;

  uint32_t mat_count;
  TbMaterialId *mat_ids;
  TbMaterialPerm *mat_perms;
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

TbMaterialPerm tb_mat_system_get_perm(MaterialSystem *self, TbMaterialId mat);
VkDescriptorSet tb_mat_system_get_set(MaterialSystem *self, TbMaterialId mat);

void tb_mat_system_release_material_ref(MaterialSystem *self, TbMaterialId mat);
