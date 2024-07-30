#include "tb_camera_component.h"
#include "tb_common.h"
#include "tb_light_component.h"
#include "tb_mesh_component.h"
#include "tb_mesh_rnd_sys.h"
#include "tb_mesh_system.h"
#include "tb_profiling.h"
#include "tb_render_common.h"
#include "tb_render_object_system.h"
#include "tb_render_pipeline_system.h"
#include "tb_render_target_system.h"
#include "tb_shader_system.h"
#include "tb_transform_component.h"
#include "tb_view_system.h"
#include "tb_visual_logging_system.h"
#include "tb_world.h"

#include <flecs.h>

// Ignore some warnings for the generated headers
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-variable-declarations"
// NOLINTBEGIN
#include "depth_frag.h"
#include "depth_vert.h"
// NOLINTEND
#pragma clang diagnostic pop

typedef struct TbShadowSystem {
  TbAllocator gp_alloc;
  TbAllocator tmp_alloc;

  TbDrawContextId draw_ctxs[TB_CASCADE_COUNT];
  VkPipelineLayout pipe_layout;
  VkPipeline pipeline;

  ecs_query_t *dir_light_query;
  TbFrameDescriptorPoolList desc_pool_list;
} TbShadowSystem;
ECS_COMPONENT_DECLARE(TbShadowSystem);

void tb_register_shadow_sys(TbWorld *world);
void tb_unregister_shadow_sys(TbWorld *world);

TB_REGISTER_SYS(tb, shadow, TB_SYSTEM_HIGH)

VkResult create_shadow_pipeline(TbRenderSystem *rnd_sys, VkFormat depth_format,
                                VkPipelineLayout pipe_layout,
                                VkPipeline *pipeline) {
  VkResult err = VK_SUCCESS;

  VkShaderModule vert_mod = VK_NULL_HANDLE;
  VkShaderModule frag_mod = VK_NULL_HANDLE;

  {
    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    };
    create_info.codeSize = sizeof(depth_vert);
    create_info.pCode = (const uint32_t *)depth_vert;
    err = tb_rnd_create_shader(rnd_sys, &create_info, "Shadow Vert", &vert_mod);
    TB_VK_CHECK_RET(err, "Failed to load shadow vert shader module", err);

    create_info.codeSize = sizeof(depth_frag);
    create_info.pCode = (const uint32_t *)depth_frag;
    err = tb_rnd_create_shader(rnd_sys, &create_info, "Shadow Frag", &frag_mod);
    TB_VK_CHECK_RET(err, "Failed to load shadow frag shader module", err);
  }

  VkGraphicsPipelineCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
#if TB_USE_DESC_BUFFER == 1
      .flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
#endif
      .pNext =
          &(VkPipelineRenderingCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
              .depthAttachmentFormat = depth_format,
          },
      .stageCount = 2,
      .pStages =
          (VkPipelineShaderStageCreateInfo[2]){
              {
                  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_VERTEX_BIT,
                  .module = vert_mod,
                  .pName = "vert",
              },
              {
                  .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                  .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                  .module = frag_mod,
                  .pName = "frag",
              },
          },
      .pVertexInputState =
          &(VkPipelineVertexInputStateCreateInfo){
              .sType =
                  VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
          },
      .pInputAssemblyState =
          &(VkPipelineInputAssemblyStateCreateInfo){
              .sType =
                  VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
              .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
          },
      .pViewportState =
          &(VkPipelineViewportStateCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
              .viewportCount = 1,
              .pViewports = &(VkViewport){0, 600.0f, 800.0f, -600.0f, 0, 1},
              .scissorCount = 1,
              .pScissors = &(VkRect2D){{0, 0}, {800, 600}},
          },
      .pRasterizationState =
          &(VkPipelineRasterizationStateCreateInfo){
              .sType =
                  VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
              .depthClampEnable = VK_TRUE,
              .cullMode = VK_CULL_MODE_FRONT_BIT,
              .lineWidth = 1.0f,
          },
      .pMultisampleState =
          &(VkPipelineMultisampleStateCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
              .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
          },
      .pColorBlendState =
          &(VkPipelineColorBlendStateCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
          },
      .pDepthStencilState =
          &(VkPipelineDepthStencilStateCreateInfo){
              .sType =
                  VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
              .depthTestEnable = VK_TRUE,
              .depthWriteEnable = VK_TRUE,
              .depthCompareOp = VK_COMPARE_OP_LESS,
          },
      .pDynamicState =
          &(VkPipelineDynamicStateCreateInfo){
              .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
              .dynamicStateCount = 2,
              .pDynamicStates =
                  (VkDynamicState[2]){
                      VK_DYNAMIC_STATE_VIEWPORT,
                      VK_DYNAMIC_STATE_SCISSOR,
                  },
          },
      .layout = pipe_layout,
  };
  err = tb_rnd_create_graphics_pipelines(rnd_sys, 1, &create_info,
                                         "Shadow Pipeline", pipeline);
  TB_VK_CHECK_RET(err, "Failed to create shadow pipeline", err);

  tb_rnd_destroy_shader(rnd_sys, vert_mod);
  tb_rnd_destroy_shader(rnd_sys, frag_mod);

  return err;
}

void shadow_pass_record(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                        uint32_t batch_count, const TbDrawBatch *batches) {
  TracyCZoneNC(ctx, "Record Shadows", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Shadows", 3, true);
  cmd_begin_label(buffer, "Shadows", (float4){0.8f, 0.0f, 0.4f, 1.0f});

  for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
    tb_auto batch = &batches[batch_idx];
    tb_auto prim_batch = (const TbPrimitiveBatch *)batch->user_batch;
    if (batch->draw_count == 0) {
      continue;
    }

    TracyCZoneNC(batch_ctx, "Shadow Batch", TracyCategoryColorRendering, true);
    cmd_begin_label(buffer, "Batch", (float4){0.4f, 0.0f, 0.2f, 1.0f});

    VkPipelineLayout layout = batch->layout;
    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, batch->pipeline);

    vkCmdSetViewport(buffer, 0, 1, &batch->viewport);
    vkCmdSetScissor(buffer, 0, 1, &batch->scissor);

    const uint32_t set_count = 5;
#if TB_USE_DESC_BUFFER == 1
    {
      const VkDescriptorBufferBindingInfoEXT buffer_bindings[set_count] = {
          prim_batch->view_addr, prim_batch->draw_addr, prim_batch->obj_addr,
          prim_batch->idx_addr,  prim_batch->pos_addr,
      };
      vkCmdBindDescriptorBuffersEXT(buffer, set_count, buffer_bindings);
      uint32_t buf_indices[set_count] = {0, 1, 2, 3, 4};
      VkDeviceSize buf_offsets[set_count] = {0};
      vkCmdSetDescriptorBufferOffsetsEXT(
          buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, set_count,
          buf_indices, buf_offsets);
    }
#else
    {
      VkDescriptorSet sets[set_count] = {
          prim_batch->view_set, prim_batch->draw_set, prim_batch->obj_set,
          prim_batch->idx_set, prim_batch->pos_set};
      vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
                              0, set_count, sets, 0, NULL);
    }
#endif

    for (uint32_t draw_idx = 0; draw_idx < batch->draw_count; ++draw_idx) {
      TracyCZoneNC(draw_ctx, "Record Indirect Draw",
                   TracyCategoryColorRendering, true);
      tb_auto draw = &((const TbIndirectDraw *)batch->draws)[draw_idx];
      vkCmdDrawIndirect(buffer, draw->buffer, draw->offset, draw->draw_count,
                        draw->stride);
      TracyCZoneEnd(draw_ctx);
    }

    cmd_end_label(buffer);
    TracyCZoneEnd(batch_ctx);
  }
  cmd_end_label(buffer);
  TracyCVkZoneEnd(frame_scope);
  TracyCZoneEnd(ctx);
}

void shadow_update_tick(ecs_iter_t *it) {
  TracyCZoneNC(ctx, "Shadow System Update", TracyCategoryColorCore, true);
  ecs_world_t *ecs = it->world;

  tb_auto view_sys = ecs_singleton_get_mut(ecs, TbViewSystem);
  tb_auto shadow_sys = ecs_singleton_get_mut(ecs, TbShadowSystem);

  ecs_singleton_modified(ecs, TbViewSystem);
  ecs_singleton_modified(ecs, TbShadowSystem);

  // For each camera, evaluate each light and calculate any necessary shadow
  // info
  tb_auto cameras = ecs_field(it, TbCameraComponent, 1);
  for (int32_t cam_idx = 0; cam_idx < it->count; ++cam_idx) {
    tb_auto camera = &cameras[cam_idx];

    const float near = camera->near;
    const float far = camera->far;

    // Calculate inv cam vp based on shadow draw distance
    float4x4 inv_cam_vp = {.col0 = tb_f4(0, 0, 0, 0)};
    {
      const TbView *v = tb_get_view(view_sys, camera->view_id);
      float4 proj_params = v->view_data.proj_params;
      float4x4 view = v->view_data.v;
      float4x4 proj = tb_perspective(proj_params[2], proj_params[3], near, far);
      inv_cam_vp = tb_invf44(tb_mulf44f44(proj, view));
    }

    const float cascade_split_lambda = 0.95f;
    float cascade_splits[TB_CASCADE_COUNT] = {0};

    float clip_range = far - near;

    float min_z = near;
    float max_z = near + clip_range;

    float range = max_z - min_z;
    float ratio = max_z / min_z;

    // Calculate split depths based on view camera frustum
    // Based on method presented in
    // https://developer.nvidia.com/gpugems/GPUGems3/gpugems3_ch10.html
    for (uint32_t i = 0; i < TB_CASCADE_COUNT; i++) {
      float p = (i + 1) / (float)TB_CASCADE_COUNT;
      float log = min_z * SDL_powf(ratio, p);
      float uniform = min_z + range * p;
      float d = cascade_split_lambda * (log - uniform) + uniform;
      cascade_splits[i] = (d - near) / clip_range;
    }

    ecs_iter_t light_it = ecs_query_iter(ecs, shadow_sys->dir_light_query);
    while (ecs_query_next(&light_it)) {
      TbDirectionalLightComponent *lights =
          ecs_field(&light_it, TbDirectionalLightComponent, 1);
      const TbTransformComponent *transforms =
          ecs_field(&light_it, TbTransformComponent, 2);
      for (int32_t light_idx = 0; light_idx < light_it.count; ++light_idx) {
        TbDirectionalLightComponent *light = &lights[light_idx];
        const TbTransformComponent *trans = &transforms[light_idx];

        TbTransform transform = trans->transform;

        TbCommonViewData data = {
            .view_pos = transform.position,
        };

        float last_split_dist = 0.0f;
        for (uint32_t cascade_idx = 0; cascade_idx < TB_CASCADE_COUNT;
             ++cascade_idx) {
          float split_dist = cascade_splits[cascade_idx];

          float3 frustum_corners[TB_FRUSTUM_CORNER_COUNT] = {{0}};
          // Project into world space
          for (uint32_t i = 0; i < TB_FRUSTUM_CORNER_COUNT; ++i) {
            const float3 corner = tb_frustum_corners[i];
            float4 inv_corner = tb_mulf44f4(
                inv_cam_vp, (float4){corner[0], corner[1], corner[2], 1.0f});
            frustum_corners[i] = tb_f4tof3(inv_corner) / inv_corner[3];
          }
          for (uint32_t i = 0; i < 4; i++) {
            float3 dist = frustum_corners[i + 4] - frustum_corners[i];
            frustum_corners[i + 4] = frustum_corners[i] + (dist * split_dist);
            frustum_corners[i] = frustum_corners[i] + (dist * last_split_dist);
          }

          // Calculate frustum center
          float3 center = {0};
          for (uint32_t i = 0; i < TB_FRUSTUM_CORNER_COUNT; i++) {
            center += frustum_corners[i];
          }
          center /= (float)TB_FRUSTUM_CORNER_COUNT;

          // Calculate radius
          float radius = 0.0f;
          for (uint32_t i = 0; i < TB_FRUSTUM_CORNER_COUNT; i++) {
            float distance = tb_magf3(frustum_corners[i] - center);
            radius = SDL_max(radius, distance);
          }
          radius = SDL_ceilf(radius * 16.0f) / 16.0f;

          const float3 max = {radius, radius, radius};
          const float3 min = -max;

          // Calculate projection
          float4x4 proj =
              tb_orthographic(min.x, max.x, min.y, max.y, min.z, max.z - min.z);

          // Calc view matrix
          float4x4 view = {.col0 = {0}};
          {
            const float3 forward = tb_transform_get_forward(&transform);

            const float3 offset = center + (forward * min[2]);
            // tb_vlog_location(self->vlog, offset, 1.0f, f3(0, 0, 1));
            view = tb_look_at(offset, center, TB_UP);
          }

          // Calculate view projection matrix
          data.v = view;
          data.p = proj;
          data.vp = tb_mulf44f44(proj, view);

          // Inverse
          data.inv_vp = tb_invf44(data.vp);
          data.inv_proj = tb_invf44(proj);

          TbFrustum frustum = tb_frustum_from_view_proj(&data.vp);

          tb_view_system_set_view_data(
              view_sys, light->cascade_views[cascade_idx], &data);
          tb_view_system_set_view_frustum(
              view_sys, light->cascade_views[cascade_idx], &frustum);

          // Store cascade info
          light->cascade_splits[cascade_idx] =
              (near + split_dist * clip_range) * -1.0f;

          last_split_dist = split_dist;
        }
      }
    }
  }
  TracyCZoneEnd(ctx);
}

void shadow_draw_tick(ecs_iter_t *it) {
  TB_TRACY_SCOPE("Shadow System Draw");
  tb_auto ecs = it->world;

  tb_auto rp_sys = ecs_singleton_get_mut(ecs, TbRenderPipelineSystem);
  tb_auto shadow_sys = ecs_singleton_get_mut(ecs, TbShadowSystem);
  tb_auto mesh_sys = ecs_singleton_get_mut(ecs, TbMeshSystem);
  tb_auto view_sys = ecs_singleton_get_mut(ecs, TbViewSystem);

  // If any shaders aren't ready just bail
  if (!tb_is_shader_ready(ecs, mesh_sys->opaque_shader) ||
      !tb_is_shader_ready(ecs, mesh_sys->transparent_shader) ||
      !tb_is_shader_ready(ecs, mesh_sys->prepass_shader)) {
    return;
  }

  // The shadow batch is just the opaque batch but with a different pipeline
  if (!mesh_sys->opaque_batch) {
    return;
  }

  // For each shadow casting light we want to record shadow draws
  ecs_iter_t light_it = ecs_query_iter(ecs, shadow_sys->dir_light_query);
  while (ecs_query_next(&light_it)) {
    TbDirectionalLightComponent *lights =
        ecs_field(&light_it, TbDirectionalLightComponent, 1);
    for (int32_t light_idx = 0; light_idx < light_it.count; ++light_idx) {
      TB_TRACY_SCOPE("Submit Batches");
      const TbDirectionalLightComponent *light = &lights[light_idx];
      // Submit batch for each shadow cascade
      for (uint32_t cascade_idx = 0; cascade_idx < TB_CASCADE_COUNT;
           ++cascade_idx) {
        tb_auto view_id = light->cascade_views[cascade_idx];

#if TB_USE_DESC_BUFFER == 1
        tb_auto view_addr = tb_view_sys_get_table_addr(ecs, view_id);
        // Skip camera if view set isn't ready
        if (view_addr.address == VK_NULL_HANDLE) {
          continue;
        }
#else
        tb_auto view_set = tb_view_system_get_descriptor(view_sys, view_id);
        // Skip camera if view set isn't ready
        if (view_set == VK_NULL_HANDLE) {
          TracyCZoneEnd(cam_ctx);
          continue;
        }
#endif

        // Must perform the above check before we try to access the opaque batch
        TbDrawBatch shadow_batch = *mesh_sys->opaque_batch;
        tb_auto shadow_prim_batch =
            *(TbPrimitiveBatch *)shadow_batch.user_batch;
        shadow_batch.pipeline = shadow_sys->pipeline;
        shadow_batch.layout = shadow_sys->pipe_layout;
        shadow_batch.user_batch = &shadow_prim_batch;

        tb_auto batch = &shadow_batch;
        tb_auto prim_batch = (TbPrimitiveBatch *)batch->user_batch;

#if TB_USE_DESC_BUFFER == 1
        prim_batch->view_addr = view_addr;
#else
        prim_batch->view_set = view_set;
#endif
        const float dim = TB_SHADOW_MAP_DIM;
        batch->viewport = (VkViewport){0, 0, dim, dim, 0, 1};
        batch->scissor = (VkRect2D){{0, 0}, {dim, dim}};

        tb_render_pipeline_issue_draw_batch(
            rp_sys, shadow_sys->draw_ctxs[cascade_idx], 1, batch);
      }
    }
  }

  // The opaque batch has been consumed and invalidated
  mesh_sys->opaque_batch = NULL;
}

void tb_register_shadow_sys(TbWorld *world) {
  TracyCZoneN(ctx, "Register Shadow Sys", true);
  ecs_world_t *ecs = world->ecs;

  ECS_COMPONENT_DEFINE(ecs, TbShadowSystem);

  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  tb_auto rp_sys = ecs_singleton_get_mut(ecs, TbRenderPipelineSystem);
  tb_auto mesh_sys = ecs_singleton_get_mut(ecs, TbMeshSystem);
  tb_auto view_sys = ecs_singleton_get_mut(ecs, TbViewSystem);
  (void)view_sys;

  TbShadowSystem sys = {
      .gp_alloc = world->gp_alloc,
      .tmp_alloc = world->tmp_alloc,
      .dir_light_query =
          ecs_query(ecs, {.filter.terms =
                              {
                                  {.id = ecs_id(TbDirectionalLightComponent)},
                                  {.id = ecs_id(TbTransformComponent)},
                              }}),
  };
  // Need a draw context per cascade pass
  for (uint32_t i = 0; i < TB_CASCADE_COUNT; ++i) {
    sys.draw_ctxs[i] = tb_render_pipeline_register_draw_context(
        rp_sys, &(TbDrawContextDescriptor){
                    .batch_size = sizeof(TbPrimitiveBatch),
                    .draw_fn = shadow_pass_record,
                    .pass_id = rp_sys->shadow_passes[i],
                });
  }

  {
    VkResult err = VK_SUCCESS;
    // Create pipeline layout
    {
      tb_auto mesh_set_layout = tb_mesh_sys_get_set_layout(ecs);

      VkPipelineLayoutCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
          .setLayoutCount = 5,
          .pSetLayouts =
              (VkDescriptorSetLayout[5]){
                  tb_view_sys_get_set_layout(ecs),
                  mesh_sys->draw_set_layout,
                  tb_render_object_sys_get_set_layout(ecs),
                  mesh_set_layout,
                  mesh_set_layout,
              },
      };
      err = tb_rnd_create_pipeline_layout(
          rnd_sys, &create_info, "Shadow Pipeline Layout", &sys.pipe_layout);
      TB_VK_CHECK(err, "Failed to create shadow pipeline layout");
    }

    {
      uint32_t attach_count = 0;
      tb_render_pipeline_get_attachments(rp_sys, rp_sys->shadow_passes[0],
                                         &attach_count, NULL);
      TB_CHECK(attach_count == 1, "Unexpected");
      TbPassAttachment depth_info = {0};
      tb_render_pipeline_get_attachments(rp_sys, rp_sys->shadow_passes[0],
                                         &attach_count, &depth_info);

      VkFormat depth_format =
          tb_render_target_get_format(rp_sys->rt_sys, depth_info.attachment);
      err = create_shadow_pipeline(rnd_sys, depth_format, sys.pipe_layout,
                                   &sys.pipeline);
      TB_VK_CHECK(err, "Failed to create shadow pipeline");
    }
  }

  // Sets a singleton by ptr
  ecs_set_ptr(ecs, ecs_id(TbShadowSystem), TbShadowSystem, &sys);

  ECS_SYSTEM(ecs, shadow_update_tick, EcsOnUpdate, TbCameraComponent);
  ECS_SYSTEM(ecs, shadow_draw_tick, EcsOnStore, TbShadowSystem(TbShadowSystem));

  TracyCZoneEnd(ctx);
}

void tb_unregister_shadow_sys(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;

  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  tb_auto sys = ecs_singleton_get_mut(ecs, TbShadowSystem);

  tb_rnd_destroy_pipeline(rnd_sys, sys->pipeline);
  tb_rnd_destroy_pipe_layout(rnd_sys, sys->pipe_layout);

  ecs_query_fini(sys->dir_light_query);
  *sys = (TbShadowSystem){0};

  ecs_singleton_remove(ecs, TbShadowSystem);
}
