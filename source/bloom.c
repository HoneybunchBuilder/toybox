#include "bloom.h"

#include "profiling.h"
#include "renderpipelinesystem.h"
#include "tbcommon.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-variable-declarations"
#include "downsample_comp.h"
#include "upsample_comp.h"
#pragma clang diagnostic pop

void record_downsample(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                       uint32_t batch_count, const TbDispatchBatch *batches) {
  TracyCZoneNC(ctx, "Downsample Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Downsample", 3, true);
  cmd_begin_label(buffer, "Downsample", (float4){0.0f, 0.5f, 0.0f, 1.0f});

  for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
    const TbDispatchBatch *batch = &batches[batch_idx];
    const DownsampleBatch *down_batch =
        (const DownsampleBatch *)batch->user_batch;

    VkPipelineLayout layout = batch->layout;

    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_COMPUTE, batch->pipeline);
    vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0,
                            1, &down_batch->set, 0, NULL);

    for (uint32_t i = 0; i < batch->group_count; i++) {
      uint3 group = batch->groups[i];
      vkCmdDispatch(buffer, group[0], group[1], group[2]);
    }
  }

  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

VkResult create_downsample_set_layout(TbRenderSystem *rnd_sys,
                                      VkSampler sampler,
                                      VkDescriptorSetLayout *layout) {
  VkDescriptorSetLayoutCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = 3,
      .pBindings =
          (VkDescriptorSetLayoutBinding[3]){
              {
                  .binding = 0,
                  .descriptorCount = 1,
                  .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                  .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
              },
              {
                  .binding = 1,
                  .descriptorCount = 1,
                  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                  .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
              },
              {
                  .binding = 2,
                  .descriptorCount = 1,
                  .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                  .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                  .pImmutableSamplers = &sampler,
              },
          },
  };
  VkResult err = tb_rnd_create_set_layout(rnd_sys, &create_info,
                                          "Downsample Set Layout", layout);
  TB_VK_CHECK(err, "Failed to create downsample descriptor set layout");
  return err;
}

VkResult create_downsample_pipe_layout(TbRenderSystem *rnd_sys,
                                       VkDescriptorSetLayout set_layout,
                                       VkPipelineLayout *layout) {
  VkPipelineLayoutCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts =
          (VkDescriptorSetLayout[1]){
              set_layout,
          },
  };
  VkResult err = tb_rnd_create_pipeline_layout(
      rnd_sys, &create_info, "Downsample Pipeline Layout", layout);
  TB_VK_CHECK(err, "Failed to create downsample pipeline layout");
  return err;
}

VkResult create_downsample_pipeline(TbRenderSystem *rnd_sys,
                                    VkPipelineLayout layout,
                                    VkPipeline *pipeline) {
  VkResult err = VK_SUCCESS;
  VkShaderModule downsample_comp_mod = VK_NULL_HANDLE;

  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    };
    create_info.codeSize = sizeof(downsample_comp);
    create_info.pCode = (const uint32_t *)downsample_comp;
    err = tb_rnd_create_shader(rnd_sys, &create_info, "Downsample Comp",
                               &downsample_comp_mod);
    TB_VK_CHECK_RET(err, "Failed to load downsample compute shader module",
                    err);
  }

  VkComputePipelineCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage =
          (VkPipelineShaderStageCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              .stage = VK_SHADER_STAGE_COMPUTE_BIT,
              .module = downsample_comp_mod,
              .pName = "comp",
          },
      .layout = layout,
  };
  err = tb_rnd_create_compute_pipelines(rnd_sys, 1, &create_info,
                                        "Downsample Pipeline", pipeline);
  TB_VK_CHECK_RET(err, "Failed to create downsample pipeline", err);

  tb_rnd_destroy_shader(rnd_sys, downsample_comp_mod);

  return err;
}

void record_upsample(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                     uint32_t batch_count, const TbDispatchBatch *batches) {
  TracyCZoneNC(ctx, "Upsample Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Upsample", 3, true);
  cmd_begin_label(buffer, "Upsample", (float4){0.0f, 0.5f, 0.0f, 1.0f});

  for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
    const TbDispatchBatch *batch = &batches[batch_idx];
    const UpsampleBatch *up_batch = (const UpsampleBatch *)batch->user_batch;

    VkPipelineLayout layout = batch->layout;

    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_COMPUTE, batch->pipeline);
    vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0,
                            1, &up_batch->set, 0, NULL);

    for (uint32_t i = 0; i < batch->group_count; i++) {
      uint3 group = batch->groups[i];
      vkCmdDispatch(buffer, group[0], group[1], group[2]);
    }
  }

  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

VkResult create_upsample_set_layout(TbRenderSystem *rnd_sys,
                                    VkSampler sampler,
                                    VkDescriptorSetLayout *layout) {
  VkDescriptorSetLayoutCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = 3,
      .pBindings =
          (VkDescriptorSetLayoutBinding[3]){
              {
                  .binding = 0,
                  .descriptorCount = 1,
                  .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                  .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
              },
              {
                  .binding = 1,
                  .descriptorCount = 1,
                  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                  .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
              },
              {
                  .binding = 2,
                  .descriptorCount = 1,
                  .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                  .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                  .pImmutableSamplers = &sampler,
              },
          },
  };
  VkResult err = tb_rnd_create_set_layout(rnd_sys, &create_info,
                                          "Upsample Set Layout", layout);
  TB_VK_CHECK(err, "Failed to create upsample descriptor set layout");
  return err;
}

VkResult create_upsample_pipe_layout(TbRenderSystem *rnd_sys,
                                     VkDescriptorSetLayout set_layout,
                                     VkPipelineLayout *layout) {
  VkPipelineLayoutCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts =
          (VkDescriptorSetLayout[1]){
              set_layout,
          },
  };
  VkResult err = tb_rnd_create_pipeline_layout(
      rnd_sys, &create_info, "Upsample Pipeline Layout", layout);
  TB_VK_CHECK(err, "Failed to create upsample pipeline layout");
  return err;
}

VkResult create_upsample_pipeline(TbRenderSystem *rnd_sys,
                                  VkPipelineLayout layout,
                                  VkPipeline *pipeline) {
  VkResult err = VK_SUCCESS;
  VkShaderModule upsample_comp_mod = VK_NULL_HANDLE;

  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    };
    create_info.codeSize = sizeof(upsample_comp);
    create_info.pCode = (const uint32_t *)upsample_comp;
    err = tb_rnd_create_shader(rnd_sys, &create_info, "Upsample Comp",
                               &upsample_comp_mod);
    TB_VK_CHECK_RET(err, "Failed to load upsample compute shader module", err);
  }

  VkComputePipelineCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage =
          (VkPipelineShaderStageCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              .stage = VK_SHADER_STAGE_COMPUTE_BIT,
              .module = upsample_comp_mod,
              .pName = "comp",
          },
      .layout = layout,
  };
  err = tb_rnd_create_compute_pipelines(rnd_sys, 1, &create_info,
                                        "Upsample Pipeline", pipeline);
  TB_VK_CHECK_RET(err, "Failed to create upsample pipeline", err);

  tb_rnd_destroy_shader(rnd_sys, upsample_comp_mod);

  return err;
}

VkResult tb_create_downsample_work(TbRenderSystem *rnd_sys,
                                   TbRenderPipelineSystem *rp_sys,
                                   VkSampler sampler, TbRenderPassId pass,
                                   DownsampleRenderWork *work) {
  create_downsample_set_layout(rnd_sys, sampler, &work->set_layout);
  create_downsample_pipe_layout(rnd_sys, work->set_layout,
                                &work->pipe_layout);
  create_downsample_pipeline(rnd_sys, work->pipe_layout, &work->pipeline);

  work->ctx = tb_render_pipeline_register_dispatch_context(
      rp_sys, &(TbDispatchContextDescriptor){
                       .batch_size = sizeof(DownsampleBatch),
                       .dispatch_fn = record_downsample,
                       .pass_id = pass,
                   });
  TB_CHECK(work->ctx != InvalidDispatchContextId,
           "Failed to create downsample dispatch context");
  return VK_SUCCESS;
}

void tb_destroy_downsample_work(TbRenderSystem *rnd_sys,
                                DownsampleRenderWork *work) {
  tb_rnd_destroy_set_layout(rnd_sys, work->set_layout);
  tb_rnd_destroy_pipe_layout(rnd_sys, work->pipe_layout);
  tb_rnd_destroy_pipeline(rnd_sys, work->pipeline);
}

VkResult tb_create_upsample_work(TbRenderSystem *rnd_sys,
                                 TbRenderPipelineSystem *rp_sys,
                                 VkSampler sampler, TbRenderPassId pass,
                                 UpsampleRenderWork *work) {
  create_upsample_set_layout(rnd_sys, sampler, &work->set_layout);
  create_upsample_pipe_layout(rnd_sys, work->set_layout,
                              &work->pipe_layout);
  create_upsample_pipeline(rnd_sys, work->pipe_layout, &work->pipeline);

  work->ctx = tb_render_pipeline_register_dispatch_context(
      rp_sys, &(TbDispatchContextDescriptor){
                       .batch_size = sizeof(UpsampleBatch),
                       .dispatch_fn = record_upsample,
                       .pass_id = pass,
                   });
  TB_CHECK(work->ctx != InvalidDispatchContextId,
           "Failed to create upsample dispatch context");
  return VK_SUCCESS;
}

void tb_destroy_upsample_work(TbRenderSystem *rnd_sys,
                              UpsampleRenderWork *work) {
  tb_rnd_destroy_set_layout(rnd_sys, work->set_layout);
  tb_rnd_destroy_pipe_layout(rnd_sys, work->pipe_layout);
  tb_rnd_destroy_pipeline(rnd_sys, work->pipeline);
}
