#pragma once

#include "tb_allocator.h"
#include "tb_dynarray.h"
#include "tb_mesh_system.h"
#include "tb_render_common.h"
#include "tb_simd.h"
#include "tb_system_priority.h"

#include <flecs.h>

#define TB_VLOG_SYS_PRIO TB_SYSTEM_NORMAL

typedef struct TbVLogFrame TbVLogFrame;

typedef struct TbRenderSystem TbRenderSystem;
typedef struct TbViewSystem TbViewSystem;
typedef struct TbRenderObjectSystem TbRenderObjectSystem;
typedef struct TbRenderPipelineSystem TbRenderPipelineSystem;
typedef struct TbMeshSystem TbMeshSystem;

typedef struct VkPipelineLayout_T *VkPipelineLayout;
typedef struct VkBuffer_T *VkBuffer;
typedef uint32_t TbDrawContextId;

typedef struct TbWorld TbWorld;

typedef struct TbVisualLoggingSystem {
  TbAllocator tmp_alloc;
  TbAllocator gp_alloc;

  TbRenderSystem *rnd_sys;
  TbViewSystem *view_sys;
  TbRenderPipelineSystem *rp_sys;
  TbMeshSystem *mesh_system;

  bool *ui;

  TbMesh2 sphere_mesh2;
  uint32_t sphere_index_type;
  uint32_t sphere_index_count;
  uint32_t sphere_pos_offset;
  float3 sphere_scale;

  VkPipelineLayout pipe_layout;
  ecs_entity_t shader;

  TbDrawContextId draw_ctx;

  bool logging;
  int32_t log_frame_idx;

  bool recording;
  TB_DYN_ARR_OF(TbVLogFrame) frames;

} TbVisualLoggingSystem;
extern ECS_COMPONENT_DECLARE(TbVisualLoggingSystem);

void tb_vlog_begin_recording(TbVisualLoggingSystem *vlog);
void tb_vlog_end_recording(TbVisualLoggingSystem *vlog);
void tb_vlog_clear(TbVisualLoggingSystem *vlog);

void tb_vlog_line(TbVisualLoggingSystem *vlog, float3 start, float3 end,
                  float3 color);
void tb_vlog_location(TbVisualLoggingSystem *vlog, float3 position,
                      float radius, float3 color);
