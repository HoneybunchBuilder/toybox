#include "skysystem.h"

// Ignore some warnings for the generated headers
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-variable-declarations"
#endif
#include "sky_frag.h"
#include "sky_vert.h"
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include "cameracomponent.h"
#include "common.hlsli"
#include "profiling.h"
#include "rendersystem.h"
#include "shadercommon.h"
#include "skycomponent.h"
#include "skydome.h"
#include "tbcommon.h"
#include "transformcomponent.h"
#include "world.h"

typedef struct SkyDrawBatch {
  VkPipelineLayout layout;
  VkPipeline pipeline;

  VkViewport viewport;
  VkRect2D scissor;

  VkPushConstantRange const_range;
  SkyPushConstants consts;
  VkDescriptorSet sky_set;

  // No case where we want multiple sky draws in one batch
  // May want multiple batches for multiple layers
  VkBuffer geom_buffer;
  uint32_t index_count;
  uint64_t vertex_offset;
} SkyDrawBatch;

VkResult create_sky_pipeline2(VkDevice device,
                              const VkAllocationCallbacks *vk_alloc,
                              VkPipelineCache cache, VkRenderPass pass,
                              VkPipelineLayout *pipe_layout,
                              VkDescriptorSetLayout *set_layout,
                              VkPipeline *pipeline) {
  VkResult err = VK_SUCCESS;

  // Create Descriptor Set Layout
  {
    VkDescriptorSetLayoutBinding bindings[1] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
         NULL},
    };

    VkDescriptorBindingFlags binding_flags[] = {
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
    };

    VkDescriptorSetLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        .pNext =
            &(VkDescriptorSetLayoutBindingFlagsCreateInfo){
                .sType =
                    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
                .bindingCount = 1,
                .pBindingFlags = binding_flags,
            },
        .bindingCount = 1,
        .pBindings = bindings,

    };
    err =
        vkCreateDescriptorSetLayout(device, &create_info, vk_alloc, set_layout);
    TB_VK_CHECK_RET(err, "Failed to create sky descriptor set layout", err);
    SET_VK_NAME(device, *set_layout, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                "Sky DS Layout");
  }

  // Create Pipeline Layout
  {
    VkPipelineLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges =
            &(VkPushConstantRange){
                VK_SHADER_STAGE_ALL_GRAPHICS,
                0,
                sizeof(SkyPushConstants),
            },
    };
    err = vkCreatePipelineLayout(device, &create_info, vk_alloc, pipe_layout);
    TB_VK_CHECK_RET(err, "Failed to create sky pipeline layout", err);
    SET_VK_NAME(device, *pipe_layout, VK_OBJECT_TYPE_PIPELINE_LAYOUT,
                "Sky Pipeline Layout");
  }

  // Create Sky Pipeline
  {
    // Load Shaders
    VkShaderModule vert_mod = VK_NULL_HANDLE;
    VkShaderModule frag_mod = VK_NULL_HANDLE;
    {
      VkShaderModuleCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      };

      create_info.codeSize = sizeof(sky_vert);
      create_info.pCode = (const uint32_t *)sky_vert;
      err = vkCreateShaderModule(device, &create_info, vk_alloc, &vert_mod);
      TB_VK_CHECK_RET(err, "Failed to load sky vert shader module", err);

      create_info.codeSize = sizeof(sky_frag);
      create_info.pCode = (const uint32_t *)sky_frag;
      err = vkCreateShaderModule(device, &create_info, vk_alloc, &frag_mod);
      TB_VK_CHECK_RET(err, "Failed to load sky frag shader module", err);
    }

    const uint32_t stage_count = 2;
    VkPipelineShaderStageCreateInfo stages[stage_count] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vert_mod,
            .pName = "vert",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = frag_mod,
            .pName = "frag",
        },
    };

    VkVertexInputBindingDescription vert_bindings[1] = {
        {0, sizeof(float3), VK_VERTEX_INPUT_RATE_VERTEX},
    };
    VkVertexInputAttributeDescription vert_attrs[1] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
    };
    VkPipelineVertexInputStateCreateInfo vert_input_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = vert_bindings,
        .vertexAttributeDescriptionCount = 1,
        .pVertexAttributeDescriptions = vert_attrs,
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };

    VkViewport viewport = {0, 600.0f, 800.0f, -600.0f, 0, 1};
    VkRect2D scissor = {{0, 0}, {800, 600}};
    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor,
    };

    VkPipelineRasterizationStateCreateInfo raster_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo multisample_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    VkPipelineColorBlendAttachmentState attachment_state = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };

    VkPipelineColorBlendStateCreateInfo color_blend_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &attachment_state,
    };

    VkPipelineDepthStencilStateCreateInfo depth_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_EQUAL,
        .maxDepthBounds = 1.0f,
    };

    VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                   VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dyn_states,
    };

    VkGraphicsPipelineCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = stage_count,
        .pStages = stages,
        .pVertexInputState = &vert_input_state,
        .pInputAssemblyState = &input_assembly_state,
        .pViewportState = &viewport_state,
        .pRasterizationState = &raster_state,
        .pMultisampleState = &multisample_state,
        .pColorBlendState = &color_blend_state,
        .pDepthStencilState = &depth_state,
        .pDynamicState = &dynamic_state,
        .layout = *pipe_layout,
        .renderPass = pass,
    };
    err = vkCreateGraphicsPipelines(device, cache, 1, &create_info, vk_alloc,
                                    pipeline);
    TB_VK_CHECK_RET(err, "Failed to create sky pipeline", err);
    SET_VK_NAME(device, *pipeline, VK_OBJECT_TYPE_PIPELINE, "Sky Pipeline");

    // Can safely dispose of shader module objects
    vkDestroyShaderModule(device, vert_mod, vk_alloc);
    vkDestroyShaderModule(device, frag_mod, vk_alloc);
  }

  return err;
}

void sky_pass_record(VkCommandBuffer buffer, uint32_t batch_count,
                     const void *batches) {
  TracyCZoneN(ctx, "Sky Record", true);
  TracyCZoneColor(ctx, TracyCategoryColorRendering);

  const SkyDrawBatch *sky_batches = (SkyDrawBatch *)batches;

  for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
    const SkyDrawBatch *batch = &sky_batches[batch_idx];
    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, batch->pipeline);

    vkCmdSetViewport(buffer, 0, 1, &batch->viewport);
    vkCmdSetScissor(buffer, 0, 1, &batch->scissor);

    VkPushConstantRange range = batch->const_range;
    const SkyPushConstants *consts = &batch->consts;
    vkCmdPushConstants(buffer, batch->layout, range.stageFlags, range.offset,
                       range.size, consts);

    vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            batch->layout, 0, 1, &batch->sky_set, 0, NULL);

    vkCmdBindIndexBuffer(buffer, batch->geom_buffer, 0, VK_INDEX_TYPE_UINT16);
    vkCmdBindVertexBuffers(buffer, 0, 1, &batch->geom_buffer,
                           &batch->vertex_offset);
    vkCmdDrawIndexed(buffer, batch->index_count, 1, 0, 0, 0);
  }
  TracyCZoneEnd(ctx);
}

bool create_sky_system(SkySystem *self, const SkySystemDescriptor *desc,
                       uint32_t system_dep_count, System *const *system_deps) {
  // Find the render system
  RenderSystem *render_system = (RenderSystem *)tb_find_system_dep_self_by_id(
      system_deps, system_dep_count, RenderSystemId);
  TB_CHECK_RETURN(render_system,
                  "Failed to find render system which imgui depends on",
                  VK_ERROR_UNKNOWN);

  *self = (SkySystem){
      .render_system = render_system,
      .tmp_alloc = desc->tmp_alloc,
      .std_alloc = desc->std_alloc,
  };

  VkResult err = VK_SUCCESS;

  // Create sky render pass
  // Lazy and rendering directly to the swapchain for now
  {
    const uint32_t attachment_count = 2;
    VkAttachmentDescription attachments[attachment_count] = {
        {
            .format = render_system->render_thread->swapchain.format,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        },
        {
            .format = VK_FORMAT_D32_SFLOAT,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        },
    };
    const uint32_t color_ref_count = 1;
    VkAttachmentReference color_refs[color_ref_count] = {
        {
            0,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        },
    };
    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = color_ref_count,
        .pColorAttachments = color_refs,
        .pDepthStencilAttachment =
            &(VkAttachmentReference){
                1,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            },
    };
    VkSubpassDependency subpass_dep = {
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };
    VkRenderPassCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = attachment_count,
        .pAttachments = attachments,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .pDependencies = &subpass_dep,
    };
    err = tb_rnd_create_render_pass(render_system, &create_info, "Sky Pass",
                                    &self->pass);
    TB_VK_CHECK_RET(err, "Failed to create sky render pass", err);
  }

  // Create sky pipeline
  err = create_sky_pipeline2(
      render_system->render_thread->device, &render_system->vk_host_alloc_cb,
      render_system->pipeline_cache, self->pass, &self->pipe_layout,
      &self->set_layout, &self->pipeline);
  TB_VK_CHECK_RET(err, "Failed to create imgui pipeline", err);

  // Create framebuffers that associate the sky pass with the swapchain target
  for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
    const uint32_t attachment_count = 2;
    // TODO: Figure out a way to do this without referencing the render thread
    // directly
    VkImageView attachments[attachment_count] = {
        render_system->render_thread->frame_states[i].swapchain_image_view,
        render_system->render_thread->frame_states[i].depth_buffer_view,
    };

    VkFramebufferCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = self->pass,
        .attachmentCount = attachment_count,
        .pAttachments = attachments,
        .width = render_system->render_thread->swapchain.width,
        .height = render_system->render_thread->swapchain.height,
        .layers = 1,
    };
    err = vkCreateFramebuffer(render_system->render_thread->device,
                              &create_info, &render_system->vk_host_alloc_cb,
                              &self->framebuffers[i]);
    TB_VK_CHECK_RET(err, "Failed to create sky framebuffer", err);
    SET_VK_NAME(render_system->render_thread->device, self->framebuffers[i],
                VK_OBJECT_TYPE_FRAMEBUFFER, "Sky Pass Framebuffer");
  }

  // Register pass with the render system
  tb_rnd_register_pass(render_system, self->pass, self->framebuffers,
                       render_system->render_thread->swapchain.width,
                       render_system->render_thread->swapchain.height,
                       sky_pass_record);

  // Create sky box geometry
  {
    const uint64_t skydome_size = get_skydome_size();
    // Make space for the sky geometry on the GPU
    {
      VkBufferCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
          .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                   VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
          .size = skydome_size,
      };
      err = tb_rnd_sys_alloc_gpu_buffer(render_system, &create_info,
                                        "Sky Geom Buffer",
                                        &self->sky_geom_gpu_buffer);
      TB_VK_CHECK_RET(err, "Failed to alloc imgui atlas", err);
    }

    // Use the gpu tmp buffer to copy the geom buffer
    {
      TbHostBuffer host_buf = {0};
      err = tb_rnd_sys_alloc_tmp_host_buffer(render_system, skydome_size, 16,
                                             &host_buf);
      TB_VK_CHECK_RET(err, "Failed to alloc tmp space for the skydome geometry",
                      err);
      copy_skydome(host_buf.ptr); // Copy to the newly alloced host buffer

      {
        BufferCopy skydome_copy = {
            .src = host_buf.buffer,
            .dst = self->sky_geom_gpu_buffer.buffer,
            .region =
                {
                    .srcOffset = host_buf.offset,
                    .size = skydome_size,
                },
        };
        tb_rnd_upload_buffers(render_system, &skydome_copy, 1);
      }
    }
  }

  return true;
}

void destroy_sky_system(SkySystem *self) {
  RenderSystem *render_system = self->render_system;

  for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
    vkDestroyFramebuffer(render_system->render_thread->device,
                         self->framebuffers[i],
                         &render_system->vk_host_alloc_cb);
  }

  vmaDestroyBuffer(render_system->vma_alloc, self->sky_geom_gpu_buffer.buffer,
                   self->sky_geom_gpu_buffer.alloc);

  vkDestroyDescriptorPool(render_system->render_thread->device, self->sky_pool,
                          &render_system->vk_host_alloc_cb);

  tb_rnd_destroy_render_pass(render_system, self->pass);

  tb_rnd_destroy_set_layout(render_system, self->set_layout);
  tb_rnd_destroy_pipe_layout(render_system, self->pipe_layout);
  tb_rnd_destroy_pipeline(render_system, self->pipeline);

  *self = (SkySystem){0};
}

void tick_sky_system(SkySystem *self, const SystemInput *input,
                     SystemOutput *output, float delta_seconds) {
  (void)output;
  VkResult err = VK_SUCCESS;

  EntityId *entities = tb_get_column_entity_ids(input, 0);

  const PackedComponentStore *skys =
      tb_get_column_check_id(input, 0, 0, SkyComponentId);
  const uint32_t sky_count = tb_get_column_component_count(input, 0);
  const PackedComponentStore *cameras =
      tb_get_column_check_id(input, 1, 0, CameraComponentId);
  const PackedComponentStore *transforms =
      tb_get_column_check_id(input, 1, 1, TransformComponentId);
  const uint32_t camera_count = tb_get_column_component_count(input, 1);

  if (skys == NULL || cameras == NULL || transforms == NULL) {
    return;
  }

  // TODO: Make this less hacky
  const uint32_t width = self->render_system->render_thread->swapchain.width;
  const uint32_t height = self->render_system->render_thread->swapchain.height;

  if (camera_count > 0 && sky_count > 0) {
    const CameraComponent *camera_comps =
        (const CameraComponent *)cameras->components;
    const TransformComponent *transform_comps =
        (const TransformComponent *)transforms->components;
    const SkyComponent *sky_comps = (const SkyComponent *)skys->components;

    // Copy the sky component for output
    SkyComponent *out_skys =
        tb_alloc_nm_tp(self->tmp_alloc, sky_count, SkyComponent);
    SDL_memcpy(out_skys, sky_comps, sky_count * sizeof(SkyComponent));

    uint32_t batch_count = 0;
    SkyDrawBatch *batches =
        tb_alloc_nm_tp(self->render_system->render_thread
                           ->frame_states[self->render_system->frame_idx]
                           .tmp_alloc.alloc,
                       sky_count * camera_count, SkyDrawBatch);

    // Determine if we need to resize & reallocate descriptor sets
    if (sky_count > self->sky_set_max) {
      if (self->sky_pool) {
        vkDestroyDescriptorPool(self->render_system->render_thread->device,
                                self->sky_pool,
                                &self->render_system->vk_host_alloc_cb);
      }

      self->sky_set_max = sky_count * 2;

      VkDescriptorPoolCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
          .maxSets = self->sky_set_max,
          .poolSizeCount = 1,
          .pPoolSizes =
              &(VkDescriptorPoolSize){
                  .descriptorCount = self->sky_set_max,
                  .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
              },
          .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
      };
      err = vkCreateDescriptorPool(
          self->render_system->render_thread->device, &create_info,
          &self->render_system->vk_host_alloc_cb, &self->sky_pool);
      TB_VK_CHECK(err, "Failed to create sky descriptor pool");
      SET_VK_NAME(self->render_system->render_thread->device, self->sky_pool,
                  VK_OBJECT_TYPE_DESCRIPTOR_POOL, "Sky Set Pool");

      // Re-allocate descriptors
      self->sky_sets = tb_realloc_nm_tp(self->std_alloc, self->sky_sets,
                                        self->sky_set_max, VkDescriptorSet);

      VkDescriptorSetLayout *layouts =
          tb_alloc_nm_tp(self->tmp_alloc, sky_count, VkDescriptorSetLayout);
      for (uint32_t i = 0; i < sky_count; ++i) {
        layouts[i] = self->set_layout;
      }

      VkDescriptorSetAllocateInfo alloc_info = {
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
          .descriptorSetCount = sky_count,
          .descriptorPool = self->sky_pool,
          .pSetLayouts = layouts,
      };
      err = vkAllocateDescriptorSets(self->render_system->render_thread->device,
                                     &alloc_info, self->sky_sets);
      TB_VK_CHECK(err, "Failed to allocate sky descriptor sets");
    }

    // Submit a sky draw for each camera, for each sky
    for (uint32_t cam_idx = 0; cam_idx < camera_count; ++cam_idx) {
      const CameraComponent *camera = &camera_comps[cam_idx];
      const TransformComponent *transform = &transform_comps[cam_idx];

      // TODO: Instead of calculating the vp matrix here, a camera system could
      // do it earlier in the frame
      float4x4 vp = {.row0 = {0}};
      {
        float4x4 proj = {.row0 = {0}};
        perspective(&proj, camera->fov, camera->aspect_ratio, camera->near,
                    camera->far);

        float4x4 model = {.row0 = {0}};
        transform_to_matrix(&model, &transform->transform);
        float3 forward = f4tof3(model.row2);

        float4x4 view = {.row0 = {0}};
        look_forward(&view, (float3){0.0f, 0.0f, 0.0f}, forward,
                     (float3){0.0f, 1.0f, 0.0f});

        mulmf44(&proj, &view, &vp);
      }

      for (uint32_t sky_idx = 0; sky_idx < sky_count; ++sky_idx) {
        SkyComponent *sky = &out_skys[sky_idx];

        // Update the sky's descriptor set
        {
          VkDescriptorSet sky_set = self->sky_sets[batch_count];

          sky->time += delta_seconds;

          SkyData sky_data = {
              .time = sky->time,
              .cirrus = sky->cirrus,
              .cumulus = sky->cumulus,
              .sun_dir = sky->sun_dir,
          };

          TbHostBuffer host_buffer = {0};
          tb_rnd_sys_alloc_tmp_host_buffer(self->render_system, sizeof(SkyData),
                                           0x40, &host_buffer);
          SDL_memcpy(host_buffer.ptr, &sky_data, sizeof(SkyData));

          VkWriteDescriptorSet write = {
              .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
              .dstSet = sky_set,
              .dstBinding = 0,
              .dstArrayElement = 0,
              .descriptorCount = 1,
              .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
              .pBufferInfo =
                  &(VkDescriptorBufferInfo){
                      .buffer = tb_rnd_get_gpu_tmp_buffer(self->render_system),
                      .offset = host_buffer.offset,
                      .range = sizeof(SkyData),
                  },
          };
          vkUpdateDescriptorSets(self->render_system->render_thread->device, 1,
                                 &write, 0, NULL);
        }

        batches[batch_count] = (SkyDrawBatch){
            .layout = self->pipe_layout,
            .pipeline = self->pipeline,
            .viewport = {0, 0, width, height, 0, 1},
            .scissor = {{0, 0}, {width, height}},
            .const_range =
                (VkPushConstantRange){
                    .size = sizeof(SkyPushConstants),
                    .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
                },
            .consts =
                {
                    .vp = vp,
                },
            .sky_set = self->sky_sets[batch_count],
            .geom_buffer = self->sky_geom_gpu_buffer.buffer,
            .index_count = get_skydome_index_count(),
            .vertex_offset = get_skydome_vert_offset(),
        };
        batch_count++;
      }
    }

    tb_rnd_issue_draw_batch(self->render_system, self->pass, batch_count,
                            sizeof(SkyDrawBatch), batches);

    // Report output (we've updated the time on the sky component)
    output->set_count = 1;
    output->write_sets[0] = (SystemWriteSet){
        .id = SkyComponentId,
        .count = sky_count,
        .components = (uint8_t *)out_skys,
        .entities = entities,
    };
  }
}

TB_DEFINE_SYSTEM(sky, SkySystem, SkySystemDescriptor)

void tb_sky_system_descriptor(SystemDescriptor *desc,
                              const SkySystemDescriptor *sky_desc) {
  desc->name = "Sky";
  desc->size = sizeof(SkySystem);
  desc->id = SkySystemId;
  desc->desc = (InternalDescriptor)sky_desc;
  SDL_memset(desc->deps, 0,
             sizeof(SystemComponentDependencies) * MAX_DEPENDENCY_SET_COUNT);
  desc->dep_count = 2;
  desc->deps[0] = (SystemComponentDependencies){
      .count = 1,
      .dependent_ids = {SkyComponentId},
  };
  desc->deps[1] = (SystemComponentDependencies){
      .count = 2,
      .dependent_ids = {CameraComponentId, TransformComponentId},
  };
  desc->system_dep_count = 1;
  desc->system_deps[0] = RenderSystemId;
  desc->create = tb_create_sky_system;
  desc->destroy = tb_destroy_sky_system;
  desc->tick = tb_tick_sky_system;
}
