#include "tb_view_system.h"

#include "common.hlsli"
#include "tb_camera_component.h"
#include "tb_common.h"
#include "tb_profiling.h"
#include "tb_render_system.h"
#include "tb_render_target_system.h"
#include "tb_texture_system.h"
#include "tb_transform_component.h"
#include "tb_world.h"

#include <flecs.h>

ECS_COMPONENT_DECLARE(TbViewSystem);

void tb_register_view_sys(TbWorld *world);
void tb_unregister_view_sys(TbWorld *world);

TB_REGISTER_SYS(tb, view, TB_VIEW_SYS_PRIO)

TbViewSystem create_view_system(TbAllocator gp_alloc, TbAllocator tmp_alloc,
                                TbRenderSystem *rnd_sys,
                                TbRenderTargetSystem *rt_sys) {
  TbViewSystem sys = {
      .rnd_sys = rnd_sys,
      .rt_sys = rt_sys,
      .tmp_alloc = tmp_alloc,
      .gp_alloc = gp_alloc,
  };

  TB_DYN_ARR_RESET(sys.views, sys.gp_alloc, 1);

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
    tb_rnd_create_sampler(rnd_sys, &create_info, "Filtered Env Sampler",
                          &sys.filtered_env_sampler);
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
    tb_rnd_create_sampler(rnd_sys, &create_info, "BRDF Sampler",
                          &sys.brdf_sampler);
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
    tb_rnd_create_set_layout(rnd_sys, &create_info,
                             "TbView Descriptor Set Layout", &sys.set_layout);
  }

  {
    VkDescriptorSetLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
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
    tb_rnd_create_set_layout(rnd_sys, &create_info,
                             "View Descriptor Set Layout", &sys.set_layout2);
    tb_create_descriptor_buffer(rnd_sys, sys.set_layout2, "View Descriptors", 4,
                                &sys.desc_buffer);
  }

  return sys;
}

void destroy_view_system(TbViewSystem *self, TbRenderSystem *rnd_sys) {
  TB_DYN_ARR_DESTROY(self->views);

  tb_rnd_destroy_sampler(rnd_sys, self->brdf_sampler);
  tb_rnd_destroy_sampler(rnd_sys, self->filtered_env_sampler);
  tb_rnd_destroy_set_layout(rnd_sys, self->set_layout);

  tb_rnd_destroy_set_layout(rnd_sys, self->set_layout2);
  tb_destroy_descriptor_buffer(rnd_sys, &self->desc_buffer);

  for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
    TbViewSystemFrameState *state = &self->frame_states[i];
    tb_rnd_destroy_descriptor_pool(rnd_sys, state->set_pool);
  }

  *self = (TbViewSystem){0};
}

void view_update_tick(ecs_iter_t *it) {
  TracyCZoneNC(ctx, "TbView System Tick", TracyCategoryColorRendering, true);

  // The view system requires that the texture system's BRDF texture be ready
  tb_auto brdf_tex = tb_get_brdf_tex(it->world);
  if (!tb_is_texture_ready(it->world, brdf_tex)) {
    TracyCZoneEnd(ctx);
    return;
  }

  tb_auto sys = ecs_field(it, TbViewSystem, 1);

  const uint32_t view_count = TB_DYN_ARR_SIZE(sys->views);

  if (view_count == 0) {
    TracyCZoneEnd(ctx);
    return;
  }

  VkResult err = VK_SUCCESS;

  TbRenderSystem *rnd_sys = sys->rnd_sys;
  TbRenderTargetSystem *rt_sys = sys->rt_sys;

  VkBuffer tmp_gpu_buffer = tb_rnd_get_gpu_tmp_buffer(rnd_sys);

#if TB_USE_DESC_BUFFER == 1
  {
    // Reset the descriptor buffer so we can just re-use it from the start
    // Similar to restarting a descriptor pool
    tb_reset_descriptor_buffer(rnd_sys, &sys->desc_buffer);

    const uint32_t frame_idx = rnd_sys->frame_idx;

    TB_DYN_ARR_FOREACH(sys->views, view_idx) {
      const TbView *view = &TB_DYN_ARR_AT(sys->views, view_idx);
      const TbCommonViewData *view_data = &view->view_data;
      const TbCommonLightData *light_data = &view->light_data;

      tb_auto tmp_addr = tb_rnd_get_gpu_tmp_addr(rnd_sys);

      // Write view data into the tmp buffer we know will wind up on the GPU
      uint64_t view_offset = 0;
      tb_rnd_sys_copy_to_tmp_buffer(rnd_sys, sizeof(TbCommonViewData), 0x40,
                                    view_data, &view_offset);
      uint64_t light_offset = 0;
      tb_rnd_sys_copy_to_tmp_buffer(rnd_sys, sizeof(TbCommonLightData), 0x40,
                                    light_data, &light_offset);

      TB_DYN_ARR_OF(TbDescriptor) descriptors = {0};
      TB_DYN_ARR_RESET(descriptors, rnd_sys->tmp_alloc, 16);

      // Binding 0: Common View Data
      TB_DYN_ARR_APPEND(
          descriptors,
          ((TbDescriptor){
              .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
              .data = {
                  .pUniformBuffer = &(VkDescriptorAddressInfoEXT){
                      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT,
                      .address = tmp_addr + view_offset,
                      .range = sizeof(TbCommonViewData),
                  }}}));
      // Binding 1: Irradiance Map
      TB_DYN_ARR_APPEND(
          descriptors,
          ((TbDescriptor){
              .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
              .data = {
                  .pSampledImage = &(VkDescriptorImageInfo){
                      .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      .imageView = tb_render_target_get_view(
                          rt_sys, frame_idx, rt_sys->irradiance_map),
                  }}}));
      // Binding 2: Environment Map
      TB_DYN_ARR_APPEND(
          descriptors,
          ((TbDescriptor){
              .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
              .data = {
                  .pSampledImage = &(VkDescriptorImageInfo){
                      .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      .imageView = tb_render_target_get_view(
                          rt_sys, frame_idx, rt_sys->prefiltered_cube),
                  }}}));
      // Binding 3: BRDF
      TB_DYN_ARR_APPEND(
          descriptors,
          ((TbDescriptor){
              .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
              .data = {
                  .pSampledImage = &(VkDescriptorImageInfo){
                      .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      .imageView = tb_render_target_get_view(rt_sys, frame_idx,
                                                             brdf_tex),
                  }}}));
      // Binding 4: Cascaded Shadow Map
      TB_DYN_ARR_APPEND(
          descriptors,
          ((TbDescriptor){
              .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
              .data = {
                  .pSampledImage = &(VkDescriptorImageInfo){
                      .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      .imageView = tb_render_target_get_view(
                          rt_sys, frame_idx, rt_sys->shadow_map),
                  }}}));
      // Binding 5: Lighting Data
      TB_DYN_ARR_APPEND(
          descriptors,
          ((TbDescriptor){
              .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
              .data = {
                  .pUniformBuffer = &(VkDescriptorAddressInfoEXT){
                      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT,
                      .address = tmp_addr + light_offset,
                      .range = sizeof(TbCommonLightData),
                  }}}));

      // Write all descriptors to buffer
      TB_DYN_ARR_FOREACH(descriptors, i) {
        tb_auto descriptor = TB_DYN_ARR_AT(descriptors, i);
        tb_write_desc_to_buffer(rnd_sys, &sys->desc_buffer, i, descriptor.type,
                                &descriptor.data);
      }
    }
  }

#else
  TbViewSystemFrameState *state = &sys->frame_states[rnd_sys->frame_idx];
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
          rnd_sys, &create_info, "TbView System Frame State Descriptor Pool",
          &state->set_pool);
      TB_VK_CHECK(err,
                  "Failed to create view system frame state descriptor pool");
      state->set_count = view_count;
      state->sets = tb_realloc_nm_tp(sys->gp_alloc, state->sets,
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
  const uint32_t img_count = 4;
  const uint32_t write_count = buf_count + img_count;

  tb_auto writes = tb_alloc_nm_tp(
      sys->tmp_alloc, (uint64_t)view_count * write_count, VkWriteDescriptorSet);
  tb_auto buffer_info = tb_alloc_nm_tp(
      sys->tmp_alloc, (uint64_t)view_count * buf_count, VkDescriptorBufferInfo);
  tb_auto image_info = tb_alloc_nm_tp(
      sys->tmp_alloc, (uint64_t)view_count * img_count, VkDescriptorImageInfo);
  TB_DYN_ARR_FOREACH(sys->views, view_idx) {
    const TbView *view = &TB_DYN_ARR_AT(sys->views, view_idx);
    const TbCommonViewData *view_data = &view->view_data;
    const TbCommonLightData *light_data = &view->light_data;

    // Write view data into the tmp buffer we know will wind up on the GPU
    uint64_t view_offset = 0;
    err = tb_rnd_sys_copy_to_tmp_buffer(rnd_sys, sizeof(TbCommonViewData), 0x40,
                                        view_data, &view_offset);
    TB_VK_CHECK(err, "Failed to make tmp host buffer allocation for view");
    uint64_t light_offset = 0;
    err = tb_rnd_sys_copy_to_tmp_buffer(rnd_sys, sizeof(TbCommonLightData),
                                        0x40, light_data, &light_offset);
    TB_VK_CHECK(err, "Failed to make tmp host buffer allocation for view");

    uint32_t buffer_idx = view_idx * buf_count;
    uint32_t image_idx = view_idx * img_count;
    uint32_t write_idx = view_idx * write_count;

    // Get the descriptor we want to write to
    VkDescriptorSet view_set = state->sets[view_idx];

    buffer_info[buffer_idx + 0] = (VkDescriptorBufferInfo){
        .buffer = tmp_gpu_buffer,
        .offset = view_offset,
        .range = sizeof(TbCommonViewData),
    };
    buffer_info[buffer_idx + 1] = (VkDescriptorBufferInfo){
        .buffer = tmp_gpu_buffer,
        .offset = light_offset,
        .range = sizeof(TbCommonLightData),
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
        .imageView = tb_tex_sys_get_image_view2(it->world, brdf_tex)};

    image_info[image_idx + 3] = (VkDescriptorImageInfo){
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .imageView = tb_render_target_get_view(rt_sys, rnd_sys->frame_idx,
                                               rt_sys->shadow_map),
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
  }
  tb_rnd_update_descriptors(rnd_sys, view_count * write_count, writes);
#endif

  TracyCZoneEnd(ctx);
}

void tb_register_view_sys(TbWorld *world) {
  TracyCZoneN(ctx, "Register View Sys", true);
  ecs_world_t *ecs = world->ecs;

  ECS_COMPONENT_DEFINE(ecs, TbViewSystem);

  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  tb_auto rt_sys = ecs_singleton_get_mut(ecs, TbRenderTargetSystem);

  tb_auto sys =
      create_view_system(world->gp_alloc, world->tmp_alloc, rnd_sys, rt_sys);
  // Sets a singleton based on the value at a pointer
  ecs_set_ptr(ecs, ecs_id(TbViewSystem), TbViewSystem, &sys);

  ECS_SYSTEM(ecs, view_update_tick, EcsOnStore, TbViewSystem(TbViewSystem));
  TracyCZoneEnd(ctx);
}

void tb_unregister_view_sys(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;
  tb_auto sys = ecs_singleton_get_mut(ecs, TbViewSystem);
  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  destroy_view_system(sys, rnd_sys);
  ecs_singleton_remove(ecs, TbViewSystem);
}

TbViewId tb_view_system_create_view(TbViewSystem *self) {
  TB_CHECK_RETURN(self, "Invalid self object", TbInvalidViewId);

  TbViewId id = TB_DYN_ARR_SIZE(self->views);
  {
    TbView v = {0};
    TB_DYN_ARR_APPEND(self->views, v);
  }
  TbView *view = &TB_DYN_ARR_AT(self->views, id);

  view->view_data = (TbCommonViewData){
      .view_pos = {0},
  };
  TbCommonViewData *view_data = &view->view_data;

  // Supply a really basic view projection matrix for default
  float4x4 view_mat = tb_look_forward(TB_ORIGIN, TB_FORWARD, TB_UP);
  float4x4 proj_mat = tb_perspective(TB_PI_2, 16.0f / 9.0f, 0.001f, 1000.0f);
  view_data->vp = tb_mulf44f44(proj_mat, view_mat);
  view_data->inv_vp = tb_invf44(view_data->vp);
  view->frustum = tb_frustum_from_view_proj(&view_data->vp);

  return id;
}

void tb_view_system_set_view_target(TbViewSystem *self, TbViewId view,
                                    TbRenderTargetId target) {
  if (view >= TB_DYN_ARR_SIZE(self->views)) {
    TB_CHECK(false, "TbView Id out of range");
  }
  TB_DYN_ARR_AT(self->views, view).target = target;
}

void tb_view_system_set_view_data(TbViewSystem *self, TbViewId view,
                                  const TbCommonViewData *data) {
  if (view >= TB_DYN_ARR_SIZE(self->views)) {
    TB_CHECK(false, "TbView Id out of range");
  }
  TB_DYN_ARR_AT(self->views, view).view_data = *data;
}

void tb_view_system_set_light_data(TbViewSystem *self, TbViewId view,
                                   const TbCommonLightData *data) {
  if (view >= TB_DYN_ARR_SIZE(self->views)) {
    TB_CHECK(false, "TbView Id out of range");
  }
  TB_DYN_ARR_AT(self->views, view).light_data = *data;
}

void tb_view_system_set_view_frustum(TbViewSystem *self, TbViewId view,
                                     const TbFrustum *frust) {
  if (view >= TB_DYN_ARR_SIZE(self->views)) {
    TB_CHECK(false, "TbView Id out of range");
  }
  TB_DYN_ARR_AT(self->views, view).frustum = *frust;
}

VkDescriptorSet tb_view_system_get_descriptor(TbViewSystem *self,
                                              TbViewId view) {
  if (view >= TB_DYN_ARR_SIZE(self->views)) {
    return VK_NULL_HANDLE;
  }

  // Can happen if view system's descriptors weren't ready yet
  if (self->frame_states[self->rnd_sys->frame_idx].sets == NULL) {
    return VK_NULL_HANDLE;
  }

  return self->frame_states[self->rnd_sys->frame_idx].sets[view];
}

const TbView *tb_get_view(TbViewSystem *self, TbViewId view) {
  if (view >= TB_DYN_ARR_SIZE(self->views)) {
    TB_CHECK_RETURN(false, "TbView Id out of range", NULL);
  }
  return &TB_DYN_ARR_AT(self->views, view);
}

VkDescriptorSetLayout tb_view_sys_get_set_layout(ecs_world_t *ecs) {
  tb_auto ctx = ecs_singleton_get_mut(ecs, TbViewSystem);
#if TB_USE_DESC_BUFFER == 1
  return ctx->set_layout2;
#else
  return ctx->set_layout;
#endif
}

VkDescriptorBufferBindingInfoEXT tb_view_sys_get_table_addr(ecs_world_t *ecs,
                                                            TbViewId view) {
  tb_auto ctx = ecs_singleton_get_mut(ecs, TbViewSystem);
  tb_auto addr = ctx->desc_buffer.buffer.address;
  tb_auto set_size = ctx->desc_buffer.layout_size;
  // An address of 0 indicates an error
  if (addr > 0 && set_size > 0) {
    addr = addr + ((uint64_t)view * set_size);
  }
  VkDescriptorBufferBindingInfoEXT binding_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT,
      .address = addr,
      // HACK: Hardcoded same usage from tb_descriptor_buffer.c
      .usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT |
               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
               VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
               VK_BUFFER_USAGE_TRANSFER_DST_BIT,
  };
  return binding_info;
}
