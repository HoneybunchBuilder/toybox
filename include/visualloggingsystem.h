#pragma once

#include "allocator.h"
#include "dynarray.h"
#include "simd.h"

#define VisualLoggingSystemId 0x1337F001

typedef struct VLogFrame VLogFrame;

typedef struct RenderSystem RenderSystem;
typedef struct ViewSystem ViewSystem;
typedef struct RenderObjectSystem RenderObjectSystem;
typedef struct RenderPipelineSystem RenderPipelineSystem;
typedef struct MeshSystem MeshSystem;

typedef struct VkPipelineLayout_T *VkPipelineLayout;
typedef struct VkPipeline_T *VkPipeline;
typedef struct VkBuffer_T *VkBuffer;
typedef uint32_t TbDrawContextId;
typedef uint64_t TbMeshId;

typedef struct ecs_world_t ecs_world_t;

typedef struct VisualLoggingSystem {
  TbAllocator tmp_alloc;
  TbAllocator std_alloc;

  RenderSystem *render_system;
  ViewSystem *view_system;
  RenderPipelineSystem *render_pipe_system;
  MeshSystem *mesh_system;

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
  TB_DYN_ARR_OF(VLogFrame) frames;

} VisualLoggingSystem;

void tb_register_visual_logging_sys(ecs_world_t *ecs, TbAllocator std_alloc,
                                    TbAllocator tmp_alloc);
void tb_unregister_visual_logging_sys(ecs_world_t *ecs);

void tb_vlog_begin_recording(VisualLoggingSystem *vlog);
void tb_vlog_end_recording(VisualLoggingSystem *vlog);
void tb_vlog_clear(VisualLoggingSystem *vlog);

void tb_vlog_line(VisualLoggingSystem *vlog, float3 start, float3 end,
                  float3 color);
void tb_vlog_location(VisualLoggingSystem *vlog, float3 position, float radius,
                      float3 color);
