#pragma once

#include "tbrendercommon.h"

typedef struct FXAASystem {

  TbRenderTargetId input_target;
  TbRenderTargetId output_target;

  TbDrawContextId draw_ctx;

  VkDescriptorSetLayout set_layout;
  VkPipelineLayout pipe_layout;
  VkPipeline pipeline;
} FXAASystem;
