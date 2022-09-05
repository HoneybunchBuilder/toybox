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

  // Create Descriptor Set
  {
    VkDescriptorSetLayoutBinding bindings[2] = {
        {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
         NULL},
        {1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
         sampler},
    };

    VkDescriptorSetLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
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
                VK_SHADER_STAGE_ALL_GRAPHICS,
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
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};

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
  const ImGuiDrawBatch *imgui_batches = (ImGuiDrawBatch *)batches;

  for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
    const ImGuiDrawBatch *batch = &imgui_batches[batch_idx];

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

bool create_imgui_system(ImGuiSystem *self, const ImGuiSystemDescriptor *desc,
                         uint32_t system_dep_count,
                         System *const *system_deps) {
  TB_CHECK_RETURN(system_dep_count == 1,
                  "Different than expected number of system dependencies",
                  false);
  TB_CHECK_RETURN(desc, "Invalid descriptor", false);

  // Find the render system
  RenderSystem *render_system = NULL;
  for (uint32_t i = 0; i < system_dep_count; ++i) {
    if (system_deps[i]->id == RenderSystemId) {
      render_system = (RenderSystem *)system_deps[i]->self;
      break;
    }
  }
  TB_CHECK_RETURN(render_system,
                  "Failed to find render system which imgui depends on", false);

  *self = (ImGuiSystem){
      .render_system = render_system,
      .tmp_alloc = desc->tmp_alloc,
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
    TB_VK_CHECK_RET(err, "Failed to create imgui render pass", false);
  }

  // Create imgui pipeline
  err = create_imgui_pipeline2(
      render_system->render_thread->device, &render_system->vk_host_alloc_cb,
      render_system->pipeline_cache, self->pass, &self->sampler,
      &self->pipe_layout, &self->set_layout, &self->pipeline);
  TB_VK_CHECK_RET(err, "Failed to create imgui pipeline", false);

  // Register a pass with the render system
  tb_rnd_register_pass(render_system, self->pass, imgui_pass_record);

  return true;
}

void destroy_imgui_system(ImGuiSystem *self) {
  RenderSystem *render_system = self->render_system;

  tb_rnd_destroy_render_pass(render_system, self->pass);

  tb_rnd_destroy_sampler(render_system, self->sampler);
  tb_rnd_destroy_set_layout(render_system, self->set_layout);
  tb_rnd_destroy_pipe_layout(render_system, self->pipe_layout);
  tb_rnd_destroy_pipeline(render_system, self->pipeline);

  *self = (ImGuiSystem){0};
}

void tick_imgui_system(ImGuiSystem *self, const SystemInput *input,
                       SystemOutput *output, float delta_seconds) {
  (void)self;
  (void)output; // No output for this system
  TracyCZoneN(ctx, "ImGui System", true);
  TracyCZoneColor(ctx, TracyCategoryColorUI);

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

    for (uint32_t entity_idx = 0; entity_idx < imgui_entity_count;
         ++entity_idx) {
      const ImGuiComponent *imgui =
          &((const ImGuiComponent *)imgui_comp_store->components)[entity_idx];

      igSetCurrentContext(imgui->context);

      // Apply this frame's input
      for (uint32_t input_idx = 0; input_idx < input_entity_count;
           ++input_idx) {
        const InputComponent *input =
            &((const InputComponent *)input_comp_store->components)[input_idx];
        for (uint32_t event_idx = 0; event_idx < input->event_count;
             ++event_idx) {
          const SDL_Event *event = &input->events[event_idx];

          // TODO: Feed event to imgui
          (void)event;
        }
      }

      // Apply basic IO
      ImGuiIO *io = igGetIO();
      io->DeltaTime = delta_seconds; // Note that ImGui expects seconds
      // TODO: Fetch this from the renderer
      io->DisplaySize = (ImVec2){1600.0f, 900.0f};

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
          TbBuffer tmp_host_buffer = {0};
          if (tb_rnd_sys_alloc_tmp_host_buffer(self->render_system, imgui_size,
                                               &tmp_host_buffer) !=
              VK_SUCCESS) {
            TracyCZoneEnd(ctx);
            return;
          }

          // Copy imgui mesh to the gpu driver controlled host buffer
          {
            size_t idx_size =
                (size_t)draw_data->TotalIdxCount * sizeof(ImDrawIdx);

            // We know to use 8 for the alignment because the vertex
            // attribute layout starts with a float2
            const size_t alignment = 8;
            size_t align_padding = idx_size % alignment;

            uint8_t *offset_ptr =
                ((uint8_t *)tmp_host_buffer.ptr) + tmp_host_buffer.offset;

            uint8_t *idx_dst = offset_ptr;
            uint8_t *vtx_dst = idx_dst + idx_size + align_padding;

            // Organize all mesh data into a single cpu-side buffer
            for (int32_t i = 0; i < draw_data->CmdListsCount; ++i) {
              const ImDrawList *cmd_list = draw_data->CmdLists[i];

              size_t idx_byte_count =
                  (size_t)cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx);
              size_t vtx_byte_count =
                  (size_t)cmd_list->VtxBuffer.Size * sizeof(ImDrawVert);

              SDL_memcpy(idx_dst, cmd_list->IdxBuffer.Data, idx_byte_count);
              SDL_memcpy(vtx_dst, cmd_list->VtxBuffer.Data, vtx_byte_count);

              idx_dst += idx_byte_count;
              vtx_dst += vtx_byte_count;
            }
          }

          // Send the render system a draw instruction
        }
      }

      igNewFrame();
    }
  }

  TracyCZoneEnd(ctx);
}

TB_DEFINE_SYSTEM(imgui, ImGuiSystem, ImGuiSystemDescriptor)

void tb_imgui_system_descriptor(SystemDescriptor *desc,
                                const ImGuiSystemDescriptor *input_desc) {
  desc->name = "ImGui";
  desc->size = sizeof(ImGuiSystem);
  desc->id = ImGuiSystemId;
  desc->desc = (InternalDescriptor)input_desc;
  SDL_memset(desc->deps, 0,
             sizeof(SystemComponentDependencies) * MAX_DEPENDENCY_SET_COUT);
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
