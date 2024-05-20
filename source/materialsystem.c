#include "materialsystem.h"

#include "cgltf.h"
#include "common.hlsli"
#include "gltf.hlsli"
#include "hash.h"
#include "profiling.h"
#include "rendersystem.h"
#include "tb_texture_system.h"
#include "world.h"

#include <flecs.h>

ECS_COMPONENT_DECLARE(TbMaterialSystem);

typedef struct TbMaterial {
  TbMaterialId id;
  uint32_t ref_count;
  TbMaterialPerm permutation;
  TbBuffer gpu_buffer;
  TbTextureId color_map;
  TbTextureId normal_map;
  TbTextureId metal_rough_map;
} TbMaterial;

void tb_register_material_sys(TbWorld *world);
void tb_unregister_material_sys(TbWorld *world);

TB_REGISTER_SYS(tb, material, TB_MAT_SYS_PRIO)

uint32_t find_mat_by_hash(TbMaterialSystem *self, uint64_t id) {
  TB_DYN_ARR_FOREACH(self->materials, i) {
    if (TB_DYN_ARR_AT(self->materials, i).id.id == id) {
      return i;
      break;
    }
  }
  return SDL_MAX_UINT32;
}

TbMaterialSystem create_material_system(TbAllocator gp_alloc,
                                        TbAllocator tmp_alloc,
                                        TbRenderSystem *rnd_sys) {
  TbMaterialSystem sys = {
      .gp_alloc = gp_alloc,
      .tmp_alloc = tmp_alloc,
      .rnd_sys = rnd_sys,
  };

  TB_DYN_ARR_RESET(sys.materials, gp_alloc, 1);

  // Create immutable sampler for materials
  {
    VkSamplerCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .anisotropyEnable = VK_TRUE,
        .maxAnisotropy = 16.0f, // 16x anisotropy is cheap
        .maxLod = 14.0f,        // Hack; known number of mips for 8k textures
        .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
    };
    tb_rnd_create_sampler(rnd_sys, &create_info, "Material Sampler",
                          &sys.sampler);
  }

  // Create immutable sampler for sampling shadows
  {
    VkSamplerCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .anisotropyEnable = VK_FALSE,
        .compareEnable = VK_TRUE,
        .compareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
        .maxAnisotropy = 1.0f,
        .maxLod = 1.0f,
        .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
    };
    tb_rnd_create_sampler(rnd_sys, &create_info, "Material Shadow Sampler",
                          &sys.shadow_sampler);
  }

  // Create descriptor set layout for materials
  {
    const VkDescriptorBindingFlags flags =
        VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT |
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
    const uint32_t binding_count = 3;
    VkDescriptorSetLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext =
            &(VkDescriptorSetLayoutBindingFlagsCreateInfo){
                .sType =
                    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
                .bindingCount = binding_count,
                .pBindingFlags =
                    (VkDescriptorBindingFlags[binding_count]){0, 0, flags},
            },
        .bindingCount = binding_count,
        .pBindings =
            (VkDescriptorSetLayoutBinding[binding_count]){
                {0, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                 &sys.sampler},
                {1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
                 &sys.shadow_sampler},
                {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                 2048, // HACK: Some high upper limit
                 VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT,
                 NULL},
            },
    };
    tb_rnd_create_set_layout(rnd_sys, &create_info, "Material Set Layout",
                             &sys.set_layout);
  }

  // Create a default material
  // It uses the metallic roughness flow
  // it does not supply textures, those will be provided by default since
  // it is marked to use pbr metal rough but provides no texture views
  cgltf_material *default_mat = tb_alloc_tp(gp_alloc, cgltf_material);
  *default_mat = (cgltf_material){
      .name = "default",
      .has_pbr_metallic_roughness = true,
      .pbr_metallic_roughness =
          {
              .base_color_factor = {1, 1, 1, 1},
              .metallic_factor = 0.5f,
              .roughness_factor = 0.5f,
          },
  };
  sys.default_material = default_mat;

  return sys;
}

void destroy_material_system(TbMaterialSystem *self) {
  TbRenderSystem *rnd_sys = self->rnd_sys;

  tb_free(self->gp_alloc, (void *)self->default_material);

  tb_rnd_destroy_set_layout(rnd_sys, self->set_layout);
  tb_rnd_destroy_sampler(rnd_sys, self->sampler);
  tb_rnd_destroy_sampler(rnd_sys, self->shadow_sampler);

  TB_DYN_ARR_FOREACH(self->materials, i) {
    if (TB_DYN_ARR_AT(self->materials, i).ref_count != 0) {
      TB_CHECK(false, "Leaking materials");
    }
  }

  TB_DYN_ARR_DESTROY(self->materials);

  *self = (TbMaterialSystem){0};
}

void tick_material_system(ecs_iter_t *it) {
  ecs_world_t *ecs = it->world;

  tb_auto mat_sys = ecs_singleton_get_mut(ecs, TbMaterialSystem);
  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);

  // If the number of materials has grown to the point where we have run out of
  // space in the descriptor pool we must reallocate. That means destroying the
  // old pool and creating a new pool for all the new writes
  const uint64_t incoming_mat_count = TB_DYN_ARR_SIZE(mat_sys->materials);
  if (incoming_mat_count > mat_sys->mat_pool.capacity) {
    mat_sys->mat_pool.capacity = incoming_mat_count + 128;
    const uint64_t desc_count = mat_sys->mat_pool.capacity;

    // Re-create pool and allocate the one set that everything will be bound to
    {
      VkDescriptorPoolCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
          .maxSets = mat_sys->mat_pool.capacity,
          .poolSizeCount = 1,
          .pPoolSizes =
              (VkDescriptorPoolSize[1]){
                  {
                      .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                      .descriptorCount = desc_count * 4,
                  },
              },
      };
      VkDescriptorSetVariableDescriptorCountAllocateInfo alloc_info = {
          .sType =
              VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
          .descriptorSetCount = 1,
          .pDescriptorCounts = (uint32_t[1]){incoming_mat_count},
      };
      tb_rnd_resize_desc_pool(rnd_sys, &create_info, &mat_sys->set_layout,
                              &alloc_info, &mat_sys->mat_pool, 1);
    }
  } else {
    // No work to do :)
    return;
  }

  // Just write all material buffer info to the descriptor set
  tb_auto write = (VkWriteDescriptorSet){0};
  {
    tb_auto mat_count = TB_DYN_ARR_SIZE(mat_sys->materials);
    tb_auto buffer_info =
        tb_alloc_nm_tp(mat_sys->tmp_alloc, mat_count, VkDescriptorBufferInfo);

    TB_DYN_ARR_FOREACH(mat_sys->materials, i) {
      tb_auto material = &TB_DYN_ARR_AT(mat_sys->materials, i);

      buffer_info[i] = (VkDescriptorBufferInfo){
          .range = VK_WHOLE_SIZE,
          .buffer = material->gpu_buffer.buffer,
      };
    }

    write = (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .descriptorCount = mat_count,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .dstSet = tb_mat_system_get_set(mat_sys),
        .dstBinding = 2,
        .pBufferInfo = buffer_info,
    };
  }

  tb_rnd_update_descriptors(rnd_sys, 1, &write);
}

void tb_register_material_sys(TbWorld *world) {
  TracyCZoneN(ctx, "Register Material Sys", true);
  ecs_world_t *ecs = world->ecs;

  ECS_COMPONENT_DEFINE(ecs, TbMaterialSystem);

  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);

  TbMaterialSystem sys =
      create_material_system(world->gp_alloc, world->tmp_alloc, rnd_sys);

  ECS_SYSTEM(ecs, tick_material_system, EcsPreStore,
             TbMaterialSystem(TbMaterialSystem));

  // Sets a singleton based on the value at a pointer
  ecs_set_ptr(ecs, ecs_id(TbMaterialSystem), TbMaterialSystem, &sys);
  TracyCZoneEnd(ctx);
}

void tb_unregister_material_sys(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;

  TbMaterialSystem *sys = ecs_singleton_get_mut(ecs, TbMaterialSystem);
  destroy_material_system(sys);
  ecs_singleton_remove(ecs, TbMaterialSystem);
}

VkDescriptorSetLayout tb_mat_system_get_set_layout(TbMaterialSystem *self) {
  return self->set_layout;
}

TbMaterialId tb_mat_system_load_material(ecs_world_t *ecs,
                                         TbMaterialSystem *self,
                                         const char *path,
                                         const cgltf_material *mat) {
  TracyCZoneN(ctx, "Load Material", true);
  VkResult err = VK_SUCCESS;

  // Hash the materials's path and gltf name to get the id
  uint64_t id = tb_hash(0, (const uint8_t *)path, SDL_strlen(path));
  id = tb_hash(id, (const uint8_t *)mat->name, SDL_strlen(mat->name));

  uint32_t index = find_mat_by_hash(self, id);

  // Material was not found, load it now
  if (index == SDL_MAX_UINT32) {
    index = TB_DYN_ARR_SIZE(self->materials);
    TB_DYN_ARR_APPEND(self->materials, (TbMaterial){0});
    tb_auto material = &TB_DYN_ARR_AT(self->materials, index);

    // Load material
    {
      // Find a suitable texture transform from the material
      cgltf_texture_transform tex_trans = {
          .scale = {1, 1},
      };
      if (mat) {
        // Expecting that all textures in the material share the same texture
        // transform
        if (mat->has_pbr_metallic_roughness) {
          tex_trans = mat->pbr_metallic_roughness.base_color_texture.transform;
        } else if (mat->has_pbr_specular_glossiness) {
          tex_trans = mat->pbr_specular_glossiness.diffuse_texture.transform;
        } else if (mat->normal_texture.texture) {
          tex_trans = mat->normal_texture.transform;
        }
      }

      // Determine feature permutations and load textures
      {
        TbTexture metal_rough = tb_get_default_metal_rough_tex(ecs);
        TbTexture color = tb_get_default_color_tex(ecs);
        TbTexture normal = tb_get_default_normal_tex(ecs);

        TbMaterialPerm feat_perm = 0;
        if (mat->has_pbr_metallic_roughness) {
          feat_perm |= GLTF_PERM_PBR_METALLIC_ROUGHNESS;

          if (mat->pbr_metallic_roughness.metallic_roughness_texture.texture !=
              NULL) {
            feat_perm |= GLTF_PERM_PBR_METAL_ROUGH_TEX;
            metal_rough = tb_tex_sys_load_mat_tex(ecs, path, mat->name,
                                                  TB_TEX_USAGE_METAL_ROUGH);
          }
          if (mat->pbr_metallic_roughness.base_color_texture.texture != NULL) {
            feat_perm |= GLTF_PERM_BASE_COLOR_MAP;
            color = tb_tex_sys_load_mat_tex(ecs, path, mat->name,
                                            TB_TEX_USAGE_COLOR);
          }

          material->metal_rough_map = metal_rough;
          material->color_map = color;
        }
        if (mat->has_pbr_specular_glossiness) {
          feat_perm |= GLTF_PERM_PBR_SPECULAR_GLOSSINESS;

          if (mat->pbr_specular_glossiness.diffuse_texture.texture != NULL) {
            feat_perm |= GLTF_PERM_BASE_COLOR_MAP;
            color = tb_tex_sys_load_mat_tex(ecs, path, mat->name,
                                            TB_TEX_USAGE_COLOR);
          }
          material->color_map = color;
        }
        if (mat->has_clearcoat) {
          feat_perm |= GLTF_PERM_CLEARCOAT;
        }
        if (mat->has_transmission) {
          feat_perm |= GLTF_PERM_TRANSMISSION;
        }
        if (mat->has_volume) {
          feat_perm |= GLTF_PERM_VOLUME;
        }
        if (mat->has_ior) {
          feat_perm |= GLTF_PERM_IOR;
        }
        if (mat->has_specular) {
          feat_perm |= GLTF_PERM_SPECULAR;
        }
        if (mat->has_sheen) {
          feat_perm |= GLTF_PERM_SHEEN;
        }
        if (mat->unlit) {
          feat_perm |= GLTF_PERM_UNLIT;
        }
        if (mat->alpha_mode == cgltf_alpha_mode_mask) {
          feat_perm |= GLTF_PERM_ALPHA_CLIP;
        }
        if (mat->alpha_mode == cgltf_alpha_mode_blend) {
          feat_perm |= GLTF_PERM_ALPHA_BLEND;
        }
        if (mat->double_sided) {
          feat_perm |= GLTF_PERM_DOUBLE_SIDED;
        }
        if (mat->normal_texture.texture != NULL) {
          feat_perm |= GLTF_PERM_NORMAL_MAP;
          normal = tb_tex_sys_load_mat_tex(ecs, path, mat->name,
                                           TB_TEX_USAGE_NORMAL);
        }
        material->normal_map = normal;

        material->permutation = feat_perm;
      }

      // Gather material buffer data and copy to the GPU
      {
        TbGLTFMaterialData data = {
            .tex_transform =
                {
                    .offset =
                        (float2){
                            tex_trans.offset[0],
                            tex_trans.offset[1],
                        },
                    .scale =
                        (float2){
                            tex_trans.scale[0],
                            tex_trans.scale[1],
                        },
                },
            .pbr_metallic_roughness =
                {
                    .base_color_factor =
                        tb_atof4(mat->pbr_metallic_roughness.base_color_factor),
                    .metal_rough_factors =
                        {mat->pbr_metallic_roughness.metallic_factor,
                         mat->pbr_metallic_roughness.roughness_factor},
                },
            .pbr_specular_glossiness.diffuse_factor =
                tb_atof4(mat->pbr_specular_glossiness.diffuse_factor),
            .specular = tb_f3tof4(
                tb_atof3(mat->pbr_specular_glossiness.specular_factor),
                mat->pbr_specular_glossiness.glossiness_factor),
            .emissives = tb_f3tof4(tb_atof3(mat->emissive_factor), 1.0f),
            .perm = material->permutation,
            // TODO: We shouldn't do this until textures are loaded
            .color_idx = 0,
            .normal_idx = 0,
            .pbr_idx = 0,
        };

        if (mat->has_emissive_strength) {
          data.emissives[3] = mat->emissive_strength.emissive_strength;
        }
        if (mat->alpha_mode == cgltf_alpha_mode_mask) {
          data.sheen_alpha[3] = mat->alpha_cutoff;
        }

        // Send data to GPU
        {
          VkBufferCreateInfo create_info = {
              .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
              .size = sizeof(TbGLTFMaterialData),
              .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                       VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
          };
          // HACK: Known alignment for uniform buffers
          err = tb_rnd_sys_create_gpu_buffer2_tmp(self->rnd_sys, &create_info,
                                                  &data, mat->name,
                                                  &material->gpu_buffer, 0x40);
          TB_VK_CHECK_RET(
              err,
              "Failed to allocate material uniform data in tmp host buffer",
              InvalidMaterialId);
        }
      }
    }

    material->id = (TbMaterialId){id, index};
    material->ref_count = 0;
  }

  TB_DYN_ARR_AT(self->materials, index).ref_count++;

  TracyCZoneEnd(ctx);
  return (TbMaterialId){id, index};
}

TbMaterialPerm tb_mat_system_get_perm(TbMaterialSystem *self,
                                      TbMaterialId mat) {
  const uint32_t index = mat.idx;
  if (index == SDL_MAX_UINT32) {
    TB_CHECK_RETURN(false, "Failed to find material to get permutation", 0);
  }
  return TB_DYN_ARR_AT(self->materials, index).permutation;
}

VkDescriptorSet tb_mat_system_get_set(TbMaterialSystem *self) {
  return self->mat_pool.sets[0];
}

void tb_mat_system_release_material_ref(TbMaterialSystem *self,
                                        TbMaterialId mat) {
  const uint32_t index = mat.idx;
  if (index == SDL_MAX_UINT32) {
    TB_CHECK(false, "Failed to find material to release");
    return;
  }
  TbMaterial *material = &TB_DYN_ARR_AT(self->materials, index);

  if (material->ref_count == 0) {
    TB_CHECK(false, "Tried to release reference to material with 0 ref count");
    return;
  }

  material->ref_count--;

  // TODO: Should materials have to release texture references?

  if (material->ref_count == 0) {
    // Free the mesh at this index
    VmaAllocator vma_alloc = self->rnd_sys->vma_alloc;

    TbBuffer *gpu_buf = &material->gpu_buffer;
    vmaDestroyBuffer(vma_alloc, gpu_buf->buffer, gpu_buf->alloc);
    *gpu_buf = (TbBuffer){0};
  }
}
