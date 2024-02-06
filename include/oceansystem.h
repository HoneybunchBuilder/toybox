#pragma once

#include "allocator.h"
#include "rendersystem.h"
#include "rendertargetsystem.h"
#include "tbrendercommon.h"

typedef struct TbWorld TbWorld;
typedef struct TbMeshSystem TbMeshSystem;
typedef struct TbViewSystem TbViewSystem;
typedef struct TbRenderPipelineSystem TbRenderPipelineSystem;
typedef struct TbRenderTargetSystem TbRenderTargetSystem;
typedef struct TbVisualLoggingSystem TbVisualLoggingSystem;
typedef struct TbAudioSystem TbAudioSystem;

typedef struct VkDescriptorPool_T *VkDescriptorPool;
typedef struct VkDescriptorSet_T *VkDescriptorSet;

typedef TbResourceId TbMeshId;
typedef uint32_t TbDrawContextId;

typedef uint32_t TbMusicId;
typedef uint32_t TbSoundEffectId;

#define TB_OCEAN_SFX_COUNT 4

typedef struct ecs_query_t ecs_query_t;

typedef struct TbOceanSystem {
  TbRenderSystem *rnd_sys;
  TbRenderPipelineSystem *rp_sys;
  TbMeshSystem *mesh_system;
  TbViewSystem *view_sys;
  TbRenderTargetSystem *rt_sys;
  TbVisualLoggingSystem *vlog;
  TbAudioSystem *audio_system;
  TbAllocator tmp_alloc;
  TbAllocator gp_alloc;

  ecs_query_t *ocean_query;

  TbMusicId music;
  TbSoundEffectId wave_sounds[TB_OCEAN_SFX_COUNT];
  float wave_sound_timer;

  TbMeshId ocean_patch_mesh;
  TbTransform ocean_transform;
  float tile_width;
  float tile_depth;
  uint32_t ocean_index_type;
  uint32_t ocean_index_count;
  uint64_t ocean_pos_offset;
  uint64_t ocean_uv_offset;
  VkBuffer ocean_geom_buffer;

  VkSampler sampler;
  VkSampler shadow_sampler;

  TbDrawContextId trans_depth_draw_ctx;
  TbDrawContextId trans_color_draw_ctx;

  TbFrameDescriptorPool ocean_pools[TB_MAX_FRAME_STATES];

  VkDescriptorSetLayout set_layout;
  VkPipelineLayout pipe_layout;
  VkPipeline prepass_pipeline;
  VkPipeline pipeline;
} TbOceanSystem;

void tb_register_ocean_sys(TbWorld *world);
void tb_unregister_ocean_sys(TbWorld *world);
