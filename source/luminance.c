#include "luminance.h"

#include "profiling.h"
#include "renderpipelinesystem.h"
#include "rendersystem.h"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-variable-declarations"
#endif
#include "lumhist_comp.h"
#ifdef __clang__
#pragma clang diagnostic pop
#endif

void record_luminance_gather(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                             uint32_t batch_count,
                             const DispatchBatch *batches) {
  TracyCZoneNC(ctx, "Luminance Gather Record", TracyCategoryColorRendering,
               true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Luminance Gather", 3, true);
  cmd_begin_label(buffer, "Luminance Gather", (float4){0.4f, 0.0f, 0.0f, 1.0f});

  for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
    const DispatchBatch *batch = &batches[batch_idx];
    const LuminanceBatch *lum_set = (const LuminanceBatch *)batch->user_batch;

    VkPipelineLayout layout = batch->layout;

    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_COMPUTE, batch->pipeline);
    vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0,
                            1, &lum_set->set, 0, NULL);
    vkCmdPushConstants(buffer, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       sizeof(LuminancePushConstants), &lum_set->consts);

    for (uint32_t i = 0; i < batch->group_count; i++) {
      uint3 group = batch->groups[i];
      vkCmdDispatch(buffer, group[0], group[1], group[2]);
    }
  }

  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

VkResult create_lum_gather_set_layout(RenderSystem *render_system,
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
  VkResult err = tb_rnd_create_set_layout(render_system, &create_info,
                                          "Lum Gather Set Layout", layout);
  TB_VK_CHECK(err, "Failed to create lum gather descriptor set layout");
  return err;
}

VkResult create_lum_gather_pipe_layout(RenderSystem *render_system,
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
                  .size = sizeof(LuminancePushConstants),
                  .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
              },
          },
  };
  VkResult err = tb_rnd_create_pipeline_layout(
      render_system, &create_info, "Lum Gather Pipeline Layout", layout);
  TB_VK_CHECK(err, "Failed to create lum gather pipeline layout");
  return err;
}

VkResult create_lum_gather_pipeline(RenderSystem *render_system,
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
    err = tb_rnd_create_shader(render_system, &create_info,
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
      render_system, 1, &create_info, "Luminance Histogram Pipeline", pipeline);
  TB_VK_CHECK_RET(err, "Failed to create lum hist pipeline", err);

  tb_rnd_destroy_shader(render_system, lumhist_comp_mod);

  return err;
}
