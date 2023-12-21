#include "shadowsystem.h"

#include "cameracomponent.h"
#include "lightcomponent.h"
#include "materialsystem.h"
#include "meshcomponent.h"
#include "meshsystem.h"
#include "profiling.h"
#include "renderobjectsystem.h"
#include "renderpipelinesystem.h"
#include "rendertargetsystem.h"
#include "tbcommon.h"
#include "transformcomponent.h"
#include "viewsystem.h"
#include "visualloggingsystem.h"
#include "world.h"

#include <flecs.h>

// Ignore some warnings for the generated headers
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-variable-declarations"
// NOLINTBEGIN
#include "depth_frag.h"
#include "depth_vert.h"
// NOLINTEND
#pragma clang diagnostic pop

VkResult create_shadow_pipeline(TbRenderSystem *rnd_sys,
                                VkFormat depth_format,
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
    err = tb_rnd_create_shader(rnd_sys, &create_info, "Shadow Vert",
                               &vert_mod);
    TB_VK_CHECK_RET(err, "Failed to load shadow vert shader module", err);

    create_info.codeSize = sizeof(depth_frag);
    create_info.pCode = (const uint32_t *)depth_frag;
    err = tb_rnd_create_shader(rnd_sys, &create_info, "Shadow Frag",
                               &frag_mod);
    TB_VK_CHECK_RET(err, "Failed to load shadow frag shader module", err);
  }

  VkGraphicsPipelineCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
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
    VkDescriptorSet sets[set_count] = {
        prim_batch->view_set, prim_batch->draw_set, prim_batch->obj_set,
        prim_batch->idx_set, prim_batch->pos_set};
    vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0,
                            set_count, sets, 0, NULL);
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
  ECS_COMPONENT(ecs, TbShadowSystem);
  ECS_COMPONENT(ecs, TbViewSystem);

  TbViewSystem *view_sys = ecs_singleton_get_mut(ecs, TbViewSystem);
  ecs_singleton_modified(ecs, TbViewSystem);
  TbShadowSystem *shadow_sys = ecs_singleton_get_mut(ecs, TbShadowSystem);
  ecs_singleton_modified(ecs, TbShadowSystem);

  // For each camera, evaluate each light and calculate any necessary shadow
  // info
  const TbCameraComponent *cameras = ecs_field(it, TbCameraComponent, 1);
  for (int32_t cam_idx = 0; cam_idx < it->count; ++cam_idx) {
    const TbCameraComponent *camera = &cameras[cam_idx];

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
  TracyCZoneNC(ctx, "Shadow System Draw", TracyCategoryColorCore, true);
  tb_auto *ecs = it->world;
  ECS_COMPONENT(ecs, TbRenderPipelineSystem);
  ECS_COMPONENT(ecs, TbRenderObjectSystem);
  ECS_COMPONENT(ecs, TbRenderSystem);
  ECS_COMPONENT(ecs, TbShadowSystem);
  ECS_COMPONENT(ecs, TbMaterialSystem);
  ECS_COMPONENT(ecs, TbMeshSystem);
  ECS_COMPONENT(ecs, TbViewSystem);
  ECS_COMPONENT(ecs, TbRenderObject);

  tb_auto *rp_sys = ecs_singleton_get_mut(ecs, TbRenderPipelineSystem);
  tb_auto *shadow_sys = ecs_singleton_get_mut(ecs, TbShadowSystem);
  tb_auto *mesh_sys = ecs_singleton_get_mut(ecs, TbMeshSystem);
  tb_auto *view_sys = ecs_singleton_get_mut(ecs, TbViewSystem);

  // The shadow batch is just the opaque batch but with a different pipeline
  if (!mesh_sys->opaque_batch) {
    TracyCZoneEnd(ctx);
    return;
  }
  TbDrawBatch shadow_batch = *mesh_sys->opaque_batch;
  tb_auto shadow_prim_batch = *(TbPrimitiveBatch *)shadow_batch.user_batch;
  shadow_batch.pipeline = shadow_sys->pipeline;
  shadow_batch.layout = shadow_sys->pipe_layout;
  shadow_batch.user_batch = &shadow_prim_batch;

  // For each shadow casting light we want to record shadow draws
  ecs_iter_t light_it = ecs_query_iter(ecs, shadow_sys->dir_light_query);
  while (ecs_query_next(&light_it)) {
    TbDirectionalLightComponent *lights =
        ecs_field(&light_it, TbDirectionalLightComponent, 1);
    for (int32_t light_idx = 0; light_idx < light_it.count; ++light_idx) {
      const TbDirectionalLightComponent *light = &lights[light_idx];

      // Submit batch for each shadow cascade
      TracyCZoneN(ctx2, "Submit Batches", true);
      for (uint32_t cascade_idx = 0; cascade_idx < TB_CASCADE_COUNT;
           ++cascade_idx) {

        tb_auto view_set = tb_view_system_get_descriptor(
            view_sys, light->cascade_views[cascade_idx]);

        tb_auto *batch = &shadow_batch;
        tb_auto *prim_batch = (TbPrimitiveBatch *)batch->user_batch;

        prim_batch->view_set = view_set;
        const float dim = TB_SHADOW_MAP_DIM;
        batch->viewport = (VkViewport){0, 0, dim, dim, 0, 1};
        batch->scissor = (VkRect2D){{0, 0}, {dim, dim}};

        tb_render_pipeline_issue_draw_batch(
            rp_sys, shadow_sys->draw_ctxs[cascade_idx], 1, batch);
      }
      TracyCZoneEnd(ctx2);
    }
  }

  TracyCZoneEnd(ctx);
}

void tb_register_shadow_sys(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;
  ECS_COMPONENT(ecs, TbViewSystem);
  ECS_COMPONENT(ecs, TbShadowSystem);
  ECS_COMPONENT(ecs, TbDirectionalLightComponent);
  ECS_COMPONENT(ecs, TbTransformComponent);
  ECS_COMPONENT(ecs, TbRenderSystem);
  ECS_COMPONENT(ecs, TbRenderPipelineSystem);
  ECS_COMPONENT(ecs, TbRenderObjectSystem);
  ECS_COMPONENT(ecs, TbMeshSystem);

  tb_auto *rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  tb_auto *rp_sys = ecs_singleton_get_mut(ecs, TbRenderPipelineSystem);
  tb_auto *ro_sys = ecs_singleton_get_mut(ecs, TbRenderObjectSystem);
  tb_auto *mesh_sys = ecs_singleton_get_mut(ecs, TbMeshSystem);
  tb_auto *view_sys = ecs_singleton_get_mut(ecs, TbViewSystem);

  TbShadowSystem sys = {
      .std_alloc = world->std_alloc,
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
      VkPipelineLayoutCreateInfo create_info = {
          .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
          .setLayoutCount = 5,
          .pSetLayouts =
              (VkDescriptorSetLayout[5]){
                  view_sys->set_layout,
                  mesh_sys->draw_set_layout,
                  ro_sys->set_layout,
                  mesh_sys->mesh_set_layout,
                  mesh_sys->mesh_set_layout,
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

      VkFormat depth_format = tb_render_target_get_format(
          rp_sys->rt_sys, depth_info.attachment);
      err = create_shadow_pipeline(rnd_sys, depth_format, sys.pipe_layout,
                                   &sys.pipeline);
      TB_VK_CHECK(err, "Failed to create shadow pipeline");
    }
  }

  // Sets a singleton by ptr
  ecs_set_ptr(ecs, ecs_id(TbShadowSystem), TbShadowSystem, &sys);

  ECS_SYSTEM(ecs, shadow_update_tick, EcsOnUpdate, TbCameraComponent);

  ECS_SYSTEM(ecs, shadow_draw_tick, EcsOnUpdate,
             TbShadowSystem(TbShadowSystem));
}

void tb_unregister_shadow_sys(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;
  ECS_COMPONENT(ecs, TbRenderSystem);
  ECS_COMPONENT(ecs, TbShadowSystem);

  TbRenderSystem *rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);

  TbShadowSystem *sys = ecs_singleton_get_mut(ecs, TbShadowSystem);
  tb_rnd_destroy_pipeline(rnd_sys, sys->pipeline);
  tb_rnd_destroy_pipe_layout(rnd_sys, sys->pipe_layout);
  ecs_query_fini(sys->dir_light_query);
  *sys = (TbShadowSystem){0};

  ecs_singleton_remove(ecs, TbShadowSystem);
}
