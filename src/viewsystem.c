#include "viewsystem.h"

#include "cameracomponent.h"
#include "common.hlsli"
#include "profiling.h"
#include "rendersystem.h"
#include "rendertargetsystem.h"
#include "tbcommon.h"
#include "texturesystem.h"
#include "transformcomponent.h"
#include "world.h"

bool create_view_system(ViewSystem *self, const ViewSystemDescriptor *desc,
                        uint32_t system_dep_count, System *const *system_deps) {
  // Find the necessary systems
  RenderSystem *render_system =
      tb_get_system(system_deps, system_dep_count, RenderSystem);
  TB_CHECK_RETURN(render_system,
                  "Failed to find render system which view depends on", false);
  RenderTargetSystem *render_target_system =
      tb_get_system(system_deps, system_dep_count, RenderTargetSystem);
  TB_CHECK_RETURN(render_target_system,
                  "Failed to find render target system which view depends on",
                  false);
  TextureSystem *texture_system =
      tb_get_system(system_deps, system_dep_count, TextureSystem);
  TB_CHECK_RETURN(texture_system,
                  "Failed to find texture system which view depends on", false);

  *self = (ViewSystem){
      .render_system = render_system,
      .render_target_system = render_target_system,
      .texture_system = texture_system,
      .tmp_alloc = desc->tmp_alloc,
      .std_alloc = desc->std_alloc,
  };

  VkResult err = VK_SUCCESS;

  // Create Immutable Sampler
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
        .maxLod = 1.0f,
        .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
    };
    err = tb_rnd_create_sampler(render_system, &create_info,
                                "Irradiance Sampler", &self->sampler);
    TB_VK_CHECK_RET(err, "Failed to create irradiance sampler", err);
  }

  // Create view descriptor set layout
  {
    VkDescriptorSetLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 7,
        .pBindings =
            (VkDescriptorSetLayoutBinding[7]){
                {
                    .binding = 0,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                                  VK_SHADER_STAGE_FRAGMENT_BIT,
                },
                {
                    .binding = 1,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                },
                {
                    .binding = 2,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                },
                {
                    .binding = 3,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                },
                {
                    .binding = 4,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                                  VK_SHADER_STAGE_FRAGMENT_BIT,
                },
                {
                    .binding = 5,
                    .descriptorCount = TB_CASCADE_COUNT,
                    .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                },
                {
                    .binding = 6,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .pImmutableSamplers = &self->sampler,
                },
            },
    };
    err = tb_rnd_create_set_layout(render_system, &create_info,
                                   "View Descriptor Set Layout",
                                   &self->set_layout);
    TB_VK_CHECK_RET(err, "Failed to create view descriptor set", false);
  }

  return true;
}

void destroy_view_system(ViewSystem *self) {
  tb_rnd_destroy_set_layout(self->render_system, self->set_layout);
  tb_rnd_destroy_sampler(self->render_system, self->sampler);

  for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
    ViewSystemFrameState *state = &self->frame_states[i];
    tb_rnd_destroy_descriptor_pool(self->render_system, state->set_pool);
  }

  *self = (ViewSystem){0};
}

void tick_view_system(ViewSystem *self, const SystemInput *input,
                      SystemOutput *output, float delta_seconds) {
  // This system doesn't interact with the ECS so these parameters can be
  // ignored
  (void)input;
  (void)output;
  (void)delta_seconds;
  TracyCZoneNC(ctx, "View System Tick", TracyCategoryColorRendering, true);

  if (self->view_count == 0) {
    TracyCZoneEnd(ctx);
    return;
  }

  VkResult err = VK_SUCCESS;

  RenderSystem *render_system = self->render_system;

  VkBuffer tmp_gpu_buffer = tb_rnd_get_gpu_tmp_buffer(render_system);

  ViewSystemFrameState *state = &self->frame_states[render_system->frame_idx];
  // Allocate all the descriptor sets for this frame
  {
    // Resize the pool
    if (state->set_count < self->view_count) {
      if (state->set_pool) {
        tb_rnd_destroy_descriptor_pool(render_system, state->set_pool);
      }

      VkDescriptorPoolCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
          .maxSets = self->view_count * 4,
          .poolSizeCount = 2,
          .pPoolSizes =
              (VkDescriptorPoolSize[2]){
                  {
                      .descriptorCount = self->view_count * 2,
                      .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                  },
                  {
                      .descriptorCount = self->view_count * 2,
                      .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                  },
              },
          .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
      };
      err = tb_rnd_create_descriptor_pool(
          render_system, &create_info,
          "View System Frame State Descriptor Pool", &state->set_pool);
      TB_VK_CHECK(err,
                  "Failed to create view system frame state descriptor pool");
    } else {
      vkResetDescriptorPool(self->render_system->render_thread->device,
                            state->set_pool, 0);
    }
    state->set_count = self->view_count;
    state->sets = tb_realloc_nm_tp(self->std_alloc, state->sets,
                                   state->set_count, VkDescriptorSet);

    VkDescriptorSetLayout *layouts = tb_alloc_nm_tp(
        self->tmp_alloc, state->set_count, VkDescriptorSetLayout);
    for (uint32_t i = 0; i < state->set_count; ++i) {
      layouts[i] = self->set_layout;
    }

    VkDescriptorSetAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorSetCount = state->set_count,
        .descriptorPool = state->set_pool,
        .pSetLayouts = layouts,
    };
    err = vkAllocateDescriptorSets(render_system->render_thread->device,
                                   &alloc_info, state->sets);
    TB_VK_CHECK(err, "Failed to re-allocate view descriptor sets");
  }

  // Just upload and write all views for now, they tend to be important anyway
  VkWriteDescriptorSet *writes = tb_alloc_nm_tp(
      self->tmp_alloc, self->view_count * 9, VkWriteDescriptorSet);
  VkDescriptorBufferInfo *buffer_info = tb_alloc_nm_tp(
      self->tmp_alloc, self->view_count * 2, VkDescriptorBufferInfo);
  VkDescriptorImageInfo *image_info = tb_alloc_nm_tp(
      self->tmp_alloc, self->view_count * 7, VkDescriptorImageInfo);
  TbHostBuffer *buffers =
      tb_alloc_nm_tp(self->tmp_alloc, self->view_count * 2, TbHostBuffer);
  for (uint32_t view_idx = 0; view_idx < self->view_count; ++view_idx) {
    const View *view = &self->views[view_idx];
    const CommonViewData *view_data = &view->view_data;
    const CommonLightData *light_data = &view->light_data;
    TbHostBuffer *view_buffer = &buffers[view_idx + 0];
    TbHostBuffer *light_buffer = &buffers[view_idx + 1];

    // Write view data into the tmp buffer we know will wind up on the GPU
    err = tb_rnd_sys_alloc_tmp_host_buffer(
        render_system, sizeof(CommonViewData), 0x40, view_buffer);
    TB_VK_CHECK(err, "Failed to make tmp host buffer allocation for view");
    err = tb_rnd_sys_alloc_tmp_host_buffer(
        render_system, sizeof(CommonLightData), 0x40, light_buffer);
    TB_VK_CHECK(err, "Failed to make tmp host buffer allocation for view");

    // Copy view data to the allocated buffers
    SDL_memcpy(view_buffer->ptr, view_data, sizeof(CommonViewData));
    SDL_memcpy(light_buffer->ptr, light_data, sizeof(CommonLightData));

    uint32_t buffer_idx = view_idx * 2;
    uint32_t image_idx = view_idx * 7;
    uint32_t write_idx = view_idx * 9;

    // Get the descriptor we want to write to
    VkDescriptorSet view_set = state->sets[view_idx];

    buffer_info[buffer_idx + 0] = (VkDescriptorBufferInfo){
        .buffer = tmp_gpu_buffer,
        .offset = view_buffer->offset,
        .range = sizeof(CommonViewData),
    };
    buffer_info[buffer_idx + 1] = (VkDescriptorBufferInfo){
        .buffer = tmp_gpu_buffer,
        .offset = light_buffer->offset,
        .range = sizeof(CommonLightData),
    };

    image_info[image_idx + 0] = (VkDescriptorImageInfo){
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .imageView = tb_render_target_get_view(
            self->render_target_system, self->render_system->frame_idx,
            self->render_target_system->irradiance_map),
    };
    image_info[image_idx + 1] = (VkDescriptorImageInfo){
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .imageView = tb_render_target_get_view(
            self->render_target_system, self->render_system->frame_idx,
            self->render_target_system->prefiltered_cube),
    };
    image_info[image_idx + 2] = (VkDescriptorImageInfo){
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .imageView = tb_tex_system_get_image_view(
            self->texture_system, self->texture_system->brdf_tex)};
    image_info[image_idx + 3] = (VkDescriptorImageInfo){
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .imageView = tb_render_target_get_view(
            self->render_target_system, self->render_system->frame_idx,
            self->render_target_system->shadow_maps[0]),
    };
    image_info[image_idx + 4] = (VkDescriptorImageInfo){
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .imageView = tb_render_target_get_view(
            self->render_target_system, self->render_system->frame_idx,
            self->render_target_system->shadow_maps[1]),
    };
    image_info[image_idx + 5] = (VkDescriptorImageInfo){
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .imageView = tb_render_target_get_view(
            self->render_target_system, self->render_system->frame_idx,
            self->render_target_system->shadow_maps[2]),
    };
    image_info[image_idx + 6] = (VkDescriptorImageInfo){
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .imageView = tb_render_target_get_view(
            self->render_target_system, self->render_system->frame_idx,
            self->render_target_system->shadow_maps[3]),
    };

    // Construct a write descriptor

    writes[write_idx + 0] = (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = view_set,
        .dstBinding = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .pBufferInfo = &buffer_info[buffer_idx + 0],
    };
    writes[write_idx + 1] = (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = view_set,
        .dstBinding = 1,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        .pImageInfo = &image_info[image_idx + 0],
    };
    writes[write_idx + 2] = (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = view_set,
        .dstBinding = 2,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        .pImageInfo = &image_info[image_idx + 1],
    };
    writes[write_idx + 3] = (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = view_set,
        .dstBinding = 3,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        .pImageInfo = &image_info[image_idx + 2],
    };
    writes[write_idx + 4] = (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = view_set,
        .dstBinding = 4,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .pBufferInfo = &buffer_info[buffer_idx + 1],
    };
    writes[write_idx + 5] = (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = view_set,
        .dstBinding = 5,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        .pImageInfo = &image_info[image_idx + 3],
    };
    writes[write_idx + 6] = (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = view_set,
        .dstBinding = 5,
        .dstArrayElement = 1,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        .pImageInfo = &image_info[image_idx + 4],
    };
    writes[write_idx + 7] = (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = view_set,
        .dstBinding = 5,
        .dstArrayElement = 2,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        .pImageInfo = &image_info[image_idx + 5],
    };
    writes[write_idx + 8] = (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = view_set,
        .dstBinding = 5,
        .dstArrayElement = 3,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        .pImageInfo = &image_info[image_idx + 6],
    };
  }
  vkUpdateDescriptorSets(self->render_system->render_thread->device,
                         self->view_count * 9, writes, 0, NULL);

  TracyCZoneEnd(ctx);
}

TB_DEFINE_SYSTEM(view, ViewSystem, ViewSystemDescriptor)

void tb_view_system_descriptor(SystemDescriptor *desc,
                               const ViewSystemDescriptor *view_desc) {
  *desc = (SystemDescriptor){
      .name = "View",
      .size = sizeof(ViewSystem),
      .id = ViewSystemId,
      .desc = (InternalDescriptor)view_desc,
      .dep_count = 1,
      .deps[0] = {2, {CameraComponentId, TransformComponentId}},
      .system_dep_count = 3,
      .system_deps[0] = RenderSystemId,
      .system_deps[1] = RenderTargetSystemId,
      .system_deps[2] = TextureSystemId,
      .create = tb_create_view_system,
      .destroy = tb_destroy_view_system,
      .tick = tb_tick_view_system,
  };
}

TbViewId tb_view_system_create_view(ViewSystem *self) {
  TB_CHECK_RETURN(self, "Invalid self object", InvalidViewId);

  TbViewId id = self->view_count;
  uint32_t new_count = self->view_count + 1;
  if (new_count > self->view_max) {
    // Reallocate collection
    const uint32_t new_max = new_count * 2;
    self->views = tb_realloc_nm_tp(self->std_alloc, self->views, new_max, View);
    self->view_max = new_max;
  }
  self->view_count = new_count;

  View *view = &self->views[id];

  view->view_data = (CommonViewData){
      .view_pos = {0},
  };
  CommonViewData *view_data = &view->view_data;

  // Supply a really basic view projection matrix for default
  float4x4 view_mat = {.row0 = {0}};
  look_forward(&view_mat, (float3){0, 0, 0}, (float3){0, 0, 1},
               (float3){0, 1, 0});
  float4x4 proj_mat = {.row0 = {0}};
  reverse_perspective(&proj_mat, PI_2, 16.0f / 9.0f, 0.001f, 1000.0f);
  mulmf44(&proj_mat, &view_mat, &view_data->vp);

  view_data->inv_vp = inv_mf44(view_data->vp);
  view->frustum = frustum_from_view_proj(&view_data->vp);

  return id;
}

void tb_view_system_set_view_data(ViewSystem *self, TbViewId view,
                                  const CommonViewData *data) {
  if (view >= self->view_count) {
    TB_CHECK(false, "View Id out of range");
  }
  self->views[view].view_data = *data;
}

void tb_view_system_set_light_data(ViewSystem *self, TbViewId view,
                                   const CommonLightData *data) {
  if (view >= self->view_count) {
    TB_CHECK(false, "View Id out of range");
  }
  self->views[view].light_data = *data;
}

void tb_view_system_set_view_frustum(ViewSystem *self, TbViewId view,
                                     const Frustum *frust) {
  if (view >= self->view_count) {
    TB_CHECK(false, "View Id out of range");
  }
  self->views[view].frustum = *frust;
}

VkDescriptorSet tb_view_system_get_descriptor(ViewSystem *self, TbViewId view) {
  if (view >= self->view_count) {
    TB_CHECK(false, "View Id out of range");
  }

  return self->frame_states[self->render_system->frame_idx].sets[view];
}

const View *tb_get_view(ViewSystem *self, TbViewId view) {
  if (view >= self->view_count) {
    TB_CHECK_RETURN(false, "View Id out of range", NULL);
  }
  return &self->views[view];
}
