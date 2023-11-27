#include "materialsystem.h"

#include "cgltf.h"
#include "common.hlsli"
#include "gltf.hlsli"
#include "hash.h"
#include "profiling.h"
#include "rendersystem.h"
#include "texturesystem.h"
#include "world.h"

#include <flecs.h>

typedef struct TbMaterial {
  TbMaterialId id;
  uint32_t ref_count;
  TbMaterialPerm permutation;
  TbBuffer gpu_buffer;
  TbTextureId color_map;
  TbTextureId normal_map;
  TbTextureId metal_rough_map;
} TbMaterial;

uint32_t find_mat_by_id(TbMaterialSystem *self, TbMaterialId id) {
  TB_DYN_ARR_FOREACH(self->materials, i) {
    if (TB_DYN_ARR_AT(self->materials, i).id == id) {
      return i;
      break;
    }
  }
  return SDL_MAX_UINT32;
}

TbMaterialSystem create_material_system(TbAllocator std_alloc,
                                        TbAllocator tmp_alloc,
                                        TbRenderSystem *rnd_sys,
                                        TbTextureSystem *tex_sys) {
  TbMaterialSystem sys = {
      .std_alloc = std_alloc,
      .tmp_alloc = tmp_alloc,
      .render_system = rnd_sys,
      .texture_system = tex_sys,
  };

  TB_DYN_ARR_RESET(sys.materials, std_alloc, 1);

  VkDevice device = rnd_sys->render_thread->device;
  const VkAllocationCallbacks *vk_alloc = &rnd_sys->vk_host_alloc_cb;

  VkResult err = VK_SUCCESS;

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
    err = vkCreateSampler(device, &create_info, vk_alloc, &sys.sampler);
    TB_VK_CHECK(err, "Failed to create material sampler");
    SET_VK_NAME(device, sys.sampler, VK_OBJECT_TYPE_SAMPLER,
                "Material Sampler");
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
    err = vkCreateSampler(device, &create_info, vk_alloc, &sys.shadow_sampler);
    TB_VK_CHECK(err, "Failed to create material shadow sampler");
    SET_VK_NAME(device, sys.shadow_sampler, VK_OBJECT_TYPE_SAMPLER,
                "Material Shadow Sampler");
  }

  // Create descriptor set layout for materials
  {
    VkDescriptorSetLayoutBinding bindings[6] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
         VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT, NULL},
        {1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
         NULL},
        {2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
         NULL},
        {3, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
         NULL},
        {4, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
         &sys.sampler},
        {5, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
         &sys.shadow_sampler},
    };
    VkDescriptorSetLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 6,
        .pBindings = bindings,
    };
    err = vkCreateDescriptorSetLayout(device, &create_info, vk_alloc,
                                      &sys.set_layout);
    TB_VK_CHECK(err, "Failed to create material descriptor set layout");
    SET_VK_NAME(device, sys.set_layout, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                "Material DS Layout");
  }

  // Create a default material
  // It uses the metallic roughness flow
  // it does not supply textures, those will be provided by default since
  // it is marked to use pbr metal rough but provides no texture views
  cgltf_material *default_mat = tb_alloc_tp(std_alloc, cgltf_material);
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
  TbRenderSystem *render_system = self->render_system;

  tb_free(self->std_alloc, (void *)self->default_material);

  tb_rnd_destroy_set_layout(render_system, self->set_layout);
  tb_rnd_destroy_sampler(render_system, self->sampler);
  tb_rnd_destroy_sampler(render_system, self->shadow_sampler);

  tb_rnd_destroy_descriptor_pool(render_system, self->mat_set_pool);

  TB_DYN_ARR_FOREACH(self->materials, i) {
    if (TB_DYN_ARR_AT(self->materials, i).ref_count != 0) {
      TB_CHECK(false, "Leaking materials");
    }
  }

  TB_DYN_ARR_DESTROY(self->materials);

  *self = (TbMaterialSystem){0};
}

void tb_register_material_sys(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;
  ECS_COMPONENT(ecs, TbRenderSystem);
  ECS_COMPONENT(ecs, TbTextureSystem);
  ECS_COMPONENT(ecs, TbMaterialSystem);

  TbRenderSystem *rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  TbTextureSystem *tex_sys = ecs_singleton_get_mut(ecs, TbTextureSystem);

  TbMaterialSystem sys = create_material_system(
      world->std_alloc, world->tmp_alloc, rnd_sys, tex_sys);

  // Sets a singleton based on the value at a pointer
  ecs_set_ptr(ecs, ecs_id(TbMaterialSystem), TbMaterialSystem, &sys);
}

void tb_unregister_material_sys(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;
  ECS_COMPONENT(ecs, TbMaterialSystem);
  TbMaterialSystem *sys = ecs_singleton_get_mut(ecs, TbMaterialSystem);
  destroy_material_system(sys);
  ecs_singleton_remove(ecs, TbMaterialSystem);
}

VkDescriptorSetLayout tb_mat_system_get_set_layout(TbMaterialSystem *self) {
  return self->set_layout;
}

TbMaterialId tb_mat_system_load_material(TbMaterialSystem *self,
                                         const char *path,
                                         const cgltf_material *mat) {
  TracyCZoneN(ctx, "Load Material", true);
  VkResult err = VK_SUCCESS;

  // Hash the materials's path and gltf name to get the id
  TbMaterialId id = tb_sdbm(0, (const uint8_t *)path, SDL_strlen(path));
  id = tb_sdbm(id, (const uint8_t *)mat->name, SDL_strlen(mat->name));

  VkDevice device = self->render_system->render_thread->device;

  uint32_t index = find_mat_by_id(self, id);

  // Material was not found, load it now
  if (index == SDL_MAX_UINT32) {
    // Determine if we will need to resize the descriptor pool after we append
    // this new material
    bool resize_pool = false;
    {
      const uint32_t new_count = TB_DYN_ARR_SIZE(self->materials) + 1;
      resize_pool = new_count >= self->materials.capacity;
    }

    // Resize to fit a new material
    {
      index = TB_DYN_ARR_SIZE(self->materials);
      TB_DYN_ARR_RESIZE(self->materials, index + 1);
    }

    if (resize_pool) {
      TbAllocator alloc = self->std_alloc;
      self->mat_sets = tb_realloc_nm_tp(
          alloc, self->mat_sets, self->materials.capacity, VkDescriptorSet);

      // Resize descriptor pool
      {
        const uint32_t desc_cap = self->materials.capacity;
        // *4 because we know we have 4 bindings
        const uint32_t max_sets = desc_cap * 4;

        if (self->mat_set_pool) {
          tb_rnd_destroy_descriptor_pool(self->render_system,
                                         self->mat_set_pool);
        }

// NOTE: Due to behavior in AMD drivers we use max_sets for the
// descriptor count
#define POOL_SIZE_COUNT 2
        VkDescriptorPoolSize pool_sizes[POOL_SIZE_COUNT] = {
            // One binding is a uniform buffer
            {
                .descriptorCount = max_sets,
                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            },
            // 3 Bindings are sampled images
            {
                .descriptorCount = max_sets,
                .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            },
        };

        VkDescriptorPoolCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = max_sets,
            .poolSizeCount = POOL_SIZE_COUNT,
            .pPoolSizes = pool_sizes,
            .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        };
#undef POOL_SIZE_COUNT
        err = tb_rnd_create_descriptor_pool(self->render_system, &create_info,
                                            "Material Set Pool",
                                            &self->mat_set_pool);
        TB_VK_CHECK(err, "Failed to create material descriptor pool");

        // Reallocate descriptors
        VkDescriptorSetLayout *layouts =
            tb_alloc_nm_tp(self->tmp_alloc, desc_cap, VkDescriptorSetLayout);
        for (uint32_t i = 0; i < desc_cap; ++i) {
          layouts[i] = self->set_layout;
        }

        VkDescriptorSetAllocateInfo alloc_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorSetCount = desc_cap,
            .descriptorPool = self->mat_set_pool,
            .pSetLayouts = layouts,
        };
        err =
            vkAllocateDescriptorSets(self->render_system->render_thread->device,
                                     &alloc_info, self->mat_sets);
        TB_VK_CHECK(err, "Failed to re-allocate material descriptor sets");

        // Re-write bindings to re-allocated descriptor sets
        uint32_t material_count = index;
        if (material_count > 0) {
          const uint32_t write_count = material_count * 4;
          VkWriteDescriptorSet *writes = tb_alloc_nm_tp(
              self->tmp_alloc, write_count, VkWriteDescriptorSet);
          VkDescriptorBufferInfo *buffer_info = tb_alloc_nm_tp(
              self->tmp_alloc, material_count, VkDescriptorBufferInfo);
          VkDescriptorImageInfo *image_info = tb_alloc_nm_tp(
              self->tmp_alloc, material_count * 3, VkDescriptorImageInfo);
          for (uint32_t i = 0; i < material_count; ++i) {
            uint32_t write_idx = i * 4;

            uint32_t img_idx = i * 3;

            VkDescriptorBufferInfo *buf_info = &buffer_info[i];
            VkDescriptorImageInfo *color_info = &image_info[img_idx + 0];
            VkDescriptorImageInfo *normal_info = &image_info[img_idx + 1];
            VkDescriptorImageInfo *metal_rough_info = &image_info[img_idx + 2];

            TbMaterial *material = &TB_DYN_ARR_AT(self->materials, i);
            VkDescriptorSet set = self->mat_sets[i];

            *buf_info = (VkDescriptorBufferInfo){
                .buffer = material->gpu_buffer.buffer,
                .range = sizeof(GLTFMaterialData),
            };
            *color_info = (VkDescriptorImageInfo){
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .imageView = tb_tex_system_get_image_view(self->texture_system,
                                                          material->color_map),
            };
            *normal_info = (VkDescriptorImageInfo){
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .imageView = tb_tex_system_get_image_view(self->texture_system,
                                                          material->normal_map),
            };
            *metal_rough_info = (VkDescriptorImageInfo){
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .imageView = tb_tex_system_get_image_view(
                    self->texture_system, material->metal_rough_map),
            };

            writes[write_idx + 0] = (VkWriteDescriptorSet){
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = set,
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo = buf_info,
            };
            writes[write_idx + 1] = (VkWriteDescriptorSet){
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = set,
                .dstBinding = 1,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                .pImageInfo = color_info,
            };
            writes[write_idx + 2] = (VkWriteDescriptorSet){
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = set,
                .dstBinding = 2,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                .pImageInfo = normal_info,
            };
            writes[write_idx + 3] = (VkWriteDescriptorSet){
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = set,
                .dstBinding = 3,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                .pImageInfo = metal_rough_info,
            };
          }
          vkUpdateDescriptorSets(device, write_count, writes, 0, NULL);
        }
      }
    }

    TbMaterial *material = &TB_DYN_ARR_AT(self->materials, index);

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

      // Gather uniform buffer data and copy to the GPU
      {
        GLTFMaterialData data = {
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
              .size = sizeof(GLTFMaterialData),
              .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                       VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
          };
          // HACK: Known alignment for uniform buffers
          err = tb_rnd_sys_create_gpu_buffer2_tmp(
              self->render_system, &create_info, &data, mat->name,
              &material->gpu_buffer, 0x40);
          TB_VK_CHECK_RET(
              err,
              "Failed to allocate material uniform data in tmp host buffer",
              InvalidMaterialId);
        }
      }

      // Determine feature permutations and load textures
      {
        // GLTFpack strips image names so we have to synthesize something
        // ourselves
        char image_name[100] = {0};

        uint64_t feat_perm = 0;
        if (mat->has_pbr_metallic_roughness) {
          feat_perm |= GLTF_PERM_PBR_METALLIC_ROUGHNESS;

          TbTextureId metal_rough_id =
              self->texture_system->default_metal_rough_tex;
          TbTextureId color_id = self->texture_system->default_color_tex;

          if (mat->pbr_metallic_roughness.metallic_roughness_texture.texture !=
              NULL) {
            feat_perm |= GLTF_PERM_PBR_METAL_ROUGH_TEX;
            SDL_snprintf(image_name, 100, "%s_metal", mat->name);
            metal_rough_id = tb_tex_system_load_texture(
                self->texture_system, path, image_name,
                mat->pbr_metallic_roughness.metallic_roughness_texture.texture);
          } else {
            tb_tex_system_take_tex_ref(self->texture_system, metal_rough_id);
          }
          if (mat->pbr_metallic_roughness.base_color_texture.texture != NULL) {
            feat_perm |= GLTF_PERM_BASE_COLOR_MAP;
            SDL_snprintf(image_name, 100, "%s_color", mat->name);
            color_id = tb_tex_system_load_texture(
                self->texture_system, path, image_name,
                mat->pbr_metallic_roughness.base_color_texture.texture);
          } else {
            tb_tex_system_take_tex_ref(self->texture_system, color_id);
          }

          material->metal_rough_map = metal_rough_id;
          material->color_map = color_id;
        }
        if (mat->has_pbr_specular_glossiness) {
          feat_perm |= GLTF_PERM_PBR_SPECULAR_GLOSSINESS;
          TbTextureId color_id = self->texture_system->default_color_tex;
          if (mat->pbr_specular_glossiness.diffuse_texture.texture != NULL) {
            feat_perm |= GLTF_PERM_BASE_COLOR_MAP;
            SDL_snprintf(image_name, 100, "%s_color", mat->name);
            color_id = tb_tex_system_load_texture(
                self->texture_system, path, image_name,
                mat->pbr_metallic_roughness.base_color_texture.texture);
          } else {
            tb_tex_system_take_tex_ref(self->texture_system, color_id);
          }
          material->color_map = color_id;
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
        TbTextureId normal_id = self->texture_system->default_normal_tex;
        if (mat->normal_texture.texture != NULL) {
          feat_perm |= GLTF_PERM_NORMAL_MAP;
          SDL_snprintf(image_name, 100, "%s_normal", mat->name);
          normal_id =
              tb_tex_system_load_texture(self->texture_system, path, image_name,
                                         mat->normal_texture.texture);
        } else {
          tb_tex_system_take_tex_ref(self->texture_system, normal_id);
        }
        material->normal_map = normal_id;

        material->permutation = feat_perm;
      }
    }

    // Write descriptors
    {
      VkWriteDescriptorSet writes[4] = {0};
      writes[0] = (VkWriteDescriptorSet){
          .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          .dstSet = self->mat_sets[index],
          .dstBinding = 0,
          .dstArrayElement = 0,
          .descriptorCount = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          .pBufferInfo =
              &(VkDescriptorBufferInfo){
                  .buffer = material->gpu_buffer.buffer,
                  .range = sizeof(GLTFMaterialData),
              },
      };
      writes[1] = (VkWriteDescriptorSet){
          .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          .dstSet = self->mat_sets[index],
          .dstBinding = 1,
          .dstArrayElement = 0,
          .descriptorCount = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
          .pImageInfo =
              &(VkDescriptorImageInfo){
                  .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  .imageView = tb_tex_system_get_image_view(
                      self->texture_system, material->color_map),
              },
      };
      writes[2] = (VkWriteDescriptorSet){
          .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          .dstSet = self->mat_sets[index],
          .dstBinding = 2,
          .dstArrayElement = 0,
          .descriptorCount = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
          .pImageInfo =
              &(VkDescriptorImageInfo){
                  .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  .imageView = tb_tex_system_get_image_view(
                      self->texture_system, material->normal_map),
              },
      };
      writes[3] = (VkWriteDescriptorSet){
          .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          .dstSet = self->mat_sets[index],
          .dstBinding = 3,
          .dstArrayElement = 0,
          .descriptorCount = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
          .pImageInfo =
              &(VkDescriptorImageInfo){
                  .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  .imageView = tb_tex_system_get_image_view(
                      self->texture_system, material->metal_rough_map),
              },
      };
      vkUpdateDescriptorSets(device, 4, writes, 0, NULL);
    }
    material->id = id;
    material->ref_count = 0;
  } else {
    TbMaterial *material = &TB_DYN_ARR_AT(self->materials, index);
    // If the material was already loaded, go through the textures and grab
    // a reference so it's known that the texture is in use
    tb_tex_system_take_tex_ref(self->texture_system, material->color_map);
    tb_tex_system_take_tex_ref(self->texture_system, material->normal_map);
    tb_tex_system_take_tex_ref(self->texture_system, material->metal_rough_map);
  }

  TB_DYN_ARR_AT(self->materials, index).ref_count++;

  TracyCZoneEnd(ctx);
  return id;
}

TbMaterialPerm tb_mat_system_get_perm(TbMaterialSystem *self,
                                      TbMaterialId mat) {
  const uint32_t index = find_mat_by_id(self, mat);
  if (index == SDL_MAX_UINT32) {
    TB_CHECK_RETURN(false, "Failed to find material to get permutation", 0);
  }
  return TB_DYN_ARR_AT(self->materials, index).permutation;
}

VkDescriptorSet tb_mat_system_get_set(TbMaterialSystem *self,
                                      TbMaterialId mat) {
  const uint32_t index = find_mat_by_id(self, mat);
  if (index == SDL_MAX_UINT32) {
    TB_CHECK_RETURN(false, "Failed to find material to get set", 0);
  }
  return self->mat_sets[index];
}

void tb_mat_system_release_material_ref(TbMaterialSystem *self,
                                        TbMaterialId mat) {
  const uint32_t index = find_mat_by_id(self, mat);
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

  // Materials should always release their texture references since materials
  // don't own their textures
  tb_tex_system_release_texture_ref(self->texture_system, material->color_map);
  tb_tex_system_release_texture_ref(self->texture_system, material->normal_map);
  tb_tex_system_release_texture_ref(self->texture_system,
                                    material->metal_rough_map);

  if (material->ref_count == 0) {
    // Free the mesh at this index
    VmaAllocator vma_alloc = self->render_system->vma_alloc;

    TbBuffer *gpu_buf = &material->gpu_buffer;
    vmaDestroyBuffer(vma_alloc, gpu_buf->buffer, gpu_buf->alloc);
    *gpu_buf = (TbBuffer){0};
  }
}
