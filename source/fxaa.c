#include "fxaa.h"

#include "profiling.h"
#include "renderpipelinesystem.h"
#include "rendersystem.h"
#include "rendertargetsystem.h"
#include "tbcommon.h"
#include "world.h"

#include <flecs.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-variable-declarations"
#include "fxaa_frag.h"
#include "fxaa_vert.h"
#pragma clang diagnostic pop

typedef struct FXAABatch {
  VkDescriptorSet set;
  TbFXAAPushConstants consts;
} FXAABatch;

void record_fxaa(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                 uint32_t batch_count, const TbDrawBatch *batches) {
  // Only expecting one draw per pass
  if (batch_count != 1) {
    return;
  }
  TracyCZoneNC(ctx, "FXAA Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "FXAA", 3, true);
  cmd_begin_label(buffer, "FXAA", (float4){0.4f, 0.0f, 0.8f, 1.0f});

  tb_auto batch = &batches[0];
  tb_auto fxaa_batch = (const FXAABatch *)batches->user_batch;

  // Just drawing a fullscreen triangle
  vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, batch->pipeline);
  vkCmdSetViewport(buffer, 0, 1, &batch->viewport);
  vkCmdSetScissor(buffer, 0, 1, &batch->scissor);
  vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          batch->layout, 0, 1, &fxaa_batch->set, 0, NULL);
  vkCmdPushConstants(buffer, batch->layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                     sizeof(TbFXAAPushConstants), &fxaa_batch->consts);
  vkCmdDraw(buffer, 3, 1, 0, 0);

  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

void tick_fxaa_draw(ecs_iter_t *it) {
  TracyCZoneNC(ctx, "FXAA Draw Tick", TracyCategoryColorRendering, true);
  tb_auto ecs = it->world;
  ECS_COMPONENT(ecs, TbRenderSystem);
  ECS_COMPONENT(ecs, TbRenderPipelineSystem);
  ECS_COMPONENT(ecs, TbRenderTargetSystem);
  ECS_COMPONENT(ecs, TbFXAASystem);

  tb_auto *self = ecs_field(it, TbFXAASystem, 1);
  tb_auto *rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  tb_auto *rp_sys = ecs_singleton_get_mut(ecs, TbRenderPipelineSystem);
  tb_auto *rt_sys = ecs_singleton_get_mut(ecs, TbRenderTargetSystem);

  // Descriptor set writes
  {
    VkDescriptorPoolCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes =
            (VkDescriptorPoolSize[1]){
                {
                    .descriptorCount = 1,
                    .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                },
            },
    };
    tb_rnd_frame_desc_pool_tick(rnd_sys, &create_info, &self->set_layout, NULL,
                                self->pools.pools, 1);
    VkDescriptorSet set =
        tb_rnd_frame_desc_pool_get_set(rnd_sys, self->pools.pools, 0);

    VkImageView ldr_color = tb_render_target_get_mip_view(
        rt_sys, 0, 0, rnd_sys->frame_idx, rt_sys->ldr_target);

    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        .pImageInfo =
            &(VkDescriptorImageInfo){
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .imageView = ldr_color,
            },
    };
    tb_rnd_update_descriptors(rnd_sys, 1, &write);
  }

  // Issue draws
  {
    // TODO: Make this less hacky
    const uint32_t width = rnd_sys->render_thread->swapchain.width;
    const uint32_t height = rnd_sys->render_thread->swapchain.height;

    FXAABatch fxaa_batch = {
        .set = tb_rnd_frame_desc_pool_get_set(rnd_sys, self->pools.pools, 0),
        .consts = self->settings,
    };
    TbDrawBatch batch = {
        .layout = self->pipe_layout,
        .pipeline = self->pipeline,
        .viewport = {0, height, width, -(float)height, 0, 1},
        .scissor = {{0, 0}, {width, height}},
        .user_batch = &fxaa_batch,
    };
    tb_render_pipeline_issue_draw_batch(rp_sys, self->draw_ctx, 1, &batch);
  }

  TracyCZoneEnd(ctx);
}

void tb_register_fxaa_system(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;
  ECS_COMPONENT(ecs, TbRenderSystem);
  ECS_COMPONENT(ecs, TbRenderPipelineSystem);
  ECS_COMPONENT(ecs, TbFXAASystem);

  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  tb_auto rp_sys = ecs_singleton_get_mut(ecs, TbRenderPipelineSystem);

  TbFXAASystem sys = {0};
  // Create Set Layout
  {
    VkDescriptorSetLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 2,
        .pBindings =
            (VkDescriptorSetLayoutBinding[2]){
                {
                    .binding = 0,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                },
                {
                    .binding = 1,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .pImmutableSamplers = (VkSampler[1]){rp_sys->sampler},
                },
            },
    };
    tb_rnd_create_set_layout(rnd_sys, &create_info, "FXAA Set Layout",
                             &sys.set_layout);
  }
  // Create Pipeline Layout
  {
    VkPipelineLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = (VkDescriptorSetLayout[1]){sys.set_layout},
        .pushConstantRangeCount = 1,
        .pPushConstantRanges =
            (VkPushConstantRange[1]){
                {
                    .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .offset = 0,
                    .size = sizeof(TbFXAAPushConstants),
                },
            },
    };
    tb_rnd_create_pipeline_layout(rnd_sys, &create_info, "FXAA Pipeline Layout",
                                  &sys.pipe_layout);
  }
  // Create Pipeline
  {
    VkShaderModule vert_mod = VK_NULL_HANDLE;
    {
      VkShaderModuleCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
          .codeSize = sizeof(fxaa_vert),
          .pCode = (const uint32_t *)fxaa_vert,
      };
      tb_rnd_create_shader(rnd_sys, &create_info, "FXAA Vert", &vert_mod);
    }
    VkShaderModule frag_mod = VK_NULL_HANDLE;
    {
      VkShaderModuleCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
          .codeSize = sizeof(fxaa_frag),
          .pCode = (const uint32_t *)fxaa_frag,
      };
      tb_rnd_create_shader(rnd_sys, &create_info, "FXAA Frag", &frag_mod);
    }

    VkGraphicsPipelineCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext =
            &(VkPipelineRenderingCreateInfo){
                .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
                .colorAttachmentCount = 1,
                .pColorAttachmentFormats =
                    (VkFormat[1]){VK_FORMAT_B8G8R8A8_UNORM},
            },
        .stageCount = 2,
        .pStages =
            (VkPipelineShaderStageCreateInfo[2]){
                {
                    .sType =
                        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .stage = VK_SHADER_STAGE_VERTEX_BIT,
                    .module = vert_mod,
                    .pName = "vert",
                },
                {
                    .sType =
                        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .module = frag_mod,
                    .pName = "frag",
                },
            },
        .pVertexInputState =
            &(VkPipelineVertexInputStateCreateInfo){
                .sType =
                    VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            },
        .pInputAssemblyState =
            &(VkPipelineInputAssemblyStateCreateInfo){
                .sType =
                    VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            },
        .pViewportState =
            &(VkPipelineViewportStateCreateInfo){
                .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                .viewportCount = 1,
                .pViewports = &(VkViewport){0, 600.0f, 800.0f, -600.0f, 0, 1},
                .scissorCount = 1,
                .pScissors = &(VkRect2D){{0, 0}, {800, 600}},
            },
        .pRasterizationState =
            &(VkPipelineRasterizationStateCreateInfo){
                .sType =
                    VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                .polygonMode = VK_POLYGON_MODE_FILL,
                .cullMode = VK_CULL_MODE_NONE,
                .lineWidth = 1.0f,
            },
        .pMultisampleState =
            &(VkPipelineMultisampleStateCreateInfo){
                .sType =
                    VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
            },
        .pColorBlendState =
            &(VkPipelineColorBlendStateCreateInfo){
                .sType =
                    VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                .attachmentCount = 1,
                .pAttachments =
                    &(VkPipelineColorBlendAttachmentState){
                        .blendEnable = VK_FALSE,
                        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                          VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT |
                                          VK_COLOR_COMPONENT_A_BIT,
                    },
            },
        .pDepthStencilState =
            &(VkPipelineDepthStencilStateCreateInfo){
                .sType =
                    VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            },
        .pDynamicState =
            &(VkPipelineDynamicStateCreateInfo){
                .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                .dynamicStateCount = 2,
                .pDynamicStates = (VkDynamicState[2]){VK_DYNAMIC_STATE_VIEWPORT,
                                                      VK_DYNAMIC_STATE_SCISSOR},
            },
        .layout = sys.pipe_layout,
    };
    tb_rnd_create_graphics_pipelines(rnd_sys, 1, &create_info, "FXAA Pipeline",
                                     &sys.pipeline);

    tb_rnd_destroy_shader(rnd_sys, vert_mod);
    tb_rnd_destroy_shader(rnd_sys, frag_mod);
  }
  // Register draw context
  {
    TbDrawContextDescriptor desc = {
        .batch_size = sizeof(FXAABatch),
        .draw_fn = record_fxaa,
        .pass_id = rp_sys->fxaa_pass,
    };
    sys.draw_ctx = tb_render_pipeline_register_draw_context(rp_sys, &desc);
    TB_CHECK(sys.draw_ctx != InvalidDispatchContextId,
             "Failed to create fxaa draw context");
  }

  sys.settings = (TbFXAAPushConstants){
      .edge_threshold = 1.0 / 8.0,
      .edge_threshold_min = 1.0 / 24.0,
      .search_steps = 32,
      .search_accel = 1,
      .search_threshold = 1.0 / 4.0,
      .subpixel = 1,
      .subpixel_cap = 3.0 / 4.0,
      .subpixel_trim = 1.0 / 4.0,
      .subpixel_trim_scale = 1.0 / (1.0 - sys.settings.subpixel_trim),
  };

  // Sets a singleton based on the value at a pointer
  ecs_set_ptr(ecs, ecs_id(TbFXAASystem), TbFXAASystem, &sys);

  ECS_SYSTEM(ecs, tick_fxaa_draw, EcsOnUpdate, TbFXAASystem(TbFXAASystem));
}

void tb_unregister_fxaa_system(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;
  ECS_COMPONENT(ecs, TbFXAASystem);
  ECS_COMPONENT(ecs, TbRenderSystem);
  tb_auto sys = ecs_singleton_get_mut(ecs, TbFXAASystem);
  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  tb_rnd_destroy_set_layout(rnd_sys, sys->set_layout);
  tb_rnd_destroy_pipe_layout(rnd_sys, sys->pipe_layout);
  tb_rnd_destroy_pipeline(rnd_sys, sys->pipeline);
  ecs_singleton_remove(ecs, TbFXAASystem);
}
