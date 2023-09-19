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

#include <flecs.h>

ViewSystem create_view_system(Allocator std_alloc, Allocator tmp_alloc,
                              RenderSystem *rnd_sys, RenderTargetSystem *rt_sys,
                              TextureSystem *tex_sys) {
  ViewSystem sys = {
      .render_system = rnd_sys,
      .render_target_system = rt_sys,
      .texture_system = tex_sys,
      .tmp_alloc = tmp_alloc,
      .std_alloc = std_alloc,
  };

  TB_DYN_ARR_RESET(sys.views, sys.std_alloc, 1);

  VkResult err = VK_SUCCESS;

  // Create a filtered env sampler
  {
    VkSamplerCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .maxLod = 9.0f, // TODO: Fix hack
        .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
    };
    err = tb_rnd_create_sampler(rnd_sys, &create_info, "Filtered Env Sampler",
                                &sys.filtered_env_sampler);
    TB_VK_CHECK(err, "Failed to create filtered env sampler");
  }

  // Create a BRDF sampler
  {
    VkSamplerCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .maxLod = 1.0f,
        .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
    };
    err = tb_rnd_create_sampler(rnd_sys, &create_info, "BRDF Sampler",
                                &sys.brdf_sampler);
    TB_VK_CHECK(err, "Failed to create brdf sampler");
  }

  // Create view descriptor set layout
  {
    VkDescriptorSetLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 9,
        .pBindings =
            (VkDescriptorSetLayoutBinding[9]){
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
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                },
                {
                    .binding = 6,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                },
                {
                    .binding = 7,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .pImmutableSamplers = &sys.filtered_env_sampler,
                },
                {
                    .binding = 8,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .pImmutableSamplers = &sys.brdf_sampler,
                },
            },
    };
    err = tb_rnd_create_set_layout(
        rnd_sys, &create_info, "View Descriptor Set Layout", &sys.set_layout);
    TB_VK_CHECK(err, "Failed to create view descriptor set");
  }
  return sys;
}

void destroy_view_system(ViewSystem *self) {
  TB_DYN_ARR_DESTROY(self->views);

  tb_rnd_destroy_sampler(self->render_system, self->brdf_sampler);
  tb_rnd_destroy_sampler(self->render_system, self->filtered_env_sampler);
  tb_rnd_destroy_set_layout(self->render_system, self->set_layout);

  for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
    ViewSystemFrameState *state = &self->frame_states[i];
    tb_rnd_destroy_descriptor_pool(self->render_system, state->set_pool);
  }

  *self = (ViewSystem){0};
}

void view_update_tick(ecs_iter_t *it) {
  TracyCZoneNC(ctx, "View System Tick", TracyCategoryColorRendering, true);

  ViewSystem *sys = ecs_field(it, ViewSystem, 1);

  const uint32_t view_count = TB_DYN_ARR_SIZE(sys->views);

  if (view_count == 0) {
    TracyCZoneEnd(ctx);
    return;
  }

  VkResult err = VK_SUCCESS;

  RenderSystem *rnd_sys = sys->render_system;
  RenderTargetSystem *rt_sys = sys->render_target_system;

  VkBuffer tmp_gpu_buffer = tb_rnd_get_gpu_tmp_buffer(rnd_sys);

  ViewSystemFrameState *state = &sys->frame_states[rnd_sys->frame_idx];
  // Allocate all the descriptor sets for this frame
  {
    // Resize the pool
    if (state->set_count < view_count) {
      if (state->set_pool) {
        tb_rnd_destroy_descriptor_pool(rnd_sys, state->set_pool);
      }

      VkDescriptorPoolCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
          .maxSets = view_count * 16,
          .poolSizeCount = 2,
          .pPoolSizes =
              (VkDescriptorPoolSize[2]){
                  {
                      .descriptorCount = view_count * 8,
                      .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                  },
                  {
                      .descriptorCount = view_count * 8,
                      .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                  },
              },
          .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
      };
      err = tb_rnd_create_descriptor_pool(
          rnd_sys, &create_info, "View System Frame State Descriptor Pool",
          &state->set_pool);
      TB_VK_CHECK(err,
                  "Failed to create view system frame state descriptor pool");
      state->set_count = view_count;
      state->sets = tb_realloc_nm_tp(sys->std_alloc, state->sets,
                                     state->set_count, VkDescriptorSet);
    } else {
      vkResetDescriptorPool(rnd_sys->render_thread->device, state->set_pool, 0);
      state->set_count = view_count;
    }

    VkDescriptorSetLayout *layouts =
        tb_alloc_nm_tp(sys->tmp_alloc, state->set_count, VkDescriptorSetLayout);
    for (uint32_t i = 0; i < state->set_count; ++i) {
      layouts[i] = sys->set_layout;
    }

    VkDescriptorSetAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorSetCount = state->set_count,
        .descriptorPool = state->set_pool,
        .pSetLayouts = layouts,
    };
    err = vkAllocateDescriptorSets(rnd_sys->render_thread->device, &alloc_info,
                                   state->sets);
    TB_VK_CHECK(err, "Failed to re-allocate view descriptor sets");
  }

  // Just upload and write all views for now, they tend to be important anyway
  const uint32_t buf_count = 2;
  const uint32_t img_count = 5;
  const uint32_t write_count = buf_count + img_count;

  VkWriteDescriptorSet *writes = tb_alloc_nm_tp(
      sys->tmp_alloc, view_count * write_count, VkWriteDescriptorSet);
  VkDescriptorBufferInfo *buffer_info = tb_alloc_nm_tp(
      sys->tmp_alloc, view_count * buf_count, VkDescriptorBufferInfo);
  VkDescriptorImageInfo *image_info = tb_alloc_nm_tp(
      sys->tmp_alloc, view_count * img_count, VkDescriptorImageInfo);
  TbHostBuffer *buffers =
      tb_alloc_nm_tp(sys->tmp_alloc, view_count * buf_count, TbHostBuffer);
  TB_DYN_ARR_FOREACH(sys->views, view_idx) {
    const View *view = &TB_DYN_ARR_AT(sys->views, view_idx);
    const CommonViewData *view_data = &view->view_data;
    const CommonLightData *light_data = &view->light_data;
    TbHostBuffer *view_buffer = &buffers[view_idx + 0];
    TbHostBuffer *light_buffer = &buffers[view_idx + 1];

    // Write view data into the tmp buffer we know will wind up on the GPU
    err = tb_rnd_sys_alloc_tmp_host_buffer(rnd_sys, sizeof(CommonViewData),
                                           0x40, view_buffer);
    TB_VK_CHECK(err, "Failed to make tmp host buffer allocation for view");
    err = tb_rnd_sys_alloc_tmp_host_buffer(rnd_sys, sizeof(CommonLightData),
                                           0x40, light_buffer);
    TB_VK_CHECK(err, "Failed to make tmp host buffer allocation for view");

    // Copy view data to the allocated buffers
    SDL_memcpy(view_buffer->ptr, view_data, sizeof(CommonViewData));
    SDL_memcpy(light_buffer->ptr, light_data, sizeof(CommonLightData));

    uint32_t buffer_idx = view_idx * buf_count;
    uint32_t image_idx = view_idx * img_count;
    uint32_t write_idx = view_idx * write_count;

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
        .imageView = tb_render_target_get_view(rt_sys, rnd_sys->frame_idx,
                                               rt_sys->irradiance_map),
    };
    image_info[image_idx + 1] = (VkDescriptorImageInfo){
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .imageView = tb_render_target_get_view(rt_sys, rnd_sys->frame_idx,
                                               rt_sys->prefiltered_cube),
    };
    image_info[image_idx + 2] = (VkDescriptorImageInfo){
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .imageView = tb_tex_system_get_image_view(
            sys->texture_system, sys->texture_system->brdf_tex)};

    image_info[image_idx + 3] = (VkDescriptorImageInfo){
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .imageView = tb_render_target_get_view(rt_sys, rnd_sys->frame_idx,
                                               rt_sys->shadow_map),
    };

    image_info[image_idx + 4] = (VkDescriptorImageInfo){
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .imageView = tb_render_target_get_view(rt_sys, rnd_sys->frame_idx,
                                               rt_sys->ssao_buffer)};

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
        .dstBinding = 6,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        .pImageInfo = &image_info[image_idx + 4],
    };
  }
  tb_rnd_update_descriptors(rnd_sys, view_count * write_count, writes);

  TracyCZoneEnd(ctx);
}

void tb_register_view_sys(ecs_world_t *ecs, Allocator std_alloc,
                          Allocator tmp_alloc) {
  ECS_COMPONENT(ecs, RenderSystem);
  ECS_COMPONENT(ecs, RenderTargetSystem);
  ECS_COMPONENT(ecs, TextureSystem);
  ECS_COMPONENT(ecs, ViewSystem);

  RenderSystem *rnd_sys = ecs_singleton_get_mut(ecs, RenderSystem);
  RenderTargetSystem *rt_sys = ecs_singleton_get_mut(ecs, RenderTargetSystem);
  TextureSystem *tex_sys = ecs_singleton_get_mut(ecs, TextureSystem);

  ViewSystem sys =
      create_view_system(std_alloc, tmp_alloc, rnd_sys, rt_sys, tex_sys);
  // Sets a singleton based on the value at a pointer
  ecs_set_ptr(ecs, ecs_id(ViewSystem), ViewSystem, &sys);

  ECS_SYSTEM(ecs, view_update_tick, EcsOnUpdate, ViewSystem(ViewSystem));
}

void tb_unregister_view_sys(ecs_world_t *ecs) {
  ECS_COMPONENT(ecs, ViewSystem);
  ViewSystem *sys = ecs_singleton_get_mut(ecs, ViewSystem);
  destroy_view_system(sys);
  ecs_singleton_remove(ecs, ViewSystem);
}

TbViewId tb_view_system_create_view(ViewSystem *self) {
  TB_CHECK_RETURN(self, "Invalid self object", InvalidViewId);

  TbViewId id = TB_DYN_ARR_SIZE(self->views);
  {
    View v = {0};
    TB_DYN_ARR_APPEND(self->views, v);
  }
  View *view = &TB_DYN_ARR_AT(self->views, id);

  view->view_data = (CommonViewData){
      .view_pos = {0},
  };
  CommonViewData *view_data = &view->view_data;

  // Supply a really basic view projection matrix for default
  float4x4 view_mat = look_forward(TB_ORIGIN, TB_FORWARD, TB_UP);
  float4x4 proj_mat = perspective(PI_2, 16.0f / 9.0f, 0.001f, 1000.0f);
  view_data->vp = mulmf44(proj_mat, view_mat);
  view_data->inv_vp = inv_mf44(view_data->vp);
  view->frustum = frustum_from_view_proj(&view_data->vp);

  return id;
}

void tb_view_system_set_view_target(ViewSystem *self, TbViewId view,
                                    TbRenderTargetId target) {
  if (view >= TB_DYN_ARR_SIZE(self->views)) {
    TB_CHECK(false, "View Id out of range");
  }
  TB_DYN_ARR_AT(self->views, view).target = target;
}

void tb_view_system_set_view_data(ViewSystem *self, TbViewId view,
                                  const CommonViewData *data) {
  if (view >= TB_DYN_ARR_SIZE(self->views)) {
    TB_CHECK(false, "View Id out of range");
  }
  TB_DYN_ARR_AT(self->views, view).view_data = *data;
}

void tb_view_system_set_light_data(ViewSystem *self, TbViewId view,
                                   const CommonLightData *data) {
  if (view >= TB_DYN_ARR_SIZE(self->views)) {
    TB_CHECK(false, "View Id out of range");
  }
  TB_DYN_ARR_AT(self->views, view).light_data = *data;
}

void tb_view_system_set_view_frustum(ViewSystem *self, TbViewId view,
                                     const Frustum *frust) {
  if (view >= TB_DYN_ARR_SIZE(self->views)) {
    TB_CHECK(false, "View Id out of range");
  }
  TB_DYN_ARR_AT(self->views, view).frustum = *frust;
}

VkDescriptorSet tb_view_system_get_descriptor(ViewSystem *self, TbViewId view) {
  if (view >= TB_DYN_ARR_SIZE(self->views)) {
    TB_CHECK(false, "View Id out of range");
  }

  return self->frame_states[self->render_system->frame_idx].sets[view];
}

const View *tb_get_view(ViewSystem *self, TbViewId view) {
  if (view >= TB_DYN_ARR_SIZE(self->views)) {
    TB_CHECK_RETURN(false, "View Id out of range", NULL);
  }
  return &TB_DYN_ARR_AT(self->views, view);
}
