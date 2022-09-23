#include "imguisystem.h"

// Ignore some warnings for the generated headers
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-variable-declarations"
#endif
#include "imgui_frag.h"
#include "imgui_vert.h"
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include "imguicomponent.h"
#include "inputcomponent.h"
#include "profiling.h"
#include "rendersystem.h"
#include "shadercommon.h"
#include "tbcommon.h"
#include "tbimgui.h"
#include "tbvma.h"
#include "vkdbg.h"

typedef struct ImGuiDraw {
  VkBuffer geom_buffer;
  uint64_t index_offset;
  uint64_t vertex_offset;
  uint32_t index_count;
} ImGuiDraw;

typedef struct ImGuiDrawBatch {
  VkPipelineLayout layout;
  VkPipeline pipeline;

  VkViewport viewport;
  VkRect2D scissor;

  VkPushConstantRange const_range;
  ImGuiPushConstants consts;
  VkDescriptorSet atlas_set;

  uint32_t draw_count;
  ImGuiDraw *draws;
} ImGuiDrawBatch;

VkResult create_imgui_pipeline2(VkDevice device,
                                const VkAllocationCallbacks *vk_alloc,
                                VkPipelineCache cache, VkRenderPass pass,
                                VkSampler *sampler,
                                VkPipelineLayout *pipe_layout,
                                VkDescriptorSetLayout *set_layout,
                                VkPipeline *pipeline) {
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
    err = vkCreateSampler(device, &create_info, vk_alloc, sampler);
    TB_VK_CHECK_RET(err, "Failed to create imgui samplers", err);
    SET_VK_NAME(device, *sampler, VK_OBJECT_TYPE_SAMPLER, "ImGui Sampler");
  }

  // Create Descriptor Set Layout
  {
    VkDescriptorSetLayoutBinding bindings[2] = {
        {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
         NULL},
        {1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
         sampler},
    };

    VkDescriptorSetLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
        .bindingCount = 2,
        .pBindings = bindings,

    };
    err =
        vkCreateDescriptorSetLayout(device, &create_info, vk_alloc, set_layout);
    TB_VK_CHECK_RET(err, "Failed to create imgui descriptor set layout", err);
    SET_VK_NAME(device, *set_layout, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                "ImGui DS Layout");
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
                VK_SHADER_STAGE_VERTEX_BIT,
                0,
                sizeof(ImGuiPushConstants),
            },
    };
    err = vkCreatePipelineLayout(device, &create_info, vk_alloc, pipe_layout);
    TB_VK_CHECK_RET(err, "Failed to create imgui pipeline layout", err);
    SET_VK_NAME(device, *pipe_layout, VK_OBJECT_TYPE_PIPELINE_LAYOUT,
                "ImGui Pipeline Layout");
  }

  // Create ImGui Pipeline
  {
    // Load Shaders
    VkShaderModule vert_mod = VK_NULL_HANDLE;
    VkShaderModule frag_mod = VK_NULL_HANDLE;
    {
      VkShaderModuleCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      };

      create_info.codeSize = sizeof(imgui_vert);
      create_info.pCode = (const uint32_t *)imgui_vert;
      err = vkCreateShaderModule(device, &create_info, vk_alloc, &vert_mod);
      TB_VK_CHECK_RET(err, "Failed to load imgui vert shader module", err);

      create_info.codeSize = sizeof(imgui_frag);
      create_info.pCode = (const uint32_t *)imgui_frag;
      err = vkCreateShaderModule(device, &create_info, vk_alloc, &frag_mod);
      TB_VK_CHECK_RET(err, "Failed to load imgui frag shader module", err);
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
        {0, sizeof(float2) + sizeof(float2) + sizeof(uint32_t),
         VK_VERTEX_INPUT_RATE_VERTEX},
    };
    VkVertexInputAttributeDescription vert_attrs[3] = {
        {0, 0, VK_FORMAT_R32G32_SFLOAT, 0},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(float2)},
        {2, 0, VK_FORMAT_R8G8B8A8_UNORM, sizeof(float2) * 2},
    };
    VkPipelineVertexInputStateCreateInfo vert_input_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = vert_bindings,
        .vertexAttributeDescriptionCount = 3,
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
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };

    VkPipelineMultisampleStateCreateInfo multisample_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    VkPipelineColorBlendAttachmentState attachment_state = {
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
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
    TB_VK_CHECK_RET(err, "Failed to create imgui pipeline", err);
    SET_VK_NAME(device, *pipeline, VK_OBJECT_TYPE_PIPELINE, "ImGui Pipeline");

    // Can safely dispose of shader module objects
    vkDestroyShaderModule(device, vert_mod, vk_alloc);
    vkDestroyShaderModule(device, frag_mod, vk_alloc);
  }

  return err;
}

void imgui_pass_record(VkCommandBuffer buffer, uint32_t batch_count,
                       const void *batches) {
  TracyCZoneN(ctx, "ImGui Record", true);
  TracyCZoneColor(ctx, TracyCategoryColorRendering);
  const ImGuiDrawBatch *imgui_batches = (ImGuiDrawBatch *)batches;

  for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
    const ImGuiDrawBatch *batch = &imgui_batches[batch_idx];
    if (batch->draw_count > 0) {
      vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        batch->pipeline);

      vkCmdSetViewport(buffer, 0, 1, &batch->viewport);
      vkCmdSetScissor(buffer, 0, 1, &batch->scissor);

      VkPushConstantRange range = batch->const_range;
      const ImGuiPushConstants *consts = &batch->consts;
      vkCmdPushConstants(buffer, batch->layout, range.stageFlags, range.offset,
                         range.size, consts);

      vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              batch->layout, 0, 1, &batch->atlas_set, 0, NULL);

      for (uint32_t draw_idx = 0; draw_idx < batch->draw_count; ++draw_idx) {
        const ImGuiDraw *draw = &batch->draws[draw_idx];
        vkCmdBindIndexBuffer(buffer, draw->geom_buffer, draw->index_offset,
                             VK_INDEX_TYPE_UINT16);
        vkCmdBindVertexBuffers(buffer, 0, 1, &draw->geom_buffer,
                               &draw->vertex_offset);
        vkCmdDrawIndexed(buffer, draw->index_count, 1, 0, 0, 0);
      }
    }
  }
  TracyCZoneEnd(ctx);
}

bool create_imgui_system(ImGuiSystem *self, const ImGuiSystemDescriptor *desc,
                         uint32_t system_dep_count,
                         System *const *system_deps) {
  TB_CHECK_RETURN(system_dep_count == 1,
                  "Different than expected number of system dependencies",
                  VK_ERROR_UNKNOWN);
  TB_CHECK_RETURN(desc, "Invalid descriptor", VK_ERROR_UNKNOWN);

  // Find the render system
  RenderSystem *render_system = (RenderSystem *)tb_find_system_dep_self_by_id(
      system_deps, system_dep_count, RenderSystemId);
  TB_CHECK_RETURN(render_system,
                  "Failed to find render system which imgui depends on",
                  VK_ERROR_UNKNOWN);

  *self = (ImGuiSystem){
      .render_system = render_system,
      .tmp_alloc = desc->tmp_alloc,
      .std_alloc = desc->std_alloc,
  };

  VkResult err = VK_SUCCESS;

  // Create imgui render pass
  // Will render directly to the swapchain
  {
    VkAttachmentDescription color_attachment = {
        .format = render_system->render_thread->swapchain.format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
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
    err = tb_rnd_create_render_pass(render_system, &create_info, "ImGui Pass",
                                    &self->pass);
    TB_VK_CHECK_RET(err, "Failed to create imgui render pass", err);
  }

  // Create imgui pipeline
  err = create_imgui_pipeline2(
      render_system->render_thread->device, &render_system->vk_host_alloc_cb,
      render_system->pipeline_cache, self->pass, &self->sampler,
      &self->pipe_layout, &self->set_layout, &self->pipeline);
  TB_VK_CHECK_RET(err, "Failed to create imgui pipeline", err);

  // Create framebuffers that associate imgui pass with swapchain target
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
    err = tb_rnd_create_framebuffer(render_system, &create_info,
                                    "ImGui Pass Framebuffer",
                                    &self->framebuffers[i]);
  }

  // Register a pass with the render system
  tb_rnd_register_pass(render_system, self->pass, self->framebuffers,
                       render_system->render_thread->swapchain.width,
                       render_system->render_thread->swapchain.height,
                       imgui_pass_record);

  return true;
}

void destroy_imgui_system(ImGuiSystem *self) {
  RenderSystem *render_system = self->render_system;

  for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
    tb_rnd_destroy_framebuffer(render_system, self->framebuffers[i]);
  }

  tb_rnd_destroy_render_pass(render_system, self->pass);

  vkDestroyDescriptorPool(render_system->render_thread->device,
                          self->atlas_pool, &render_system->vk_host_alloc_cb);

  tb_rnd_destroy_sampler(render_system, self->sampler);
  tb_rnd_destroy_set_layout(render_system, self->set_layout);
  tb_rnd_destroy_pipe_layout(render_system, self->pipe_layout);
  tb_rnd_destroy_pipeline(render_system, self->pipeline);

  *self = (ImGuiSystem){0};
}

void tick_imgui_system(ImGuiSystem *self, const SystemInput *input,
                       SystemOutput *output, float delta_seconds) {
  (void)output; // No output for this system
  TracyCZoneN(ctx, "ImGui System", true);
  TracyCZoneColor(ctx, TracyCategoryColorUI);

  VkResult err = VK_SUCCESS;

  // Find expected components
  uint32_t imgui_entity_count = 0;
  const EntityId *imgui_entities = NULL;
  const PackedComponentStore *imgui_comp_store = NULL;

  uint32_t input_entity_count = 0;
  const PackedComponentStore *input_comp_store = NULL;
  TB_CHECK(input->dep_set_count == 2, "Unexpected number of dependency sets");
  for (uint32_t dep_set_idx = 0; dep_set_idx < input->dep_set_count;
       ++dep_set_idx) {
    const SystemDependencySet *dep_set = &input->dep_sets[dep_set_idx];

    for (uint32_t col_idx = 0; col_idx < dep_set->column_count; ++col_idx) {
      const PackedComponentStore *column = &dep_set->columns[col_idx];
      if (column->id == ImGuiComponentId) {
        imgui_comp_store = column;
        imgui_entities = dep_set->entity_ids;
        imgui_entity_count = dep_set->entity_count;
      }

      if (column->id == InputComponentId) {
        input_comp_store = column;
        input_entity_count = dep_set->entity_count;
      }
    }
  }

  if (imgui_entity_count > 0) {
    TB_CHECK(imgui_entities, "Invalid input entities");
    TB_CHECK(imgui_comp_store, "Failed to find imgui component store");

    // Allocate a max draw batch per entity
    uint32_t batch_count = 0;
    ImGuiDrawBatch *batches =
        tb_alloc_nm_tp(self->render_system->render_thread
                           ->frame_states[self->render_system->frame_idx]
                           .tmp_alloc.alloc,
                       imgui_entity_count, ImGuiDrawBatch);

    // Resize atlas descriptor pool if necessary
    if (imgui_entity_count > self->atlas_set_max) {
      if (self->atlas_pool) {
        vkDestroyDescriptorPool(self->render_system->render_thread->device,
                                self->atlas_pool,
                                &self->render_system->vk_host_alloc_cb);
      }

      self->atlas_set_max = imgui_entity_count * 2;

      VkDescriptorPoolCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
          .maxSets = self->atlas_set_max,
          .poolSizeCount = 1,
          .pPoolSizes =
              &(VkDescriptorPoolSize){
                  .descriptorCount = self->atlas_set_max,
                  .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
              },
          .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
      };
      err = vkCreateDescriptorPool(
          self->render_system->render_thread->device, &create_info,
          &self->render_system->vk_host_alloc_cb, &self->atlas_pool);
      TB_VK_CHECK(err, "Failed to create imgui atlas descriptor pool");
      SET_VK_NAME(self->render_system->render_thread->device, self->atlas_pool,
                  VK_OBJECT_TYPE_DESCRIPTOR_POOL, "ImGui Atlas Set Pool");

      // Re-allocate descriptors
      self->atlas_sets = tb_realloc_nm_tp(self->std_alloc, self->atlas_sets,
                                          self->atlas_set_max, VkDescriptorSet);

      VkDescriptorSetLayout *layouts = tb_alloc_nm_tp(
          self->tmp_alloc, imgui_entity_count, VkDescriptorSetLayout);
      for (uint32_t i = 0; i < imgui_entity_count; ++i) {
        layouts[i] = self->set_layout;
      }

      VkDescriptorSetAllocateInfo alloc_info = {
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
          .descriptorSetCount = imgui_entity_count,
          .descriptorPool = self->atlas_pool,
          .pSetLayouts = layouts,
      };
      err = vkAllocateDescriptorSets(self->render_system->render_thread->device,
                                     &alloc_info, self->atlas_sets);
      TB_VK_CHECK(err, "Failed to allocate imgui atlas descriptor sets");
    }

    for (uint32_t entity_idx = 0; entity_idx < imgui_entity_count;
         ++entity_idx) {
      const ImGuiComponent *imgui =
          &((const ImGuiComponent *)imgui_comp_store->components)[entity_idx];

      igSetCurrentContext(imgui->context);

      ImGuiIO *io = igGetIO();

      // Apply this frame's input
      for (uint32_t input_idx = 0; input_idx < input_entity_count;
           ++input_idx) {
        const InputComponent *input =
            &((const InputComponent *)input_comp_store->components)[input_idx];
        for (uint32_t event_idx = 0; event_idx < input->event_count;
             ++event_idx) {
          const SDL_Event *event = &input->events[event_idx];

          // Feed event to imgui
          if (event->type == SDL_MOUSEMOTION) {
            io->MousePos = (ImVec2){
                (float)event->motion.x,
                (float)event->motion.y,
            };
          } else if (event->type == SDL_MOUSEBUTTONDOWN ||
                     event->type == SDL_MOUSEBUTTONUP) {
            if (event->button.button == SDL_BUTTON_LEFT) {
              io->MouseDown[0] = event->type == SDL_MOUSEBUTTONDOWN ? 1 : 0;
            } else if (event->button.button == SDL_BUTTON_RIGHT) {
              io->MouseDown[1] = event->type == SDL_MOUSEBUTTONDOWN ? 1 : 0;
            } else if (event->button.button == SDL_BUTTON_MIDDLE) {
              io->MouseDown[2] = event->type == SDL_MOUSEBUTTONDOWN ? 1 : 0;
            }
          }
        }
      }

      // Apply basic IO
      io->DeltaTime = delta_seconds; // Note that ImGui expects seconds
      io->DisplaySize = (ImVec2){
          self->render_system->render_thread->swapchain.width,
          self->render_system->render_thread->swapchain.height,
      };

      igRender();

      ImDrawData *draw_data = igGetDrawData();
      TB_CHECK(draw_data, "Failed to retrieve draw data");

      // Send to render thread
      if (draw_data->Valid) {
        // Calculate how big the draw data is
        size_t imgui_size = 0;
        {
          const size_t idx_size =
              (size_t)draw_data->TotalIdxCount * sizeof(ImDrawIdx);
          const size_t vtx_size =
              (size_t)draw_data->TotalVtxCount * sizeof(ImDrawVert);
          // We know to use 8 for the alignment because the vertex
          // attribute layout starts with a float2
          const size_t alignment = 8;
          const size_t align_padding = idx_size % alignment;

          imgui_size = idx_size + align_padding + vtx_size;
        }

        if (imgui_size > 0) {
          // Make space for this on the next frame. For the host and the device
          // Note that we can rely on the tmp host buffer to be uploaded
          // to the gpu every frame
          TbHostBuffer tmp_host_buffer = {0};
          if (tb_rnd_sys_alloc_tmp_host_buffer(self->render_system, imgui_size,
                                               0x40, &tmp_host_buffer) !=
              VK_SUCCESS) {
            TracyCZoneEnd(ctx);
            return;
          }
          const uint32_t imgui_draw_count = draw_data->CmdListsCount;

          size_t vtx_offset = 0;

          // Copy imgui mesh to the gpu driver controlled host buffer
          {
            size_t idx_size =
                (size_t)draw_data->TotalIdxCount * sizeof(ImDrawIdx);

            size_t test_offset = 0;

            // We know to use 8 for the alignment because the vertex
            // attribute layout starts with a float2
            const size_t alignment = 8;
            size_t align_padding = idx_size % alignment;

            vtx_offset = idx_size + align_padding;

            uint8_t *idx_dst = (uint8_t *)tmp_host_buffer.ptr;
            uint8_t *vtx_dst = idx_dst + vtx_offset;

            // Organize all mesh data into a single cpu-side buffer
            for (uint32_t i = 0; i < imgui_draw_count; ++i) {
              const ImDrawList *cmd_list = draw_data->CmdLists[i];

              size_t idx_byte_count =
                  (size_t)cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx);
              size_t vtx_byte_count =
                  (size_t)cmd_list->VtxBuffer.Size * sizeof(ImDrawVert);

              SDL_memcpy(idx_dst, cmd_list->IdxBuffer.Data, idx_byte_count);
              SDL_memcpy(vtx_dst, cmd_list->VtxBuffer.Data, vtx_byte_count);

              test_offset += idx_byte_count;
              test_offset += vtx_byte_count;
              TB_CHECK(test_offset <= imgui_size, "Writing past buffer");

              idx_dst += idx_byte_count;
              vtx_dst += vtx_byte_count;
            }
          }

          // Send the render system a batch to draw
          {
            // Write atlas to the descriptor set we're using for this draw
            VkDescriptorSet atlas_set = self->atlas_sets[entity_idx];
            {
              VkWriteDescriptorSet write = {
                  .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                  .dstSet = atlas_set,
                  .dstBinding = 0,
                  .dstArrayElement = 0,
                  .descriptorCount = 1,
                  .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                  .pImageInfo =
                      &(VkDescriptorImageInfo){
                          .imageLayout =
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          .imageView = imgui->atlas_view,
                          // Sampler is immutable
                      },
              };
              vkUpdateDescriptorSets(self->render_system->render_thread->device,
                                     1, &write, 0, NULL);
            }

            VkBuffer gpu_tmp_buffer =
                tb_rnd_get_gpu_tmp_buffer(self->render_system);

            const float width = draw_data->DisplaySize.x;
            const float height = draw_data->DisplaySize.y;

            const float scale_x = 2.0f / width;
            const float scale_y = 2.0f / height;

            ImGuiDraw *draws = tb_alloc_nm_tp(
                self->render_system->render_thread
                    ->frame_states[self->render_system->frame_idx]
                    .tmp_alloc.alloc,
                imgui_draw_count, ImGuiDraw);
            {
              uint64_t cmd_index_offset = tmp_host_buffer.offset;
              uint64_t cmd_vertex_offset = tmp_host_buffer.offset;

              for (uint32_t draw_idx = 0; draw_idx < imgui_draw_count;
                   ++draw_idx) {
                const ImDrawList *cmd_list = draw_data->CmdLists[draw_idx];
                const uint32_t index_count = cmd_list->IdxBuffer.Size;

                draws[draw_idx] = (ImGuiDraw){
                    .geom_buffer = gpu_tmp_buffer,
                    .index_count = index_count,
                    .index_offset = cmd_index_offset,
                    .vertex_offset = vtx_offset + cmd_vertex_offset,
                };

                cmd_index_offset += index_count * sizeof(ImDrawIdx);
                cmd_vertex_offset +=
                    cmd_list->VtxBuffer.Size * sizeof(ImDrawVert);
              }
            }

            batches[batch_count++] = (ImGuiDrawBatch){
                .layout = self->pipe_layout,
                .pipeline = self->pipeline,
                .viewport = {0, 0, width, height, 0, 1},
                .scissor = {{0, 0}, {(uint32_t)width, (uint32_t)height}},
                .const_range =
                    (VkPushConstantRange){
                        .size = sizeof(ImGuiPushConstants),
                        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                    },
                .consts =
                    {
                        .scale =
                            {
                                scale_x,
                                scale_y,
                            },
                        .translation =
                            {
                                -1.0f - draw_data->DisplayPos.x * scale_x,
                                -1.0f - draw_data->DisplayPos.y * scale_y,
                            },
                    },
                .draw_count = imgui_draw_count,
                .draws = draws,
                .atlas_set = atlas_set,
            };
          }
        }
      }

      // Issue draw batches
      tb_rnd_issue_draw_batch(self->render_system, self->pass, batch_count,
                              sizeof(ImGuiDrawBatch), batches);

      igNewFrame();
    }
  }

  TracyCZoneEnd(ctx);
}

TB_DEFINE_SYSTEM(imgui, ImGuiSystem, ImGuiSystemDescriptor)

void tb_imgui_system_descriptor(SystemDescriptor *desc,
                                const ImGuiSystemDescriptor *imgui_desc) {
  desc->name = "ImGui";
  desc->size = sizeof(ImGuiSystem);
  desc->id = ImGuiSystemId;
  desc->desc = (InternalDescriptor)imgui_desc;
  SDL_memset(desc->deps, 0,
             sizeof(SystemComponentDependencies) * MAX_DEPENDENCY_SET_COUNT);
  desc->dep_count = 2;
  desc->deps[0] = (SystemComponentDependencies){
      .count = 1,
      .dependent_ids = {InputComponentId},
  };
  desc->deps[1] = (SystemComponentDependencies){
      .count = 1,
      .dependent_ids = {ImGuiComponentId},
  };
  desc->system_dep_count = 1;
  desc->system_deps[0] = RenderSystemId;
  desc->create = tb_create_imgui_system;
  desc->destroy = tb_destroy_imgui_system;
  desc->tick = tb_tick_imgui_system;
}
