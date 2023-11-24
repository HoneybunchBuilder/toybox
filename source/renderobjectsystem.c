#include "renderobjectsystem.h"

#include "common.hlsli"
#include "meshcomponent.h"
#include "profiling.h"
#include "rendersystem.h"
#include "tbcommon.h"
#include "transformcomponent.h"

#include <flecs.h>

RenderObjectSystem create_render_object_system(Allocator std_alloc,
                                               Allocator tmp_alloc,
                                               RenderSystem *rnd_sys) {
  RenderObjectSystem sys = (RenderObjectSystem){
      .render_system = rnd_sys,
      .tmp_alloc = tmp_alloc,
      .std_alloc = std_alloc,
  };

  VkResult err = VK_SUCCESS;

  // Create render object descriptor set layout
  {
    VkDescriptorSetLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings =
            &(VkDescriptorSetLayoutBinding){
                .binding = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            },
    };
    err = tb_rnd_create_set_layout(
        rnd_sys, &create_info, "Object Descriptor Set Layout", &sys.set_layout);
    TB_VK_CHECK(err, "Failed to create render object descriptor set");
  }

  return sys;
}

void destroy_render_object_system(RenderObjectSystem *self) {
  tb_rnd_destroy_set_layout(self->render_system, self->set_layout);

  *self = (RenderObjectSystem){0};
}

void tick_render_object_system(ecs_iter_t *it) {
  TracyCZoneNC(ctx, "Render Object System", TracyCategoryColorCore, true);
  ecs_world_t *ecs = it->world;
  ECS_COMPONENT(ecs, RenderSystem);
  ECS_COMPONENT(ecs, RenderObjectSystem);
  ECS_COMPONENT(ecs, RenderObject);

  RenderSystem *rnd_sys = ecs_singleton_get_mut(ecs, RenderSystem);
  RenderObjectSystem *ro_sys = ecs_singleton_get_mut(ecs, RenderObjectSystem);

  TransformsBuffer *trans_buffer = &ro_sys->trans_buffers[rnd_sys->frame_idx];

  int32_t prev_count = trans_buffer->obj_count;
  int32_t obj_count = 0;
  // Find object count by running query
  ecs_iter_t obj_it = ecs_query_iter(ecs, ro_sys->obj_query);
  while (ecs_query_next(&obj_it)) {
    obj_count += obj_it.count;
  }
  trans_buffer->obj_count = obj_count;

  if (obj_count == 0) {
    TracyCZoneEnd(ctx);
    return;
  }

  obj_it = ecs_query_iter(ecs, ro_sys->obj_query); // reset query

  float4x4 *trans_ptr = NULL;
  if (obj_count > prev_count) {
    // We need to resize the GPU buffer
    tb_rnd_free_gpu_buffer(ro_sys->render_system, &trans_buffer->gpu);
    VkBufferCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(float4x4) * obj_count,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
    };
    tb_rnd_sys_create_gpu_buffer(rnd_sys, &create_info, "Transform Buffer",
                                 &trans_buffer->gpu, &trans_buffer->host,
                                 (void **)&trans_ptr);
  } else {
    tb_rnd_sys_update_gpu_buffer(rnd_sys, &trans_buffer->gpu,
                                 &trans_buffer->host, (void **)&trans_ptr);
  }

  {
    uint32_t obj_idx = 0;
    while (ecs_query_next(&obj_it)) {
      TransformComponent *trans_comps =
          ecs_field(&obj_it, TransformComponent, 1);
      RenderObject *rnd_objs = ecs_field(&obj_it, RenderObject, 2);
      for (int32_t i = 0; i < obj_it.count; ++i) {
        // TODO: We want to only have to do this when transforms are dirty
        // but we need to triple buffer the transform buffers to avoid
        // stomping the transform and when a transform is dirty *all* transform
        // buffers need that new data.
        // Maybe we need some kind of queueing procedure?
        trans_ptr[obj_idx] =
            tb_transform_get_world_matrix(ecs, &trans_comps[i]);
        rnd_objs[i].index = obj_idx;
        obj_idx++;
      }
    }
  }

  // We can optimize this later but for now just always update this descriptor
  // set every frame
  {
    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .poolSizeCount = 1,
        .pPoolSizes = (VkDescriptorPoolSize[1]){{
            .descriptorCount = 4,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        }},
        .maxSets = 4,
    };
    tb_rnd_frame_desc_pool_tick(rnd_sys, &pool_info, &ro_sys->set_layout,
                                ro_sys->pools, 1);

    if (trans_buffer->obj_count > 0) {
      VkWriteDescriptorSet write = {
          .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          .descriptorCount = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .dstSet = tb_render_object_sys_get_set(ro_sys),
          .pBufferInfo =
              &(VkDescriptorBufferInfo){
                  .buffer = trans_buffer->gpu.buffer,
                  .offset = 0,
                  .range = trans_buffer->gpu.info.size,
              },
      };
      tb_rnd_update_descriptors(rnd_sys, 1, &write);
    }
  }
  TracyCZoneEnd(ctx);
}

VkDescriptorSet tb_render_object_sys_get_set(RenderObjectSystem *sys) {
  return tb_rnd_frame_desc_pool_get_set(sys->render_system, sys->pools, 0);
}

void tb_register_render_object_sys(ecs_world_t *ecs, Allocator std_alloc,
                                   Allocator tmp_alloc) {
  ECS_COMPONENT(ecs, RenderSystem);
  ECS_COMPONENT(ecs, RenderObjectSystem);
  ECS_COMPONENT(ecs, RenderObject);
  ECS_COMPONENT(ecs, TransformComponent);
  RenderSystem *rnd_sys = ecs_singleton_get_mut(ecs, RenderSystem);
  RenderObjectSystem sys =
      create_render_object_system(std_alloc, tmp_alloc, rnd_sys);
  sys.obj_query =
      ecs_query(ecs, {
                         .filter.terms =
                             {
                                 {
                                     .id = ecs_id(TransformComponent),
                                 },
                                 {
                                     .id = ecs_id(RenderObject),
                                 },
                             },
                     });
  // Sets a singleton based on the value at a pointer
  ecs_set_ptr(ecs, ecs_id(RenderObjectSystem), RenderObjectSystem, &sys);

  // Register a tick function
  ECS_SYSTEM(ecs, tick_render_object_system, EcsOnUpdate,
             RenderObjectSystem(RenderObjectSystem));
}

void tb_unregister_render_object_sys(ecs_world_t *ecs) {
  ECS_COMPONENT(ecs, RenderObjectSystem);
  RenderObjectSystem *sys = ecs_singleton_get_mut(ecs, RenderObjectSystem);
  ecs_query_fini(sys->obj_query);
  destroy_render_object_system(sys);
  ecs_singleton_remove(ecs, RenderObjectSystem);
}
