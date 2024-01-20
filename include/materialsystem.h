#pragma once

#include "SDL3/SDL_stdinc.h"
#include "allocator.h"
#include "dynarray.h"
#include "rendersystem.h"
#include "tbcommon.h"
#include "tbrendercommon.h"

typedef struct cgltf_material cgltf_material;
typedef struct TbWorld TbWorld;
typedef struct VkSampler_T *VkSampler;
typedef struct VkDescriptorSetLayout_T *VkDescriptorSetLayout;
typedef struct TbRenderSystem TbRenderSystem;
typedef struct TbTextureSystem TbTextureSystem;
typedef struct TbMaterial TbMaterial;
typedef TbResourceId TbMaterialId;
typedef uint64_t TbTextureId;
typedef uint32_t TbMaterialPerm;

static const TbMaterialId InvalidMaterialId = {SDL_MAX_UINT64, SDL_MAX_UINT32};

typedef struct TbMaterialSystem {
  TbAllocator std_alloc;
  TbAllocator tmp_alloc;

  TbRenderSystem *rnd_sys;
  TbTextureSystem *texture_system;

  VkSampler sampler;        // Immutable sampler for material descriptor sets
  VkSampler shadow_sampler; // Immutable sampler for sampling shadow maps
  VkDescriptorSetLayout set_layout;

  const cgltf_material *default_material;

  // These two arrays are to be kept in sync
  TB_DYN_ARR_OF(TbMaterial) materials;

  // Descriptor pool that owns descriptor arrays of material buffer data, color,
  // normal and pbr textures
  TbDescriptorPool mat_pool;
} TbMaterialSystem;

void tb_register_material_sys(TbWorld *world);
void tb_unregister_material_sys(TbWorld *world);

VkDescriptorSetLayout tb_mat_system_get_set_layout(TbMaterialSystem *self);

TbMaterialId tb_mat_system_load_material(TbMaterialSystem *self,
                                         const char *path,
                                         const cgltf_material *material);

TbMaterialPerm tb_mat_system_get_perm(TbMaterialSystem *self, TbMaterialId mat);

VkDescriptorSet tb_mat_system_get_set(TbMaterialSystem *self);

void tb_mat_system_release_material_ref(TbMaterialSystem *self,
                                        TbMaterialId mat);
