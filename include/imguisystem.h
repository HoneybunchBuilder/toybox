#pragma once

#include "tbcommon.h"
#include "tbrendercommon.h"
#include "world.h"

#define ImGuiSystemId 0xDEADFA11

typedef struct RenderSystem RenderSystem;
typedef struct RenderPipelineSystem RenderPipelineSystem;
typedef struct RenderTargetSystem RenderTargetSystem;
typedef struct InputSystem InputSystem;

typedef struct VkSampler_T *VkSampler;
typedef struct VkDescriptorSetLayout_T *VkDescriptorSetLayout;
typedef struct VkPipelineLayout_T *VkPipelineLayout;
typedef struct VkPipeline_T *VkPipeline;
typedef struct VkFramebuffer_T *VkFramebuffer;
typedef uint32_t TbDrawContextId;

typedef struct ImFontAtlas ImFontAtlas;
typedef struct ImGuiContext ImGuiContext;
typedef struct ImGuiIO ImGuiIO;

#define TB_MAX_UI_CONTEXTS 4

typedef struct UIContext {
  ImGuiContext *context;
  TbImage atlas;
  VkImageView atlas_view;
} UIContext;

typedef struct ImGuiSystemDescriptor {
  Allocator std_alloc;
  Allocator tmp_alloc;
  uint32_t context_count;
  ImFontAtlas *context_atlases[TB_MAX_UI_CONTEXTS];
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
  RenderPipelineSystem *render_pipe_system;
  RenderTargetSystem *render_target_system;
  InputSystem *input;

  ImGuiFrameState frame_states[TB_MAX_FRAME_STATES];

  TbDrawContextId imgui_draw_ctx;

  VkSampler sampler;
  VkDescriptorSetLayout set_layout;
  VkPipelineLayout pipe_layout;
  VkPipeline pipeline;

  uint32_t context_count;
  UIContext contexts[TB_MAX_UI_CONTEXTS];
} ImGuiSystem;

void tb_imgui_system_descriptor(SystemDescriptor *desc,
                                const ImGuiSystemDescriptor *imgui_desc);
