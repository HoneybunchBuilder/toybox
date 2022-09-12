#include "materialsystem.h"

#include "cgltf.h"
#include "common.hlsli"
#include "hash.h"
#include "rendersystem.h"
#include "world.h"

bool create_material_system(MaterialSystem *self,
                            const MaterialSystemDescriptor *desc,
                            uint32_t system_dep_count,
                            System *const *system_deps) {
  // Find the render system
  RenderSystem *render_system = (RenderSystem *)tb_find_system_dep_self_by_id(
      system_deps, system_dep_count, RenderSystemId);
  TB_CHECK_RETURN(render_system,
                  "Failed to find render system which materials depends on",
                  VK_ERROR_UNKNOWN);

  *self = (MaterialSystem){
      .render_system = render_system,
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
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
         NULL},
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

  vkDestroyDescriptorSetLayout(device, self->set_layout, vk_alloc);
  vkDestroySampler(device, self->sampler, vk_alloc);

  vkDestroyDescriptorPool(device, self->mat_set_pool, vk_alloc);

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
  desc->system_dep_count = 1;
  desc->system_deps[0] = RenderSystemId;
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
                                         const cgltf_material *material) {
  // Hash the materials's path and gltf name to get the id
  TbMaterialId id = sdbm(0, (const uint8_t *)path, SDL_strlen(path));
  id = sdbm(id, (const uint8_t *)material->name, SDL_strlen(material->name));

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
        VkResult err = VK_SUCCESS;

        if (self->mat_set_pool) {
          vkDestroyDescriptorPool(device, self->mat_set_pool, vk_alloc);
        }

        const uint32_t pool_size_count = 2;
        VkDescriptorPoolSize pool_sizes[pool_size_count] = {
            // One binding is a uniform buffer
            {
                .descriptorCount = new_max,
                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            },
            // 3 Bindings are sampled images
            {
                .descriptorCount = new_max * 3,
                .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            },
        };

        VkDescriptorPoolCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = new_max * 4, // *4 because we know we have 4 bindings
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
          for (uint32_t i = 0; i < self->mat_count; ++i) {
            uint32_t write_idx = i * 4;
            writes[write_idx + 0] = (VkWriteDescriptorSet){
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo =
                    &(VkDescriptorBufferInfo){
                        .buffer = self->mat_gpu_buffers[i].buffer,
                        .range = self->mat_gpu_buffers[i].info.size,
                    },
            };
            writes[write_idx + 1] = (VkWriteDescriptorSet){
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstBinding = 1,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                .pImageInfo =
                    &(VkDescriptorImageInfo){
                        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        .imageView = VK_NULL_HANDLE, // TODO
                    },
            };
            writes[write_idx + 2] = (VkWriteDescriptorSet){
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstBinding = 2,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                .pImageInfo =
                    &(VkDescriptorImageInfo){
                        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        .imageView = VK_NULL_HANDLE, // TODO
                    },
            };
            writes[write_idx + 3] = (VkWriteDescriptorSet){
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstBinding = 3,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                .pImageInfo =
                    &(VkDescriptorImageInfo){
                        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        .imageView = VK_NULL_HANDLE, // TODO
                    },
            };
          }
          vkUpdateDescriptorSets(device, write_count, writes, 0, NULL);
        }
      }
    }

    index = self->mat_count;

    // Load material
    {}
  }

  return id;
}

void tb_mat_system_release_material_ref(MaterialSystem *self,
                                        TbMaterialId mat) {
  (void)self;
  (void)mat;
}
