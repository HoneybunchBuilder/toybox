#pragma once

#include "allocator.h"
#include "dynarray.h"
#include "simd.h"
#include "tbrendercommon.h"

typedef struct TbVLogFrame TbVLogFrame;

typedef struct TbRenderSystem TbRenderSystem;
typedef struct TbViewSystem TbViewSystem;
typedef struct TbRenderObjectSystem TbRenderObjectSystem;
typedef struct TbRenderPipelineSystem TbRenderPipelineSystem;
typedef struct TbMeshSystem TbMeshSystem;

typedef struct VkPipelineLayout_T *VkPipelineLayout;
typedef struct VkPipeline_T *VkPipeline;
typedef struct VkBuffer_T *VkBuffer;
typedef uint32_t TbDrawContextId;
typedef TbResourceId TbMeshId;

typedef struct TbWorld TbWorld;

typedef struct TbVisualLoggingSystem {
  TbAllocator tmp_alloc;
  TbAllocator gp_alloc;

  TbRenderSystem *rnd_sys;
  TbViewSystem *view_sys;
  TbRenderPipelineSystem *rp_sys;
  TbMeshSystem *mesh_system;

  bool *ui;

  TbMeshId sphere_mesh;
  uint32_t sphere_index_type;
  uint32_t sphere_index_count;
  uint32_t sphere_pos_offset;
  float3 sphere_scale;
  VkBuffer sphere_geom_buffer;

  VkPipelineLayout pipe_layout;
  VkPipeline pipeline;
  TbDrawContextId draw_ctx;

  bool logging;
  int32_t log_frame_idx;

  bool recording;
  TB_DYN_ARR_OF(TbVLogFrame) frames;

} TbVisualLoggingSystem;

void tb_register_visual_logging_sys(TbWorld *world);
void tb_unregister_visual_logging_sys(TbWorld *world);

void tb_vlog_begin_recording(TbVisualLoggingSystem *vlog);
void tb_vlog_end_recording(TbVisualLoggingSystem *vlog);
void tb_vlog_clear(TbVisualLoggingSystem *vlog);

void tb_vlog_line(TbVisualLoggingSystem *vlog, float3 start, float3 end,
                  float3 color);
void tb_vlog_location(TbVisualLoggingSystem *vlog, float3 position,
                      float radius, float3 color);
