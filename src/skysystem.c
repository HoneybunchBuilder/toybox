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

#include "common.hlsli"
#include "profiling.h"
#include "rendersystem.h"
#include "skycomponent.h"
#include "skydome.h"
#include "tbcommon.h"
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
        .cullMode = VK_CULL_MODE_FRONT_BIT,
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
  (void)buffer;
  const SkyDrawBatch *sky_batches = (SkyDrawBatch *)batches;

  for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
    const SkyDrawBatch *batch = &sky_batches[batch_idx];
    (void)batch;
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
    VkAttachmentDescription color_attachment = {
        .format = render_system->render_thread->swapchain.format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    VkAttachmentDescription attachments[1] = {
        color_attachment,
    };
    VkAttachmentReference color_attachment_ref = {
        0,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    VkAttachmentReference attachment_refs[1] = {
        color_attachment_ref,
    };
    VkSubpassDescription subpass = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = attachment_refs,
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
        .attachmentCount = 1,
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
    VkFramebufferCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = self->pass,
        .attachmentCount = 1,
        .pAttachments =
            &render_system->render_thread->frame_states[i].swapchain_image_view,
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
      err = tb_rnd_sys_alloc_tmp_host_buffer(render_system, skydome_size,
                                             &host_buf);
      TB_VK_CHECK_RET(err, "Failed to alloc tmp space for the skydome geometry",
                      err);
      copy_skydome(host_buf.ptr); // Copy to the newly alloced host buffer

      // We know that the tmp host buffer gets uploaded automatically
      // so issue a copy to the perm gpu geom buffer
      {
        VkBuffer tmp_gpu_buffer = tb_rnd_get_gpu_tmp_buffer(render_system);

        BufferCopy skydome_copy = {
            .src = tmp_gpu_buffer,
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

  tb_rnd_destroy_render_pass(render_system, self->pass);

  tb_rnd_destroy_set_layout(render_system, self->set_layout);
  tb_rnd_destroy_pipe_layout(render_system, self->pipe_layout);
  tb_rnd_destroy_pipeline(render_system, self->pipeline);

  *self = (SkySystem){0};
}

void tick_sky_system(SkySystem *self, const SystemInput *input,
                     SystemOutput *output, float delta_seconds) {}

TB_DEFINE_SYSTEM(sky, SkySystem, SkySystemDescriptor)

void tb_sky_system_descriptor(SystemDescriptor *desc,
                              const SkySystemDescriptor *sky_desc) {
  desc->name = "Sky";
  desc->size = sizeof(SkySystem);
  desc->id = SkySystemId;
  desc->desc = (InternalDescriptor)sky_desc;
  SDL_memset(desc->deps, 0,
             sizeof(SystemComponentDependencies) * MAX_DEPENDENCY_SET_COUNT);
  desc->dep_count = 1;
  desc->deps[0] = (SystemComponentDependencies){
      .count = 1,
      .dependent_ids = {SkyComponentId},
  };
  desc->system_dep_count = 1;
  desc->system_deps[0] = RenderSystemId;
  desc->create = tb_create_sky_system;
  desc->destroy = tb_destroy_sky_system;
  desc->tick = tb_tick_sky_system;
}
