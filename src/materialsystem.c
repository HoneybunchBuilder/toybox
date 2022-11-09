#include "materialsystem.h"

#include "cgltf.h"
#include "common.hlsli"
#include "gltf.hlsli"
#include "hash.h"
#include "rendersystem.h"
#include "texturesystem.h"
#include "world.h"

bool create_material_system(MaterialSystem *self,
                            const MaterialSystemDescriptor *desc,
                            uint32_t system_dep_count,
                            System *const *system_deps) {
  // Find the necessary systems
  RenderSystem *render_system = (RenderSystem *)tb_find_system_dep_self_by_id(
      system_deps, system_dep_count, RenderSystemId);
  TB_CHECK_RETURN(render_system,
                  "Failed to find render system which materials depend on",
                  false);
  TextureSystem *texture_system =
      (TextureSystem *)tb_find_system_dep_self_by_id(
          system_deps, system_dep_count, TextureSystemId);
  TB_CHECK_RETURN(texture_system,
                  "Failed to find texture system which materials depend on",
                  false);

  *self = (MaterialSystem){
      .render_system = render_system,
      .texture_system = texture_system,
      .tmp_alloc = desc->tmp_alloc,
      .std_alloc = desc->std_alloc,
  };

  VkDevice device = self->render_system->render_thread->device;
  const VkAllocationCallbacks *vk_alloc =
      &self->render_system->vk_host_alloc_cb;

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
        .anisotropyEnable = VK_FALSE,
        .maxAnisotropy = 1.0f,
        .maxLod = 14.0f, // Hack; known number of mips for 8k textures
        .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
    };
    err = vkCreateSampler(device, &create_info, vk_alloc, &self->sampler);
    TB_VK_CHECK_RET(err, "Failed to create material sampler", false);
    SET_VK_NAME(device, self->sampler, VK_OBJECT_TYPE_SAMPLER,
                "Material Sampler");
  }

  // Create descriptor set layout for materials
  {
    VkDescriptorSetLayoutBinding bindings[5] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
         VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT, NULL},
        {1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
         NULL},
        {2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
         NULL},
        {3, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
         NULL},
        {4, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
         &self->sampler},
    };
    VkDescriptorSetLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 5,
        .pBindings = bindings,
    };
    err = vkCreateDescriptorSetLayout(device, &create_info, vk_alloc,
                                      &self->set_layout);
    TB_VK_CHECK_RET(err, "Failed to create material descriptor set layout",
                    false);
    SET_VK_NAME(device, self->set_layout, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                "Material DS Layout");
  }

  return true;
}

void destroy_material_system(MaterialSystem *self) {
  RenderSystem *render_system = self->render_system;
  VkDevice device = render_system->render_thread->device;
  const VkAllocationCallbacks *vk_alloc = &render_system->vk_host_alloc_cb;

  tb_rnd_destroy_set_layout(render_system, self->set_layout);
  tb_rnd_destroy_sampler(render_system, self->sampler);

  vkDestroyDescriptorPool(device, self->mat_set_pool, vk_alloc);

  for (uint32_t i = 0; i < self->mat_count; ++i) {
    if (self->mat_ref_counts[i] != 0) {
      TB_CHECK(false, "Leaking materials");
    }
  }

  *self = (MaterialSystem){0};
}

void tick_material_system(MaterialSystem *self, const SystemInput *input,
                          SystemOutput *output, float delta_seconds) {
  (void)self;
  (void)input;
  (void)output;
  (void)delta_seconds;
}

TB_DEFINE_SYSTEM(material, MaterialSystem, MaterialSystemDescriptor)

void tb_material_system_descriptor(SystemDescriptor *desc,
                                   const MaterialSystemDescriptor *mat_desc) {
  desc->name = "Material";
  desc->size = sizeof(MaterialSystem);
  desc->id = MaterialSystemId;
  desc->desc = (InternalDescriptor)mat_desc;
  SDL_memset(desc->deps, 0,
             sizeof(SystemComponentDependencies) * MAX_DEPENDENCY_SET_COUNT);
  desc->dep_count = 0;
  desc->system_dep_count = 2;
  desc->system_deps[0] = RenderSystemId;
  desc->system_deps[1] = TextureSystemId;
  desc->create = tb_create_material_system;
  desc->destroy = tb_destroy_material_system;
  desc->tick = tb_tick_material_system;
}

uint32_t find_mat_by_id(MaterialSystem *self, TbMaterialId id) {
  for (uint32_t i = 0; i < self->mat_count; ++i) {
    if (self->mat_ids[i] == id) {
      return i;
      break;
    }
  }
  return SDL_MAX_UINT32;
}

VkDescriptorSetLayout tb_mat_system_get_set_layout(MaterialSystem *self) {
  return self->set_layout;
}

TbMaterialId tb_mat_system_load_material(MaterialSystem *self, const char *path,
                                         const cgltf_material *mat) {
  VkResult err = VK_SUCCESS;

  // Hash the materials's path and gltf name to get the id
  TbMaterialId id = sdbm(0, (const uint8_t *)path, SDL_strlen(path));
  id = sdbm(id, (const uint8_t *)mat->name, SDL_strlen(mat->name));

  VkDevice device = self->render_system->render_thread->device;
  const VkAllocationCallbacks *vk_alloc =
      &self->render_system->vk_host_alloc_cb;

  uint32_t index = find_mat_by_id(self, id);

  // Material was not found, load it now
  if (index == SDL_MAX_UINT32) {
    // Resize collections if necessary
    const uint32_t new_count = self->mat_count + 1;
    if (new_count > self->mat_max) {
      const uint32_t new_max = new_count * 2;

      Allocator alloc = self->std_alloc;

      self->mat_ids =
          tb_realloc_nm_tp(alloc, self->mat_ids, new_max, TbMaterialId);
      self->mat_perms =
          tb_realloc_nm_tp(alloc, self->mat_perms, new_max, uint64_t);
      self->mat_gpu_buffers =
          tb_realloc_nm_tp(alloc, self->mat_gpu_buffers, new_max, TbBuffer);
      self->mat_color_maps =
          tb_realloc_nm_tp(alloc, self->mat_color_maps, new_max, TbTextureId);
      self->mat_normal_maps =
          tb_realloc_nm_tp(alloc, self->mat_normal_maps, new_max, TbTextureId);
      self->mat_metal_rough_maps = tb_realloc_nm_tp(
          alloc, self->mat_metal_rough_maps, new_max, TbTextureId);
      self->mat_sets =
          tb_realloc_nm_tp(alloc, self->mat_sets, new_max, VkDescriptorSet);
      self->mat_ref_counts =
          tb_realloc_nm_tp(alloc, self->mat_ref_counts, new_max, uint32_t);

      self->mat_max = new_max;

      // Resize descriptor pool
      {
        const uint32_t max_sets =
            new_max * 4; // *4 because we know we have 4 bindings

        if (self->mat_set_pool) {
          vkDestroyDescriptorPool(device, self->mat_set_pool, vk_alloc);
        }

        // NOTE: Due to behavior in AMD drivers we use max_sets for the
        // descriptor count
        const uint32_t pool_size_count = 2;
        VkDescriptorPoolSize pool_sizes[pool_size_count] = {
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
            .poolSizeCount = pool_size_count,
            .pPoolSizes = pool_sizes,
            .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        };
        err = vkCreateDescriptorPool(device, &create_info, vk_alloc,
                                     &self->mat_set_pool);
        TB_VK_CHECK(err, "Failed to create material descriptor pool");
        SET_VK_NAME(device, self->mat_set_pool, VK_OBJECT_TYPE_DESCRIPTOR_POOL,
                    "Material Set Pool");

        // Reallocate descriptors
        VkDescriptorSetLayout *layouts =
            tb_alloc_nm_tp(self->tmp_alloc, new_max, VkDescriptorSetLayout);
        for (uint32_t i = 0; i < new_max; ++i) {
          layouts[i] = self->set_layout;
        }

        VkDescriptorSetAllocateInfo alloc_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorSetCount = new_max,
            .descriptorPool = self->mat_set_pool,
            .pSetLayouts = layouts,
        };
        err =
            vkAllocateDescriptorSets(self->render_system->render_thread->device,
                                     &alloc_info, self->mat_sets);
        TB_VK_CHECK(err, "Failed to re-allocate material descriptor sets");

        // Re-write bindings to re-allocated descriptor sets
        if (self->mat_count > 0) {
          const uint32_t write_count = self->mat_count * 4;
          VkWriteDescriptorSet *writes = tb_alloc_nm_tp(
              self->tmp_alloc, write_count, VkWriteDescriptorSet);
          VkDescriptorBufferInfo *buffer_info = tb_alloc_nm_tp(
              self->tmp_alloc, self->mat_count, VkDescriptorBufferInfo);
          VkDescriptorImageInfo *image_info = tb_alloc_nm_tp(
              self->tmp_alloc, self->mat_count * 3, VkDescriptorImageInfo);
          for (uint32_t i = 0; i < self->mat_count; ++i) {
            uint32_t write_idx = i * 4;

            uint32_t img_idx = i * 3;

            VkDescriptorBufferInfo *buf_info = &buffer_info[i];
            VkDescriptorImageInfo *color_info = &image_info[img_idx + 0];
            VkDescriptorImageInfo *normal_info = &image_info[img_idx + 1];
            VkDescriptorImageInfo *metal_rough_info = &image_info[img_idx + 2];

            *buf_info = (VkDescriptorBufferInfo){
                .buffer = self->mat_gpu_buffers[i].buffer,
                .range = sizeof(GLTFMaterialData),
            };
            *color_info = (VkDescriptorImageInfo){
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .imageView = tb_tex_system_get_image_view(
                    self->texture_system, self->mat_color_maps[i]),
            };
            *normal_info = (VkDescriptorImageInfo){
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .imageView = tb_tex_system_get_image_view(
                    self->texture_system, self->mat_normal_maps[i]),
            };
            *metal_rough_info = (VkDescriptorImageInfo){
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .imageView = tb_tex_system_get_image_view(
                    self->texture_system, self->mat_metal_rough_maps[i]),
            };

            writes[write_idx + 0] = (VkWriteDescriptorSet){
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = self->mat_sets[i],
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo = buf_info,
            };
            writes[write_idx + 1] = (VkWriteDescriptorSet){
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = self->mat_sets[i],
                .dstBinding = 1,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                .pImageInfo = color_info,
            };
            writes[write_idx + 2] = (VkWriteDescriptorSet){
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = self->mat_sets[i],
                .dstBinding = 2,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                .pImageInfo = normal_info,
            };
            writes[write_idx + 3] = (VkWriteDescriptorSet){
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = self->mat_sets[i],
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

    index = self->mat_count;

    // Load material
    {
      // Create gpu uniform buffer
      {
        VkBufferCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = sizeof(GLTFMaterialData),
            .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        };
        const uint32_t name_max = 100;
        char name[name_max] = {0};
        SDL_snprintf(name, name_max, "%s Uniform Buffer", mat->name);
        err = tb_rnd_sys_alloc_gpu_buffer(self->render_system, &create_info,
                                          name, &self->mat_gpu_buffers[index]);
        TB_VK_CHECK_RET(err,
                        "Failed to allocate space for material uniform buffer",
                        InvalidMaterialId);
      }

      // Find a suitable texture transform from the material
      const cgltf_texture_transform *tex_trans = NULL;
      {
        // Expecting that all textures in the material share the same texture
        // transform
        if (mat->has_pbr_metallic_roughness) {
          tex_trans = &mat->pbr_metallic_roughness.base_color_texture.transform;
        } else if (mat->has_pbr_specular_glossiness) {
          tex_trans = &mat->pbr_specular_glossiness.diffuse_texture.transform;
        } else if (mat->normal_texture.texture) {
          tex_trans = &mat->normal_texture.transform;
        }
      }

      // Gather uniform buffer data and copy to the tmp host buffer
      TbHostBuffer host_buf = {0};
      {
        GLTFMaterialData data = {.clearcoat_factor = 0};
        {
          data.tex_transform = (TextureTransform){
              .offset =
                  (float2){
                      tex_trans->offset[0],
                      tex_trans->offset[1],
                  },
              .scale =
                  (float2){
                      tex_trans->scale[0],
                      tex_trans->scale[1],
                  },
          };
          SDL_memcpy(&data.pbr_metallic_roughness.base_color_factor,
                     mat->pbr_metallic_roughness.base_color_factor,
                     sizeof(float) * 4);
          data.pbr_metallic_roughness.metallic_factor =
              mat->pbr_metallic_roughness.metallic_factor;
          data.pbr_metallic_roughness.roughness_factor =
              mat->pbr_metallic_roughness.roughness_factor;

          SDL_memcpy(&data.pbr_specular_glossiness.diffuse_factor,
                     mat->pbr_specular_glossiness.diffuse_factor,
                     sizeof(float) * 4);
          SDL_memcpy(&data.pbr_specular_glossiness.specular_factor,
                     mat->pbr_specular_glossiness.specular_factor,
                     sizeof(float) * 3);
          data.pbr_specular_glossiness.glossiness_factor =
              mat->pbr_specular_glossiness.glossiness_factor;
        }

        // HACK: Known alignment for uniform buffers
        err = tb_rnd_sys_alloc_tmp_host_buffer(
            self->render_system, sizeof(GLTFMaterialData), 0x40, &host_buf);
        TB_VK_CHECK_RET(
            err, "Failed to allocate material uniform data in tmp host buffer",
            InvalidMaterialId);

        SDL_memcpy(host_buf.ptr, &data, sizeof(GLTFMaterialData));
      }

      // Issue copy of uniform buffer
      {
        const TbBuffer *gpu_buf = &self->mat_gpu_buffers[index];
        BufferCopy upload = {
            .src = host_buf.buffer,
            .dst = gpu_buf->buffer,
            .region =
                {
                    .srcOffset = host_buf.offset,
                    .size = sizeof(GLTFMaterialData),
                },
        };
        tb_rnd_upload_buffers(self->render_system, &upload, 1);
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

          self->mat_metal_rough_maps[index] = metal_rough_id;
          self->mat_color_maps[index] = color_id;
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
          self->mat_color_maps[index] = color_id;
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
        self->mat_normal_maps[index] = normal_id;

        self->mat_perms[index] = feat_perm;
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
                  .buffer = self->mat_gpu_buffers[index].buffer,
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
                      self->texture_system, self->mat_color_maps[index]),
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
                      self->texture_system, self->mat_normal_maps[index]),
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
                      self->texture_system, self->mat_metal_rough_maps[index]),
              },
      };
      vkUpdateDescriptorSets(device, 4, writes, 0, NULL);
    }

    self->mat_ids[index] = id;
    self->mat_ref_counts[index] =
        0; // Must initialize this or it could be garbage
    self->mat_count++;
  } else {
    // If the material was already loaded, go through the textures and grab
    // a reference so it's known that the texture is in use
    tb_tex_system_take_tex_ref(self->texture_system,
                               self->mat_color_maps[index]);
    tb_tex_system_take_tex_ref(self->texture_system,
                               self->mat_normal_maps[index]);
    tb_tex_system_take_tex_ref(self->texture_system,
                               self->mat_metal_rough_maps[index]);
  }

  self->mat_ref_counts[index]++;

  return id;
}

TbMaterialPerm tb_mat_system_get_perm(MaterialSystem *self, TbMaterialId mat) {
  const uint32_t index = find_mat_by_id(self, mat);
  if (index == SDL_MAX_UINT32) {
    TB_CHECK_RETURN(false, "Failed to find material to get permutation", 0);
  }
  return self->mat_perms[index];
}

VkDescriptorSet tb_mat_system_get_set(MaterialSystem *self, TbMaterialId mat) {
  const uint32_t index = find_mat_by_id(self, mat);
  if (index == SDL_MAX_UINT32) {
    TB_CHECK_RETURN(false, "Failed to find material to get set", 0);
  }
  return self->mat_sets[index];
}

void tb_mat_system_release_material_ref(MaterialSystem *self,
                                        TbMaterialId mat) {
  const uint32_t index = find_mat_by_id(self, mat);
  if (index == SDL_MAX_UINT32) {
    TB_CHECK(false, "Failed to find material to release");
    return;
  }

  if (self->mat_ref_counts[index] == 0) {
    TB_CHECK(false, "Tried to release reference to material with 0 ref count");
    return;
  }

  self->mat_ref_counts[index]--;

  // Materials should always release their texture references since materials
  // don't own their textures
  tb_tex_system_release_texture_ref(self->texture_system,
                                    self->mat_color_maps[index]);
  tb_tex_system_release_texture_ref(self->texture_system,
                                    self->mat_normal_maps[index]);
  tb_tex_system_release_texture_ref(self->texture_system,
                                    self->mat_metal_rough_maps[index]);

  if (self->mat_ref_counts[index] == 0) {
    // Free the mesh at this index
    VmaAllocator vma_alloc = self->render_system->vma_alloc;

    TbBuffer *gpu_buf = &self->mat_gpu_buffers[index];

    vmaDestroyBuffer(vma_alloc, gpu_buf->buffer, gpu_buf->alloc);

    *gpu_buf = (TbBuffer){0};
  }
}
