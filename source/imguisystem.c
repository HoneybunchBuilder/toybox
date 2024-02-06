#include "imguisystem.h"

// Ignore some warnings for the generated headers
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-variable-declarations"
#include "imgui_frag.h"
#include "imgui_vert.h"
#pragma clang diagnostic pop

#include "inputsystem.h"
#include "profiling.h"
#include "renderpipelinesystem.h"
#include "rendersystem.h"
#include "rendertargetsystem.h"
#include "shadercommon.h"
#include "tbcommon.h"
#include "tbimgui.h"
#include "tbvma.h"
#include "vkdbg.h"

#include <flecs.h>

typedef struct ImGuiDraw {
  VkBuffer geom_buffer;
  uint64_t index_offset;
  uint64_t vertex_offset;
  uint32_t index_count;
} ImGuiDraw;

typedef struct ImGuiDrawBatch {
  VkPushConstantRange const_range;
  TbImGuiPushConstants consts;
  VkDescriptorSet atlas_set;
} ImGuiDrawBatch;

VkResult
create_imgui_pipeline(VkDevice device, const VkAllocationCallbacks *vk_alloc,
                      VkPipelineCache cache, VkSampler sampler,
                      VkFormat ui_target_format, VkPipelineLayout *pipe_layout,
                      VkDescriptorSetLayout *set_layout, VkPipeline *pipeline) {
  VkResult err = VK_SUCCESS;

  // Create Descriptor Set Layout
  {
    VkDescriptorSetLayoutBinding bindings[2] = {
        {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
         NULL},
        {1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
         &sampler},
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
                sizeof(TbImGuiPushConstants),
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

#define STAGE_COUNT 2
    VkPipelineShaderStageCreateInfo stages[STAGE_COUNT] = {
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

    // We're using direct rendering instead of render passes
    // We don't supply a render pass so we have to supply rendering info
    VkPipelineRenderingCreateInfo rendering_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = (VkFormat[1]){ui_target_format},
    };

    VkGraphicsPipelineCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering_info,
        .stageCount = STAGE_COUNT,
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
    };
#undef STAGE_COUNT
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

void imgui_pass_record(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                       uint32_t batch_count, const TbDrawBatch *batches) {
  TracyCZoneNC(ctx, "ImGui Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "ImGui", 3, true);
  cmd_begin_label(buffer, "ImGui", (float4){0.8f, 0.0f, 0.8f, 1.0f});

  for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
    const TbDrawBatch *batch = &batches[batch_idx];
    const ImGuiDrawBatch *imgui_batch = (ImGuiDrawBatch *)batches->user_batch;
    if (batch->draw_count > 0) {
      vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        batch->pipeline);

      vkCmdSetViewport(buffer, 0, 1, &batch->viewport);
      vkCmdSetScissor(buffer, 0, 1, &batch->scissor);

      VkPushConstantRange range = imgui_batch->const_range;
      const TbImGuiPushConstants *consts = &imgui_batch->consts;
      vkCmdPushConstants(buffer, batch->layout, range.stageFlags, range.offset,
                         range.size, consts);

      vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              batch->layout, 0, 1, &imgui_batch->atlas_set, 0,
                              NULL);

      for (uint32_t draw_idx = 0; draw_idx < batch->draw_count; ++draw_idx) {
        const ImGuiDraw *draw = &((const ImGuiDraw *)batch->draws)[draw_idx];
        vkCmdBindIndexBuffer(buffer, draw->geom_buffer, draw->index_offset,
                             VK_INDEX_TYPE_UINT16);
        vkCmdBindVertexBuffers(buffer, 0, 1, &draw->geom_buffer,
                               &draw->vertex_offset);
        vkCmdDrawIndexed(buffer, draw->index_count, 1, 0, 0, 0);
      }
    }
  }

  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

VkResult ui_context_init(TbRenderSystem *rnd_sys, ImFontAtlas *atlas,
                         TbUIContext *context) {
  VkResult err = VK_SUCCESS;
  *context = (TbUIContext){
      .context = igCreateContext(atlas),
  };

  // Get atlas texture data for this context
  ImGuiIO *io = igGetIO();

  uint8_t *pixels = NULL;
  int32_t tex_w = 0;
  int32_t tex_h = 0;
  int32_t bytes_pp = 0;
  ImFontAtlas_GetTexDataAsRGBA32(io->Fonts, &pixels, &tex_w, &tex_h, &bytes_pp);

  // Place the atlas on the GPU via the tmp buffer
  {
    VkImageCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .arrayLayers = 1,
        .extent =
            (VkExtent3D){
                .width = tex_w,
                .height = tex_h,
                .depth = 1,
            },
        .format = VK_FORMAT_R8G8B8A8_SRGB,
        .imageType = VK_IMAGE_TYPE_2D,
        .mipLevels = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
    };

    const uint64_t atlas_size =
        (uint64_t)tex_w * (uint64_t)tex_h * (uint64_t)bytes_pp;
    err = tb_rnd_sys_create_gpu_image_tmp(rnd_sys, pixels, atlas_size, 16,
                                          &create_info, "ImGui Atlas",
                                          &context->atlas);
    TB_VK_CHECK_RET(err, "Failed to create imgui atlas", err);
  }

  // Create Image TbView for atlas
  {
    VkImageViewCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = context->atlas.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_SRGB,
        .components =
            {
                VK_COMPONENT_SWIZZLE_R,
                VK_COMPONENT_SWIZZLE_G,
                VK_COMPONENT_SWIZZLE_B,
                VK_COMPONENT_SWIZZLE_A,
            },
        .subresourceRange =
            {
                VK_IMAGE_ASPECT_COLOR_BIT,
                0,
                1,
                0,
                1,
            },
    };
    err = vkCreateImageView(rnd_sys->render_thread->device, &create_info,
                            &rnd_sys->vk_host_alloc_cb, &context->atlas_view);
    TB_VK_CHECK_RET(err, "Failed to create imgui atlas view", err);
    SET_VK_NAME(rnd_sys->render_thread->device, context->atlas_view,
                VK_OBJECT_TYPE_IMAGE_VIEW, "ImGui Atlas");
  }

  // Setup basic display size
  io->DisplaySize = (ImVec2){800.0f, 600.0f};
  io->DeltaTime = 0.1666667f;

  igSetCurrentContext(context->context);
  igNewFrame();

  return err;
}

void ui_context_destroy(TbRenderSystem *rnd_sys, TbUIContext *context) {
  tb_rnd_free_gpu_image(rnd_sys, &context->atlas);
  vkDestroyImageView(rnd_sys->render_thread->device, context->atlas_view,
                     &rnd_sys->vk_host_alloc_cb);
  igDestroyContext(context->context);
  *context = (TbUIContext){0};
}

TbImGuiSystem
create_imgui_system(TbAllocator gp_alloc, TbAllocator tmp_alloc,
                    uint32_t context_count, ImFontAtlas **context_atlases,
                    TbRenderSystem *rnd_sys, TbRenderPipelineSystem *rp_sys,
                    TbRenderTargetSystem *rt_sys, TbInputSystem *input_system) {
  TbImGuiSystem sys = {
      .rnd_sys = rnd_sys,
      .rp_sys = rp_sys,
      .rt_sys = rt_sys,
      .input = input_system,
      .tmp_alloc = tmp_alloc,
      .gp_alloc = gp_alloc,
      .context_count = context_count,
  };

  VkResult err = VK_SUCCESS;

  // Initialize each context
  for (uint32_t i = 0; i < sys.context_count; ++i) {
    err = ui_context_init(rnd_sys, context_atlases[i], &sys.contexts[i]);
    TB_VK_CHECK(err, "Failed to initialize UI context");
    if (err != VK_SUCCESS) {
      break;
    }
  }

  // Look up UI pipeline
  TbRenderPassId ui_pass_id = rp_sys->ui_pass;

  uint32_t attach_count = 0;
  tb_render_pipeline_get_attachments(rp_sys, ui_pass_id, &attach_count, NULL);
  TB_CHECK(attach_count == 1, "Unexpected");
  TbPassAttachment ui_info = {0};
  tb_render_pipeline_get_attachments(rp_sys, ui_pass_id, &attach_count,
                                     &ui_info);

  VkFormat ui_target_format =
      tb_render_target_get_format(rt_sys, ui_info.attachment);

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
    err = tb_rnd_create_sampler(rnd_sys, &create_info, "ImGui Sampler",
                                &sys.sampler);
    TB_VK_CHECK(err, "Failed to create imgui sampler");
  }

  // Create imgui pipeline
  err = create_imgui_pipeline(
      rnd_sys->render_thread->device, &rnd_sys->vk_host_alloc_cb,
      rnd_sys->pipeline_cache, sys.sampler, ui_target_format, &sys.pipe_layout,
      &sys.set_layout, &sys.pipeline);
  TB_VK_CHECK(err, "Failed to create imgui pipeline");

  sys.imgui_draw_ctx = tb_render_pipeline_register_draw_context(
      rp_sys, &(TbDrawContextDescriptor){
                  .batch_size = sizeof(ImGuiDrawBatch),
                  .draw_fn = imgui_pass_record,
                  .pass_id = ui_pass_id,
              });

  return sys;
}

void destroy_imgui_system(TbImGuiSystem *self) {
  TbRenderSystem *rnd_sys = self->rnd_sys;

  for (uint32_t i = 0; i < self->context_count; ++i) {
    ui_context_destroy(self->rnd_sys, &self->contexts[i]);
  }

  for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
    tb_rnd_destroy_descriptor_pool(rnd_sys, self->frame_states[i].set_pool);
  }

  tb_rnd_destroy_sampler(rnd_sys, self->sampler);
  tb_rnd_destroy_set_layout(rnd_sys, self->set_layout);
  tb_rnd_destroy_pipe_layout(rnd_sys, self->pipe_layout);
  tb_rnd_destroy_pipeline(rnd_sys, self->pipeline);

  *self = (TbImGuiSystem){0};
}

void imgui_draw_tick(ecs_iter_t *it) {
  TracyCZoneNC(ctx, "ImGui System", TracyCategoryColorUI, true);

  TbImGuiSystem *sys = ecs_field(it, TbImGuiSystem, 1);

  VkResult err = VK_SUCCESS;

  if (sys->context_count > 0 && it->delta_time > 0) {
    TbRenderSystem *rnd_sys = sys->rnd_sys;

    TbImGuiFrameState *state = &sys->frame_states[rnd_sys->frame_idx];
    // Allocate all the descriptor sets for this frame
    {
      // Resize the pool
      if (state->set_count < sys->context_count) {
        if (state->set_pool) {
          tb_rnd_destroy_descriptor_pool(rnd_sys, state->set_pool);
        }

        VkDescriptorPoolCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = sys->context_count * 8,
            .poolSizeCount = 1,
            .pPoolSizes =
                &(VkDescriptorPoolSize){
                    .descriptorCount = sys->context_count * 8,
                    .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                },
            .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        };
        err = tb_rnd_create_descriptor_pool(
            rnd_sys, &create_info, "ImGui System Frame State Descriptor Pool",
            &state->set_pool);
        TB_VK_CHECK(
            err, "Failed to create imgui system frame state descriptor pool");

        state->set_count = sys->context_count;
        state->sets = tb_realloc_nm_tp(sys->gp_alloc, state->sets,
                                       state->set_count, VkDescriptorSet);
      } else {
        vkResetDescriptorPool(sys->rnd_sys->render_thread->device,
                              state->set_pool, 0);
        state->set_count = sys->context_count;
      }

      VkDescriptorSetLayout *layouts = tb_alloc_nm_tp(
          sys->tmp_alloc, state->set_count, VkDescriptorSetLayout);
      for (uint32_t i = 0; i < state->set_count; ++i) {
        layouts[i] = sys->set_layout;
      }

      VkDescriptorSetAllocateInfo alloc_info = {
          .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
          .descriptorSetCount = state->set_count,
          .descriptorPool = state->set_pool,
          .pSetLayouts = layouts,
      };
      err = vkAllocateDescriptorSets(rnd_sys->render_thread->device,
                                     &alloc_info, state->sets);
      TB_VK_CHECK(err, "Failed to re-allocate view descriptor sets");
    }

    // Just upload and write all atlases for now, they tend to be important
    // anyway
    VkWriteDescriptorSet *writes = tb_alloc_nm_tp(
        sys->tmp_alloc, sys->context_count, VkWriteDescriptorSet);
    VkDescriptorImageInfo *image_info = tb_alloc_nm_tp(
        sys->tmp_alloc, sys->context_count, VkDescriptorImageInfo);
    for (uint32_t imgui_idx = 0; imgui_idx < sys->context_count; ++imgui_idx) {
      const TbUIContext *ui_ctx = &sys->contexts[imgui_idx];

      // Get the descriptor we want to write to
      VkDescriptorSet atlas_set = state->sets[imgui_idx];

      image_info[imgui_idx] = (VkDescriptorImageInfo){
          .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          .imageView = ui_ctx->atlas_view,
      };

      // Construct a write descriptor
      writes[imgui_idx] = (VkWriteDescriptorSet){
          .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          .dstSet = atlas_set,
          .dstBinding = 0,
          .dstArrayElement = 0,
          .descriptorCount = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
          .pImageInfo = &image_info[imgui_idx],
      };
    }
    tb_rnd_update_descriptors(sys->rnd_sys, sys->context_count, writes);

    // Allocate a max draw batch per entity
    uint32_t batch_count = 0;
    ImGuiDrawBatch *imgui_batches = tb_alloc_nm_tp(
        sys->rnd_sys->render_thread->frame_states[sys->rnd_sys->frame_idx]
            .tmp_alloc.alloc,
        sys->context_count, ImGuiDrawBatch);
    TbDrawBatch *batches = tb_alloc_nm_tp(
        sys->rnd_sys->render_thread->frame_states[sys->rnd_sys->frame_idx]
            .tmp_alloc.alloc,
        sys->context_count, TbDrawBatch);

    for (uint32_t imgui_idx = 0; imgui_idx < sys->context_count; ++imgui_idx) {
      const TbUIContext *ui_ctx = &sys->contexts[imgui_idx];

      igSetCurrentContext(ui_ctx->context);

      ImGuiIO *io = igGetIO();

      // Apply this frame's input
      for (uint32_t event_idx = 0; event_idx < sys->input->event_count;
           ++event_idx) {
        const SDL_Event *event = &sys->input->events[event_idx];

        // Feed event to imgui
        if (event->type == SDL_EVENT_MOUSE_MOTION) {
          io->MousePos = (ImVec2){
              (float)event->motion.x,
              (float)event->motion.y,
          };
        } else if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                   event->type == SDL_EVENT_MOUSE_BUTTON_UP) {
          if (event->button.button == SDL_BUTTON_LEFT) {
            io->MouseDown[0] =
                event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ? 1 : 0;
          } else if (event->button.button == SDL_BUTTON_RIGHT) {
            io->MouseDown[1] =
                event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ? 1 : 0;
          } else if (event->button.button == SDL_BUTTON_MIDDLE) {
            io->MouseDown[2] =
                event->type == SDL_EVENT_MOUSE_BUTTON_DOWN ? 1 : 0;
          }
        }
      }

      // Apply basic IO
      io->DeltaTime = it->delta_time; // Note that ImGui expects seconds
      io->DisplaySize = (ImVec2){
          (float)sys->rnd_sys->render_thread->swapchain.width,
          (float)sys->rnd_sys->render_thread->swapchain.height,
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
          uint64_t tmp_offset = 0;
          void *tmp_ptr = NULL;
          if (tb_rnd_sys_copy_to_tmp_buffer2(sys->rnd_sys, imgui_size, 0x40,
                                             &tmp_offset,
                                             &tmp_ptr) != VK_SUCCESS) {
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

            uint8_t *idx_dst = (uint8_t *)tmp_ptr;
            uint8_t *vtx_dst = idx_dst + vtx_offset;

            // Organize all mesh data into a single cpu-side buffer
            for (uint32_t i = 0; i < imgui_draw_count; ++i) {
              const ImDrawList *cmd_list = draw_data->CmdLists.Data[i];

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
            VkBuffer gpu_tmp_buffer = tb_rnd_get_gpu_tmp_buffer(sys->rnd_sys);

            const float width = draw_data->DisplaySize.x;
            const float height = draw_data->DisplaySize.y;

            const float scale_x = 2.0f / width;
            const float scale_y = 2.0f / height;

            ImGuiDraw *draws =
                tb_alloc_nm_tp(sys->rnd_sys->render_thread
                                   ->frame_states[sys->rnd_sys->frame_idx]
                                   .tmp_alloc.alloc,
                               imgui_draw_count, ImGuiDraw);
            {
              uint64_t cmd_index_offset = tmp_offset;
              uint64_t cmd_vertex_offset = tmp_offset;

              for (uint32_t draw_idx = 0; draw_idx < imgui_draw_count;
                   ++draw_idx) {
                const ImDrawList *cmd_list = draw_data->CmdLists.Data[draw_idx];
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

            batches[batch_count] = (TbDrawBatch){
                .layout = sys->pipe_layout,
                .pipeline = sys->pipeline,
                .viewport = {0, 0, width, height, 0, 1},
                .scissor = {{0, 0}, {(uint32_t)width, (uint32_t)height}},
                .user_batch = &imgui_batches[batch_count],
                .draw_count = imgui_draw_count,
                .draw_size = sizeof(ImGuiDrawBatch),
                .draws = draws,
            };
            imgui_batches[batch_count] = (ImGuiDrawBatch){
                .const_range =
                    (VkPushConstantRange){
                        .size = sizeof(TbImGuiPushConstants),
                        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                    },
                .consts =
                    {
                        .scale = {scale_x, scale_y},
                        .translation = {-1.0f, -1.0f},
                    },
                .atlas_set = state->sets[batch_count],
            };
            batch_count++;
          }
        }
      }

      // Issue draw batches
      tb_render_pipeline_issue_draw_batch(sys->rp_sys, sys->imgui_draw_ctx,
                                          batch_count, batches);

      igNewFrame();
    }
  }

  TracyCZoneEnd(ctx);
}

void tb_register_imgui_sys(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;
  ECS_COMPONENT(ecs, TbRenderSystem);
  ECS_COMPONENT(ecs, TbRenderPipelineSystem);
  ECS_COMPONENT(ecs, TbRenderTargetSystem);
  ECS_COMPONENT(ecs, TbInputSystem);
  ECS_COMPONENT(ecs, TbImGuiSystem);

  TbRenderSystem *rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  TbRenderPipelineSystem *rp_sys =
      ecs_singleton_get_mut(ecs, TbRenderPipelineSystem);
  TbRenderTargetSystem *rt_sys =
      ecs_singleton_get_mut(ecs, TbRenderTargetSystem);
  TbInputSystem *in_sys = ecs_singleton_get_mut(ecs, TbInputSystem);

  // HACK: This sucks. Do we even care about custom atlases anymore?
  TbImGuiSystem sys = create_imgui_system(world->gp_alloc, world->tmp_alloc, 1,
                                          (ImFontAtlas *[1]){NULL}, rnd_sys,
                                          rp_sys, rt_sys, in_sys);
  // Sets a singleton based on the value at a pointer
  ecs_set_ptr(ecs, ecs_id(TbImGuiSystem), TbImGuiSystem, &sys);

  ECS_SYSTEM(ecs, imgui_draw_tick, EcsOnUpdate, TbImGuiSystem(TbImGuiSystem));
}

void tb_unregister_imgui_sys(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;
  ECS_COMPONENT(ecs, TbImGuiSystem);
  TbImGuiSystem *sys = ecs_singleton_get_mut(ecs, TbImGuiSystem);
  destroy_imgui_system(sys);
  ecs_singleton_remove(ecs, TbImGuiSystem);
}
