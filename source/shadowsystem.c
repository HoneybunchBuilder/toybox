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
#include "shadow.hlsli"
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

VkResult create_shadow_pipeline(TbRenderSystem *render_system,
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
    err = tb_rnd_create_shader(render_system, &create_info, "Shadow Vert",
                               &vert_mod);
    TB_VK_CHECK_RET(err, "Failed to load shadow vert shader module", err);

    create_info.codeSize = sizeof(depth_frag);
    create_info.pCode = (const uint32_t *)depth_frag;
    err = tb_rnd_create_shader(render_system, &create_info, "Shadow Frag",
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
  err = tb_rnd_create_graphics_pipelines(render_system, 1, &create_info,
                                         "Shadow Pipeline", pipeline);
  TB_VK_CHECK_RET(err, "Failed to create shadow pipeline", err);

  tb_rnd_destroy_shader(render_system, vert_mod);
  tb_rnd_destroy_shader(render_system, frag_mod);

  return err;
}

void shadow_pass_record(TracyCGPUContext *gpu_ctx, VkCommandBuffer buffer,
                        uint32_t batch_count, const TbDrawBatch *batches) {
  TracyCZoneNC(ctx, "Shadow Record", TracyCategoryColorRendering, true);
  TracyCVkNamedZone(gpu_ctx, frame_scope, buffer, "Shadows", 3, true);

  for (uint32_t batch_idx = 0; batch_idx < batch_count; ++batch_idx) {
    const TbDrawBatch *batch = &batches[batch_idx];
    const TbPrimitiveBatch *prim_batch =
        (const TbPrimitiveBatch *)batch->user_batch;
    if (batch->draw_count == 0) {
      continue;
    }

    TracyCZoneNC(batch_ctx, "Shadow Batch", TracyCategoryColorRendering, true);
    cmd_begin_label(buffer, "Shadow Batch", (float4){0.8f, 0.0f, 0.4f, 1.0f});

    VkPipelineLayout layout = batch->layout;
    vkCmdBindPipeline(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, batch->pipeline);

    vkCmdSetViewport(buffer, 0, 1, &batch->viewport);
    vkCmdSetScissor(buffer, 0, 1, &batch->scissor);

    VkDescriptorSet sets[4] = {prim_batch->inst_set, prim_batch->view_set,
                               prim_batch->trans_set, prim_batch->mesh_set};
    vkCmdBindDescriptorSets(buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0,
                            4, sets, 0, NULL);
    for (uint32_t draw_idx = 0; draw_idx < batch->draw_count; ++draw_idx) {
      const TbPrimitiveDraw *draw =
          &((const TbPrimitiveDraw *)batch->draws)[draw_idx];
      if (draw->instance_count == 0) {
        continue;
      }

      TracyCZoneNC(draw_ctx, "Submesh Draw", TracyCategoryColorRendering, true);
      cmd_begin_label(buffer, "Submesh Draw", (float4){0.6f, 0.0f, 0.3f, 1.0f});

      if (draw->index_count > 0) {
        // Don't need to bind material data
        vkCmdBindIndexBuffer(buffer, prim_batch->geom_buffer,
                             draw->index_offset, draw->index_type);
        vkCmdDrawIndexed(buffer, draw->index_count, draw->instance_count, 0,
                         draw->vertex_offset, 0);
      }

      cmd_end_label(buffer);
      TracyCZoneEnd(draw_ctx);
    }

    cmd_end_label(buffer);
    TracyCZoneEnd(batch_ctx);
  }

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
  ecs_world_t *ecs = it->world;
  ECS_COMPONENT(ecs, TbRenderPipelineSystem);
  ECS_COMPONENT(ecs, TbRenderObjectSystem);
  ECS_COMPONENT(ecs, TbRenderSystem);
  ECS_COMPONENT(ecs, TbShadowSystem);
  ECS_COMPONENT(ecs, TbMaterialSystem);
  ECS_COMPONENT(ecs, TbMeshSystem);
  ECS_COMPONENT(ecs, TbViewSystem);
  ECS_COMPONENT(ecs, TbRenderObject);

  TbRenderPipelineSystem *rp_sys =
      ecs_singleton_get_mut(ecs, TbRenderPipelineSystem);
  TbRenderObjectSystem *ro_sys =
      ecs_singleton_get_mut(ecs, TbRenderObjectSystem);
  TbRenderSystem *rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  TbShadowSystem *shadow_sys = ecs_singleton_get_mut(ecs, TbShadowSystem);
  TbMaterialSystem *mat_sys = ecs_singleton_get_mut(ecs, TbMaterialSystem);
  TbMeshSystem *mesh_sys = ecs_singleton_get_mut(ecs, TbMeshSystem);
  TbViewSystem *view_sys = ecs_singleton_get_mut(ecs, TbViewSystem);

  TbAllocator tmp_alloc = shadow_sys->tmp_alloc;

  // Count how many meshes are in the scene
  ecs_iter_t mesh_it = ecs_query_iter(ecs, mesh_sys->mesh_query);
  uint32_t mesh_count = 0;
  while (ecs_query_next(&mesh_it)) {
    TbMeshComponent *meshes = ecs_field(&mesh_it, TbMeshComponent, 1);
    for (int32_t mesh_idx = 0; mesh_idx < mesh_it.count; ++mesh_idx) {
      TbMeshComponent *mesh = &meshes[mesh_idx];

      uint32_t ent_count = TB_DYN_ARR_SIZE(mesh->entities);

      for (uint32_t submesh_idx = 0; submesh_idx < mesh->submesh_count;
           ++submesh_idx) {
        TbSubMesh *sm = &mesh->submeshes[submesh_idx];
        TbMaterialId mat = sm->material;
        TbMaterialPerm perm = tb_mat_system_get_perm(mat_sys, mat);

        bool trans =
            perm & GLTF_PERM_ALPHA_CLIP || perm & GLTF_PERM_ALPHA_BLEND;
        if (!trans) {
          mesh_count += mesh->submesh_count * ent_count;
        }
      }
    }
  }
  mesh_it = ecs_query_iter(ecs, mesh_sys->mesh_query);
  if (!mesh_count) {
    TracyCZoneEnd(ctx);
    return;
  }

  DrawBatchList batches = {0};
  TbPrimitiveBatchList prim_batches = {0};
  TB_DYN_ARR_RESET(batches, tmp_alloc, mesh_count);
  TB_DYN_ARR_RESET(prim_batches, tmp_alloc, mesh_count);
  TbPrimIndirectList prim_trans = {0};
  TB_DYN_ARR_RESET(prim_trans, tmp_alloc, mesh_count);

  TracyCZoneN(ctx2, "Iterate Meshes", true);
  while (ecs_query_next(&mesh_it)) {
    TbMeshComponent *meshes = ecs_field(&mesh_it, TbMeshComponent, 1);
    for (int32_t mesh_idx = 0; mesh_idx < mesh_it.count; ++mesh_idx) {
      TbMeshComponent *mesh = &meshes[mesh_idx];

      VkBuffer geom_buffer =
          tb_mesh_system_get_gpu_mesh(mesh_sys, mesh->mesh_id);
      VkDescriptorSet mesh_set =
          tb_mesh_system_get_set(mesh_sys, mesh->mesh_id);
      VkDescriptorSet transforms_set = tb_render_object_sys_get_set(ro_sys);

      for (uint32_t submesh_idx = 0; submesh_idx < mesh->submesh_count;
           ++submesh_idx) {
        TbSubMesh *sm = &mesh->submeshes[submesh_idx];
        TbMaterialId mat = sm->material;

        // Deduce some important details from the submesh
        TbMaterialPerm perm = tb_mat_system_get_perm(mat_sys, mat);
        if (perm & GLTF_PERM_ALPHA_CLIP || perm & GLTF_PERM_ALPHA_BLEND) {
          continue;
        }

        const uint32_t index_count = sm->index_count;
        const uint64_t index_offset = sm->index_offset;
        const uint32_t vertex_offset = sm->vertex_offset;
        const VkIndexType index_type = (VkIndexType)sm->index_type;

        // Handle Opaque and Transparent draws
        {
          VkPipelineLayout layout = shadow_sys->pipe_layout;
          VkPipeline pipeline = shadow_sys->pipeline;

          // Determine if we need to insert a new batch
          TbDrawBatch *batch = NULL;
          TbPrimitiveBatch *prim_batch = NULL;
          TbIndirectionList *transforms = NULL;
          {
            // Try to find an existing suitable batch
            TB_DYN_ARR_FOREACH(batches, i) {
              TbDrawBatch *db = &TB_DYN_ARR_AT(batches, i);
              TbPrimitiveBatch *pb = &TB_DYN_ARR_AT(prim_batches, i);
              if (db->pipeline == pipeline && db->layout == layout &&
                  pb->perm == perm && pb->geom_buffer == geom_buffer &&
                  pb->mesh_set == mesh_set) {
                batch = db;
                prim_batch = pb;
                transforms = &TB_DYN_ARR_AT(prim_trans, i);
                break;
              }
            }
            // No batch was found, create one
            if (batch == NULL) {
              // Worst case batch count is one batch having to carry every
              // mesh with the maximum number of possible submeshes
              const uint32_t max_draw_count = mesh_count;
              TbDrawBatch db = {
                  .pipeline = pipeline,
                  .layout = layout,
                  .draw_size = sizeof(TbPrimitiveDraw),
                  .draws = tb_alloc_nm_tp(tmp_alloc, max_draw_count,
                                          TbPrimitiveDraw),
              };
              TbPrimitiveBatch pb = {
                  .perm = perm,
                  .trans_set = transforms_set,
                  .mesh_set = mesh_set,
                  .geom_buffer = geom_buffer,
              };

              TbIndirectionList il = {0};
              TB_DYN_ARR_RESET(il, tmp_alloc, max_draw_count);

              // Append it to the list and make sure we get a reference
              uint32_t idx = TB_DYN_ARR_SIZE(prim_batches);
              TB_DYN_ARR_APPEND(prim_batches, pb);
              prim_batch = &TB_DYN_ARR_AT(prim_batches, idx);
              TB_DYN_ARR_APPEND(batches, db);
              batch = &TB_DYN_ARR_AT(batches, idx);
              TB_DYN_ARR_APPEND(prim_trans, il);
              transforms = &TB_DYN_ARR_AT(prim_trans, idx);

              batch->user_batch = prim_batch;
            }
          }

          // Determine if we need to insert a new draw
          {
            TbPrimitiveDraw *draw = NULL;
            for (uint32_t i = 0; i < batch->draw_count; ++i) {
              TbPrimitiveDraw *d = &((TbPrimitiveDraw *)batch->draws)[i];
              if (d->index_count == index_count &&
                  d->index_offset == index_offset &&
                  d->vertex_offset == vertex_offset &&
                  d->index_type == index_type) {
                draw = d;
                break;
              }
            }
            // No draw was found, create one
            if (draw == NULL) {
              TbPrimitiveDraw d = {
                  .index_count = index_count,
                  .index_offset = index_offset,
                  .vertex_offset = vertex_offset,
                  .index_type = index_type,
              };
              // Append it to the list and make sure we get a reference
              uint32_t idx = batch->draw_count++;
              ((TbPrimitiveDraw *)batch->draws)[idx] = d;
              draw = &((TbPrimitiveDraw *)batch->draws)[idx];
            }

            draw->instance_count += TB_DYN_ARR_SIZE(mesh->entities);

            // Append every render object's transform index to the list
            TB_DYN_ARR_FOREACH(mesh->entities, e_idx) {
              ecs_entity_t entity = TB_DYN_ARR_AT(mesh->entities, e_idx);
              const TbRenderObject *ro = ecs_get(ecs, entity, TbRenderObject);
              TB_DYN_ARR_APPEND(*transforms, ro->index);
            }
          }
        }
      }
    }
  }
  TracyCZoneEnd(ctx2);

  // Write transform lists to the GPU temp buffer
  TB_DYN_ARR_OF(uint64_t) inst_buffers = {0};
  {
    TracyCZoneN(ctx2, "Gather Transforms", true);
    const uint32_t count = TB_DYN_ARR_SIZE(prim_trans);
    if (count) {
      TB_DYN_ARR_RESET(inst_buffers, tmp_alloc, count);

      TB_DYN_ARR_FOREACH(prim_trans, i) {
        TbIndirectionList *transforms = &TB_DYN_ARR_AT(prim_trans, i);

        const size_t trans_size =
            sizeof(int32_t) * TB_DYN_ARR_SIZE(*transforms);

        uint64_t offset = 0;
        tb_rnd_sys_tmp_buffer_copy(rnd_sys, trans_size, 0x40, transforms->data,
                                   &offset);
        TB_DYN_ARR_APPEND(inst_buffers, offset);
      }
    }
    TracyCZoneEnd(ctx2);
  }

  // Alloc and write transform descriptor sets
  {
    TracyCZoneN(ctx2, "Write Descriptors", true);
    const uint32_t set_count = TB_DYN_ARR_SIZE(inst_buffers);
    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = set_count * 4,
        .poolSizeCount = 1,
        .pPoolSizes =
            (VkDescriptorPoolSize[1]){
                {
                    .descriptorCount = set_count * 4,
                    .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                },
            },
    };
    VkDescriptorSetLayout *layouts =
        tb_alloc_nm_tp(tmp_alloc, set_count, VkDescriptorSetLayout);
    for (uint32_t i = 0; i < set_count; ++i) {
      layouts[i] = ro_sys->set_layout;
    }
    VkResult err = tb_rnd_frame_desc_pool_tick(rnd_sys, &pool_info, layouts,
                                               shadow_sys->desc_pool_list.pools,
                                               set_count);
    TB_VK_CHECK(err, "Failed to update descriptor pool");

    VkBuffer gpu_buf = tb_rnd_get_gpu_tmp_buffer(rnd_sys);

    // Get and write a buffer to each descriptor set
    VkWriteDescriptorSet *writes =
        tb_alloc_nm_tp(tmp_alloc, set_count, VkWriteDescriptorSet);

    uint32_t set_idx = 0;
    TB_DYN_ARR_FOREACH(inst_buffers, i) {
      VkDescriptorSet set = tb_rnd_frame_desc_pool_get_set(
          rnd_sys, shadow_sys->desc_pool_list.pools, set_idx);
      const uint64_t offset = TB_DYN_ARR_AT(inst_buffers, i);
      TbIndirectionList *transforms = &TB_DYN_ARR_AT(prim_trans, i);
      const uint64_t trans_count = TB_DYN_ARR_SIZE(*transforms);

      VkDescriptorBufferInfo *buffer_info =
          tb_alloc_tp(tmp_alloc, VkDescriptorBufferInfo);
      *buffer_info = (VkDescriptorBufferInfo){
          .buffer = gpu_buf,
          .offset = offset,
          .range = sizeof(int32_t) * trans_count,
      };

      writes[set_idx++] = (VkWriteDescriptorSet){
          .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          .dstSet = set,
          .dstBinding = 0,
          .dstArrayElement = 0,
          .descriptorCount = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .pBufferInfo = buffer_info,
      };

      // Need to make sure the batch is also using the correct sets
      TB_DYN_ARR_AT(prim_batches, i).inst_set = set;
    }
    tb_rnd_update_descriptors(rnd_sys, set_count, writes);
    TracyCZoneEnd(ctx2);
  }

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

        VkDescriptorSet view_set = tb_view_system_get_descriptor(
            view_sys, light->cascade_views[cascade_idx]);

        TB_DYN_ARR_FOREACH(batches, i) {
          TbDrawBatch *batch = &TB_DYN_ARR_AT(batches, i);
          TbPrimitiveBatch *prim_batch = (TbPrimitiveBatch *)batch->user_batch;
          prim_batch->view_set = view_set;

          const float dim = TB_SHADOW_MAP_DIM;
          batch->viewport = (VkViewport){0, 0, dim, dim, 0, 1};
          batch->scissor = (VkRect2D){{0, 0}, {dim, dim}};
        }

        tb_render_pipeline_issue_draw_batch(
            rp_sys, shadow_sys->draw_ctxs[cascade_idx],
            TB_DYN_ARR_SIZE(batches), batches.data);
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

  TbRenderSystem *rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  TbRenderPipelineSystem *rp_sys =
      ecs_singleton_get_mut(ecs, TbRenderPipelineSystem);
  TbRenderObjectSystem *ro_sys =
      ecs_singleton_get_mut(ecs, TbRenderObjectSystem);
  TbMeshSystem *mesh_sys = ecs_singleton_get_mut(ecs, TbMeshSystem);
  TbViewSystem *view_sys = ecs_singleton_get_mut(ecs, TbViewSystem);

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
          .setLayoutCount = 4,
          .pSetLayouts =
              (VkDescriptorSetLayout[4]){
                  mesh_sys->obj_set_layout,
                  view_sys->set_layout,
                  ro_sys->set_layout,
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
          rp_sys->render_target_system, depth_info.attachment);
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
