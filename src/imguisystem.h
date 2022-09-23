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
  Allocator std_alloc;
  Allocator tmp_alloc;
} ImGuiSystemDescriptor;

typedef struct ImGuiFrameState {
  uint32_t set_count;
  VkDescriptorPool set_pool;
  VkDescriptorSet *sets;
} ImGuiFrameState;

typedef struct ImGuiSystem {
  Allocator std_alloc;
  Allocator tmp_alloc;

  RenderSystem *render_system;

  ImGuiFrameState frame_states[TB_MAX_FRAME_STATES];

  VkRenderPass pass;
  VkFramebuffer framebuffers[TB_MAX_FRAME_STATES];

  VkSampler sampler;
  VkDescriptorSetLayout set_layout;
  VkPipelineLayout pipe_layout;
  VkPipeline pipeline;
} ImGuiSystem;

void tb_imgui_system_descriptor(SystemDescriptor *desc,
                                const ImGuiSystemDescriptor *imgui_desc);
