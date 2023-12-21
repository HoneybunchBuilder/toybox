#include "luminance.h"

#include "profiling.h"
#include "renderpipelinesystem.h"
#include "rendersystem.h"
#include "tbcommon.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-variable-declarations"
#include "lumavg_comp.h"
#include "lumhist_comp.h"
#pragma clang diagnostic pop

void record_lum_common(VkCommandBuffer buffer, uint32_t batch_count,
                       const TbDispatchBatch *batches) {
  for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
    const TbDispatchBatch *batch = &batches[batch_idx];
    const TbLuminanceBatch *lum_set =
        (const TbLuminanceBatch *)batch->user_batch;

    VkPipelineLayout layout = batch->layout;

    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_COMPUTE, batch->pipeline);
    vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0,
                            1, &lum_set->set, 0, NULL);
    vkCmdPushConstants(buffer, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(TbLuminancePushConstants), &lum_set->consts);

    for (uint32_t i = 0; i < batch->group_count; i++) {
      uint3 group = batch->groups[i];
      vkCmdDispatch(buffer, group[0], group[1], group[2]);
    }
  }
}

void record_luminance_gather(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                             uint32_t batch_count,
                             const TbDispatchBatch *batches) {
  TracyCZoneNC(ctx, "Luminance Gather Record", TracyCategoryColorRendering,
               true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Luminance Gather", 3, true);
  cmd_begin_label(buffer, "Luminance Gather", (float4){0.4f, 0.0f, 0.0f, 1.0f});

  record_lum_common(buffer, batch_count, batches);

  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

VkResult create_lum_gather_set_layout(TbRenderSystem *rnd_sys,
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
                  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
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
                                          "Lum Gather Set Layout", layout);
  TB_VK_CHECK(err, "Failed to create lum gather descriptor set layout");
  return err;
}

VkResult create_lum_gather_pipe_layout(TbRenderSystem *rnd_sys,
                                       VkDescriptorSetLayout set_layout,
                                       VkPipelineLayout *layout) {
  VkPipelineLayoutCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts =
          (VkDescriptorSetLayout[1]){
              set_layout,
          },
      .pushConstantRangeCount = 1,
      .pPushConstantRanges =
          (VkPushConstantRange[1]){
              {
                  .offset = 0,
                  .size = sizeof(TbLuminancePushConstants),
                  .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
              },
          },
  };
  VkResult err = tb_rnd_create_pipeline_layout(
      rnd_sys, &create_info, "Lum Gather Pipeline Layout", layout);
  TB_VK_CHECK(err, "Failed to create lum gather pipeline layout");
  return err;
}

VkResult create_lum_gather_pipeline(TbRenderSystem *rnd_sys,
                                    VkPipelineLayout layout,
                                    VkPipeline *pipeline) {
  VkResult err = VK_SUCCESS;
  VkShaderModule lumhist_comp_mod = VK_NULL_HANDLE;

  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    };
    create_info.codeSize = sizeof(lumhist_comp);
    create_info.pCode = (const uint32_t *)lumhist_comp;
    err = tb_rnd_create_shader(rnd_sys, &create_info,
                               "Luminance Histogram Comp", &lumhist_comp_mod);
    TB_VK_CHECK_RET(
        err, "Failed to load luminance histogram compute shader module", err);
  }

  VkComputePipelineCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage =
          (VkPipelineShaderStageCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              .stage = VK_SHADER_STAGE_COMPUTE_BIT,
              .module = lumhist_comp_mod,
              .pName = "comp",
          },
      .layout = layout,
  };
  err = tb_rnd_create_compute_pipelines(
      rnd_sys, 1, &create_info, "Luminance Histogram Pipeline", pipeline);
  TB_VK_CHECK_RET(err, "Failed to create lum hist pipeline", err);

  tb_rnd_destroy_shader(rnd_sys, lumhist_comp_mod);

  return err;
}

VkResult tb_create_lum_hist_work(TbRenderSystem *rnd_sys,
                                 TbRenderPipelineSystem *rp_sys,
                                 VkSampler sampler, TbRenderPassId pass,
                                 TbLumHistRenderWork *work) {
  VkResult err = VK_SUCCESS;
  err = create_lum_gather_set_layout(rnd_sys, sampler, &work->set_layout);

  err = create_lum_gather_pipe_layout(rnd_sys, work->set_layout,
                                      &work->pipe_layout);

  err = create_lum_gather_pipeline(rnd_sys, work->pipe_layout, &work->pipeline);

  // Create histogram buffer
  {
    VkBufferCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(uint32_t) * 256,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    };
    err = tb_rnd_sys_alloc_gpu_buffer(
        rnd_sys, &create_info, "Luminance Histogram", &work->lum_histogram);
    TB_VK_CHECK(err, "Failed to create luminance histogram buffer");
  }

  TbDispatchContextDescriptor desc = {
      .batch_size = sizeof(TbLuminanceBatch),
      .dispatch_fn = record_luminance_gather,
      .pass_id = pass,
  };
  work->ctx = tb_render_pipeline_register_dispatch_context(rp_sys, &desc);
  TB_CHECK(work->ctx != InvalidDispatchContextId,
           "Failed to create lum gather dispatch context");
  return err;
}

void tb_destroy_lum_hist_work(TbRenderSystem *rnd_sys,
                              TbLumHistRenderWork *work) {
  tb_rnd_destroy_set_layout(rnd_sys, work->set_layout);
  tb_rnd_destroy_pipe_layout(rnd_sys, work->pipe_layout);
  tb_rnd_destroy_pipeline(rnd_sys, work->pipeline);
  tb_rnd_free_gpu_buffer(rnd_sys, &work->lum_histogram);
}

void record_luminance_average(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                              uint32_t batch_count,
                              const TbDispatchBatch *batches) {
  TracyCZoneNC(ctx, "Luminance Average Record", TracyCategoryColorRendering,
               true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Luminance Average", 3, true);
  cmd_begin_label(buffer, "Luminance Average",
                  (float4){0.4f, 0.0f, 0.0f, 1.0f});

  record_lum_common(buffer, batch_count, batches);

  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

VkResult create_lum_avg_set_layout(TbRenderSystem *rnd_sys,
                                   VkDescriptorSetLayout *layout) {
  VkDescriptorSetLayoutCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = 2,
      .pBindings =
          (VkDescriptorSetLayoutBinding[2]){
              {
                  .binding = 0,
                  .descriptorCount = 1,
                  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                  .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
              },
              {
                  .binding = 1,
                  .descriptorCount = 1,
                  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                  .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
              },
          },
  };
  VkResult err = tb_rnd_create_set_layout(rnd_sys, &create_info,
                                          "Lum Average Set Layout", layout);
  TB_VK_CHECK(err, "Failed to create lum average descriptor set layout");
  return err;
}

VkResult create_lum_avg_pipeline(TbRenderSystem *rnd_sys,
                                 VkPipelineLayout layout,
                                 VkPipeline *pipeline) {
  VkResult err = VK_SUCCESS;

  VkShaderModule lumavg_comp_mod = VK_NULL_HANDLE;
  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    };
    create_info.codeSize = sizeof(lumavg_comp);
    create_info.pCode = (const uint32_t *)lumavg_comp;
    err = tb_rnd_create_shader(rnd_sys, &create_info, "Luminance Average Comp",
                               &lumavg_comp_mod);
    TB_VK_CHECK_RET(
        err, "Failed to load luminance average compute shader module", err);
  }

  VkComputePipelineCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage =
          (VkPipelineShaderStageCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              .stage = VK_SHADER_STAGE_COMPUTE_BIT,
              .module = lumavg_comp_mod,
              .pName = "comp",
          },
      .layout = layout,
  };
  err = tb_rnd_create_compute_pipelines(rnd_sys, 1, &create_info,
                                        "Luminance Average Pipeline", pipeline);
  TB_VK_CHECK_RET(err, "Failed to create lum avg pipeline", err);

  tb_rnd_destroy_shader(rnd_sys, lumavg_comp_mod);

  return err;
}

VkResult create_lum_avg_pipe_layout(TbRenderSystem *rnd_sys,
                                    VkDescriptorSetLayout set_layout,
                                    VkPipelineLayout *layout) {
  VkPipelineLayoutCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts =
          (VkDescriptorSetLayout[1]){
              set_layout,
          },
      .pushConstantRangeCount = 1,
      .pPushConstantRanges =
          (VkPushConstantRange[1]){
              {
                  .offset = 0,
                  .size = sizeof(TbLuminancePushConstants),
                  .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
              },
          },
  };
  VkResult err = tb_rnd_create_pipeline_layout(
      rnd_sys, &create_info, "Lum Average Pipeline Layout", layout);
  TB_VK_CHECK(err, "Failed to create lum average pipeline layout");
  return err;
}

VkResult tb_create_lum_avg_work(TbRenderSystem *rnd_sys,
                                TbRenderPipelineSystem *rp_sys,
                                TbRenderPassId pass, TbLumAvgRenderWork *work) {
  VkResult err = VK_SUCCESS;
  err = create_lum_avg_set_layout(rnd_sys, &work->set_layout);

  err =
      create_lum_avg_pipe_layout(rnd_sys, work->set_layout, &work->pipe_layout);

  err = create_lum_avg_pipeline(rnd_sys, work->pipe_layout, &work->pipeline);

  // Create luminance average buffer
  {
    VkBufferCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(float),
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    };
    err = tb_rnd_sys_alloc_gpu_buffer(rnd_sys, &create_info,
                                      "Luminance Average", &work->lum_avg);
    TB_VK_CHECK(err, "Failed to create luminance average buffer");
  }

  TbDispatchContextDescriptor desc = {
      .batch_size = sizeof(TbLuminanceBatch),
      .dispatch_fn = record_luminance_average,
      .pass_id = pass,
  };
  work->ctx = tb_render_pipeline_register_dispatch_context(rp_sys, &desc);
  TB_CHECK(work->ctx != InvalidDispatchContextId,
           "Failed to create lum average dispatch context");
  return err;
}

void tb_destroy_lum_avg_work(TbRenderSystem *rnd_sys,
                             TbLumAvgRenderWork *work) {
  tb_rnd_destroy_set_layout(rnd_sys, work->set_layout);
  tb_rnd_destroy_pipe_layout(rnd_sys, work->pipe_layout);
  tb_rnd_destroy_pipeline(rnd_sys, work->pipeline);
  tb_rnd_free_gpu_buffer(rnd_sys, &work->lum_avg);
}
