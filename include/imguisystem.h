#pragma once

#include "renderpipelinesystem.h"
#include "tbcommon.h"
#include "tbrendercommon.h"
#include "world.h"

typedef struct TbRenderSystem TbRenderSystem;
typedef struct TbRenderPipelineSystem TbRenderPipelineSystem;
typedef struct TbRenderTargetSystem TbRenderTargetSystem;
typedef struct TbInputSystem TbInputSystem;
typedef struct TbWorld TbWorld;

typedef struct VkSampler_T *VkSampler;
typedef struct VkDescriptorSetLayout_T *VkDescriptorSetLayout;
typedef struct VkPipelineLayout_T *VkPipelineLayout;
typedef struct VkPipeline_T *VkPipeline;
typedef struct VkFramebuffer_T *VkFramebuffer;
typedef uint32_t TbDrawContextId;

typedef struct ImFontAtlas ImFontAtlas;
typedef struct ImGuiContext ImGuiContext;
typedef struct ImGuiIO ImGuiIO;

#define TB_IMGUI_SYS_PRIO (TB_RP_SYS_PRIO + 1)

#define TB_MAX_UI_CONTEXTS 4

typedef struct TbUIContext {
  ImGuiContext *context;
  TbImage atlas;
  VkImageView atlas_view;
} TbUIContext;

typedef struct TbImGuiFrameState {
  uint32_t set_count;
  VkDescriptorPool set_pool;
  VkDescriptorSet *sets;
} TbImGuiFrameState;

typedef struct TbImGuiSystem {
  TbAllocator gp_alloc;
  TbAllocator tmp_alloc;

  TbRenderSystem *rnd_sys;
  TbRenderPipelineSystem *rp_sys;
  TbRenderTargetSystem *rt_sys;
  TbInputSystem *input;

  TbImGuiFrameState frame_states[TB_MAX_FRAME_STATES];

  TbDrawContextId imgui_draw_ctx;

  VkSampler sampler;
  VkDescriptorSetLayout set_layout;
  VkPipelineLayout pipe_layout;
  VkPipeline pipeline;

  uint32_t context_count;
  TbUIContext contexts[TB_MAX_UI_CONTEXTS];
} TbImGuiSystem;
