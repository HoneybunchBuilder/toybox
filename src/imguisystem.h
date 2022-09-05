#pragma once

#include "tbcommon.h"
#include "tbrendercommon.h"
#include "world.h"

#define ImGuiSystemId 0xDEADFA11

typedef struct RenderSystem RenderSystem;

typedef struct VkRenderPass_T *VkRenderPass;
typedef struct VkSampler_T *VkSampler;
typedef struct VkDescriptorSetLayout_T *VkDescriptorSetLayout;
typedef struct VkPipelineLayout_T *VkPipelineLayout;
typedef struct VkPipeline_T *VkPipeline;
typedef struct VkFramebuffer_T *VkFramebuffer;

typedef struct ImGuiSystemDescriptor {
  Allocator tmp_alloc;
} ImGuiSystemDescriptor;

typedef struct ImGuiSystem {
  RenderSystem *render_system;
  Allocator tmp_alloc;

  VkRenderPass pass;

  VkSampler sampler;
  VkDescriptorSetLayout set_layout;
  VkPipelineLayout pipe_layout;
  VkPipeline pipeline;

  VkFramebuffer framebuffers[TB_MAX_FRAME_STATES];
} ImGuiSystem;

void tb_imgui_system_descriptor(SystemDescriptor *desc,
                                const ImGuiSystemDescriptor *imgui_desc);
