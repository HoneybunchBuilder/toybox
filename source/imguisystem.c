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
  ImGuiPushConstants consts;
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
                       uint32_t batch_count, const DrawBatch *batches) {
  TracyCZoneNC(ctx, "ImGui Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "ImGui", 3, true);
  cmd_begin_label(buffer, "ImGui", (float4){0.8f, 0.0f, 0.8f, 1.0f});

  for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
    const DrawBatch *batch = &batches[batch_idx];
    const ImGuiDrawBatch *imgui_batch = (ImGuiDrawBatch *)batches->user_batch;
    if (batch->draw_count > 0) {
      vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        batch->pipeline);

      vkCmdSetViewport(buffer, 0, 1, &batch->viewport);
      vkCmdSetScissor(buffer, 0, 1, &batch->scissor);

      VkPushConstantRange range = imgui_batch->const_range;
      const ImGuiPushConstants *consts = &imgui_batch->consts;
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

VkResult ui_context_init(RenderSystem *render_system, ImFontAtlas *atlas,
                         UIContext *context) {
  VkResult err = VK_SUCCESS;
  *context = (UIContext){
      .context = igCreateContext(atlas),
  };

  // Get atlas texture data for this context
  ImGuiIO *io = igGetIO();

  uint8_t *pixels = NULL;
  int32_t tex_w = 0;
  int32_t tex_h = 0;
  int32_t bytes_pp = 0;
  ImFontAtlas_GetTexDataAsRGBA32(io->Fonts, &pixels, &tex_w, &tex_h, &bytes_pp);

  // Create the atlas image on the GPU
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

    err = tb_rnd_sys_alloc_gpu_image(render_system, &create_info, "ImGui Atlas",
                                     &context->atlas);
    TB_VK_CHECK_RET(err, "Failed to alloc imgui atlas", err);
  }

  // Get space for the image on the tmp buffer
  TbHostBuffer host_buf = {0};
  {
    const uint64_t atlas_size = tex_w * tex_h * bytes_pp;
    err = tb_rnd_sys_alloc_tmp_host_buffer(render_system, atlas_size, 16,
                                           &host_buf);
    TB_VK_CHECK_RET(err, "Failed to alloc imgui atlas in tmp host buffer", err);

    SDL_memcpy(host_buf.ptr, pixels, atlas_size);
  }

  // Copy the image from the tmp gpu buffer to the gpu image
  {
    // A bit jank, but upload the image directly from the gpu buffer that we
    // know will be copied to from the tmp host buffer before this copy
    // is completed.
    BufferImageCopy upload = {
        .src = host_buf.buffer,
        .dst = context->atlas.image,
        .region =
            {
                .bufferOffset = host_buf.offset,
                .imageSubresource =
                    {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .layerCount = 1,
                    },
                .imageExtent =
                    {
                        .width = tex_w,
                        .height = tex_h,
                        .depth = 1,
                    },
            },
        .range =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .layerCount = 1,
                .levelCount = 1,
            },

    };
    tb_rnd_upload_buffer_to_image(render_system, &upload, 1);
  }

  // Create Image View for atlas
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
    err = vkCreateImageView(render_system->render_thread->device, &create_info,
                            &render_system->vk_host_alloc_cb,
                            &context->atlas_view);
    TB_VK_CHECK_RET(err, "Failed to create imgui atlas view", err);
    SET_VK_NAME(render_system->render_thread->device, context->atlas_view,
                VK_OBJECT_TYPE_IMAGE_VIEW, "ImGui Atlas");
  }

  // Setup basic display size
  io->DisplaySize = (ImVec2){800.0f, 600.0f};
  io->DeltaTime = 0.1666667f;

  igSetCurrentContext(context->context);
  igNewFrame();

  return err;
}

void ui_context_destroy(RenderSystem *render_system, UIContext *context) {
  tb_rnd_free_gpu_image(render_system, &context->atlas);
  vkDestroyImageView(render_system->render_thread->device, context->atlas_view,
                     &render_system->vk_host_alloc_cb);
  igDestroyContext(context->context);
  *context = (UIContext){0};
}

ImGuiSystem create_imgui_system_internal(
    const ImGuiSystemDescriptor *desc, RenderSystem *render_system,
    RenderPipelineSystem *render_pipe_system,
    RenderTargetSystem *render_target_system, InputSystem *input_system) {

  ImGuiSystem sys = {
      .render_system = render_system,
      .render_pipe_system = render_pipe_system,
      .render_target_system = render_target_system,
      .input = input_system,
      .tmp_alloc = desc->tmp_alloc,
      .std_alloc = desc->std_alloc,
      .context_count = desc->context_count,
  };

  VkResult err = VK_SUCCESS;

  // Initialize each context
  for (uint32_t i = 0; i < sys.context_count; ++i) {
    err = ui_context_init(render_system, desc->context_atlases[i],
                          &sys.contexts[i]);
    TB_VK_CHECK(err, "Failed to initialize UI context");
  }

  // Look up UI pipeline
  TbRenderPassId ui_pass_id = render_pipe_system->ui_pass;

  uint32_t attach_count = 0;
  tb_render_pipeline_get_attachments(render_pipe_system, ui_pass_id,
                                     &attach_count, NULL);
  TB_CHECK(attach_count == 1, "Unexpected");
  PassAttachment ui_info = {0};
  tb_render_pipeline_get_attachments(render_pipe_system, ui_pass_id,
                                     &attach_count, &ui_info);

  VkFormat ui_target_format =
      tb_render_target_get_format(render_target_system, ui_info.attachment);

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
    err = tb_rnd_create_sampler(render_system, &create_info, "ImGui Sampler",
                                &sys.sampler);
    TB_VK_CHECK(err, "Failed to create imgui sampler");
  }

  // Create imgui pipeline
  err = create_imgui_pipeline(
      render_system->render_thread->device, &render_system->vk_host_alloc_cb,
      render_system->pipeline_cache, sys.sampler, ui_target_format,
      &sys.pipe_layout, &sys.set_layout, &sys.pipeline);
  TB_VK_CHECK(err, "Failed to create imgui pipeline");

  sys.imgui_draw_ctx = tb_render_pipeline_register_draw_context(
      render_pipe_system, &(DrawContextDescriptor){
                              .batch_size = sizeof(ImGuiDrawBatch),
                              .draw_fn = imgui_pass_record,
                              .pass_id = ui_pass_id,
                          });

  return sys;
}

bool create_imgui_system(ImGuiSystem *self, const ImGuiSystemDescriptor *desc,
                         uint32_t system_dep_count,
                         System *const *system_deps) {
  TB_CHECK_RETURN(system_dep_count == 4,
                  "Different than expected number of system dependencies",
                  false);
  TB_CHECK_RETURN(desc, "Invalid descriptor", false);

  // Find necessary systems
  RenderSystem *render_system =
      tb_get_system(system_deps, system_dep_count, RenderSystem);
  TB_CHECK_RETURN(render_system,
                  "Failed to find render system which imgui depends on", false);
  RenderPipelineSystem *render_pipe_system =
      tb_get_system(system_deps, system_dep_count, RenderPipelineSystem);
  TB_CHECK_RETURN(
      render_pipe_system,
      "Failed to find render pipeline system which imgui depends on", false);
  RenderTargetSystem *render_target_system =
      tb_get_system(system_deps, system_dep_count, RenderTargetSystem);
  TB_CHECK_RETURN(render_target_system,
                  "Failed to find render target system which imgui depends on",
                  false);
  InputSystem *input_system =
      tb_get_system(system_deps, system_dep_count, InputSystem);
  TB_CHECK_RETURN(input_system,
                  "Failed to find input system which imgui depends on", false);

  *self = create_imgui_system_internal(desc, render_system, render_pipe_system,
                                       render_target_system, input_system);

  return true;
}

void destroy_imgui_system(ImGuiSystem *self) {
  RenderSystem *render_system = self->render_system;

  for (uint32_t i = 0; i < self->context_count; ++i) {
    ui_context_destroy(self->render_system, &self->contexts[i]);
  }

  for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
    tb_rnd_destroy_descriptor_pool(render_system,
                                   self->frame_states[i].set_pool);
  }

  tb_rnd_destroy_sampler(render_system, self->sampler);
  tb_rnd_destroy_set_layout(render_system, self->set_layout);
  tb_rnd_destroy_pipe_layout(render_system, self->pipe_layout);
  tb_rnd_destroy_pipeline(render_system, self->pipeline);

  *self = (ImGuiSystem){0};
}

void tick_imgui_system_internal(ImGuiSystem *self, float delta_seconds) {
  TracyCZoneN(ctx, "ImGui System", true);
  TracyCZoneColor(ctx, TracyCategoryColorUI);

  VkResult err = VK_SUCCESS;

  if (self->context_count > 0 && delta_seconds > 0) {
    RenderSystem *render_system = self->render_system;

    ImGuiFrameState *state = &self->frame_states[render_system->frame_idx];
    // Allocate all the descriptor sets for this frame
    {
      // Resize the pool
      if (state->set_count < self->context_count) {
        if (state->set_pool) {
          tb_rnd_destroy_descriptor_pool(render_system, state->set_pool);
        }

        VkDescriptorPoolCreateInfo create_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = self->context_count * 8,
            .poolSizeCount = 1,
            .pPoolSizes =
                &(VkDescriptorPoolSize){
                    .descriptorCount = self->context_count * 8,
                    .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                },
            .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        };
        err = tb_rnd_create_descriptor_pool(
            render_system, &create_info,
            "ImGui System Frame State Descriptor Pool", &state->set_pool);
        TB_VK_CHECK(
            err, "Failed to create imgui system frame state descriptor pool");

        state->set_count = self->context_count;
        state->sets = tb_realloc_nm_tp(self->std_alloc, state->sets,
                                       state->set_count, VkDescriptorSet);
      } else {
        vkResetDescriptorPool(self->render_system->render_thread->device,
                              state->set_pool, 0);
        state->set_count = self->context_count;
      }

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

    // Just upload and write all atlases for now, they tend to be important
    // anyway
    VkWriteDescriptorSet *writes = tb_alloc_nm_tp(
        self->tmp_alloc, self->context_count, VkWriteDescriptorSet);
    VkDescriptorImageInfo *image_info = tb_alloc_nm_tp(
        self->tmp_alloc, self->context_count, VkDescriptorImageInfo);
    for (uint32_t imgui_idx = 0; imgui_idx < self->context_count; ++imgui_idx) {
      const UIContext *ui_ctx = &self->contexts[imgui_idx];

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
    tb_rnd_update_descriptors(self->render_system, self->context_count, writes);

    // Allocate a max draw batch per entity
    uint32_t batch_count = 0;
    ImGuiDrawBatch *imgui_batches =
        tb_alloc_nm_tp(self->render_system->render_thread
                           ->frame_states[self->render_system->frame_idx]
                           .tmp_alloc.alloc,
                       self->context_count, ImGuiDrawBatch);
    DrawBatch *batches =
        tb_alloc_nm_tp(self->render_system->render_thread
                           ->frame_states[self->render_system->frame_idx]
                           .tmp_alloc.alloc,
                       self->context_count, DrawBatch);

    for (uint32_t imgui_idx = 0; imgui_idx < self->context_count; ++imgui_idx) {
      const UIContext *ui_ctx = &self->contexts[imgui_idx];

      igSetCurrentContext(ui_ctx->context);

      ImGuiIO *io = igGetIO();

      // Apply this frame's input
      for (uint32_t event_idx = 0; event_idx < self->input->event_count;
           ++event_idx) {
        const SDL_Event *event = &self->input->events[event_idx];

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

            batches[batch_count] = (DrawBatch){
                .layout = self->pipe_layout,
                .pipeline = self->pipeline,
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
                        .size = sizeof(ImGuiPushConstants),
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
      tb_render_pipeline_issue_draw_batch(
          self->render_pipe_system, self->imgui_draw_ctx, batch_count, batches);

      igNewFrame();
    }
  }

  TracyCZoneEnd(ctx);
}

TB_DEFINE_SYSTEM(imgui, ImGuiSystem, ImGuiSystemDescriptor)

void tick_imgui_system(void *self, const SystemInput *input,
                       SystemOutput *output, float delta_seconds) {
  (void)input;
  (void)output;
  SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Tick ImGUI System");
  tick_imgui_system_internal((ImGuiSystem *)self, delta_seconds);
}

void tb_imgui_system_descriptor(SystemDescriptor *desc,
                                const ImGuiSystemDescriptor *imgui_desc) {
  *desc = (SystemDescriptor){
      .name = "ImGui",
      .size = sizeof(ImGuiSystem),
      .id = ImGuiSystemId,
      .desc = (InternalDescriptor)imgui_desc,
      .system_dep_count = 4,
      .system_deps = {RenderSystemId, RenderPipelineSystemId,
                      RenderTargetSystemId, InputSystemId},
      .create = tb_create_imgui_system,
      .destroy = tb_destroy_imgui_system,
      .tick_fn_count = 1,
      .tick_fns[0] =
          {
              .system_id = ImGuiSystemId,
              .order = E_TICK_UI,
              .function = tick_imgui_system,
          },
  };
}

void flecs_imgui_tick(ecs_iter_t *it) {
  ImGuiSystem *sys = ecs_field(it, ImGuiSystem, 1);
  tick_imgui_system_internal(sys, it->delta_time);
}

void tb_register_imgui_sys(ecs_world_t *ecs, Allocator std_alloc,
                           Allocator tmp_alloc) {
  ECS_COMPONENT(ecs, RenderSystem);
  ECS_COMPONENT(ecs, RenderPipelineSystem);
  ECS_COMPONENT(ecs, RenderTargetSystem);
  ECS_COMPONENT(ecs, InputSystem);
  ECS_COMPONENT(ecs, ImGuiSystem);

  RenderSystem *rnd_sys = ecs_singleton_get_mut(ecs, RenderSystem);
  RenderPipelineSystem *rp_sys =
      ecs_singleton_get_mut(ecs, RenderPipelineSystem);
  RenderTargetSystem *rt_sys = ecs_singleton_get_mut(ecs, RenderTargetSystem);
  InputSystem *in_sys = ecs_singleton_get_mut(ecs, InputSystem);

  // HACK: This sucks. Do we even care about custom atlases anymore?
  ImGuiSystem sys = create_imgui_system_internal(
      &(ImGuiSystemDescriptor){
          .std_alloc = std_alloc,
          .tmp_alloc = tmp_alloc,
          .context_count = 1,
          .context_atlases[0] = NULL,
      },
      rnd_sys, rp_sys, rt_sys, in_sys);
  // Sets a singleton based on the value at a pointer
  ecs_set_ptr(ecs, ecs_id(ImGuiSystem), ImGuiSystem, &sys);

  ECS_SYSTEM(ecs, flecs_imgui_tick, EcsOnUpdate, ImGuiSystem(ImGuiSystem));
}

void tb_unregister_imgui_sys(ecs_world_t *ecs) {
  ECS_COMPONENT(ecs, ImGuiSystem);
  ImGuiSystem *sys = ecs_singleton_get_mut(ecs, ImGuiSystem);
  destroy_imgui_system(sys);
}
