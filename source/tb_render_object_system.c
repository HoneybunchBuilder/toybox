#include "tb_render_object_system.h"

#include "common.hlsli"
#include "tb_common.h"
#include "tb_mesh_component.h"
#include "tb_profiling.h"
#include "tb_render_system.h"
#include "tb_transform_component.h"
#include "tb_world.h"

#include "blocks/Block.h"

void tb_register_render_object_sys(TbWorld *world);
void tb_unregister_render_object_sys(TbWorld *world);

TB_REGISTER_SYS(tb, render_object, TB_RND_OBJ_SYS_PRIO)

ECS_COMPONENT_DECLARE(TbRenderObject);
ECS_COMPONENT_DECLARE(TbRenderObjectSystem);

TbRenderObjectSystem create_render_object_system(TbAllocator gp_alloc,
                                                 TbAllocator tmp_alloc,
                                                 TbRenderSystem *rnd_sys) {
  tb_auto sys = (TbRenderObjectSystem){
      .rnd_sys = rnd_sys,
      .tmp_alloc = tmp_alloc,
      .gp_alloc = gp_alloc,
  };

  {
    const VkDescriptorBindingFlags flags =
        VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT |
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
    const uint32_t binding_count = 1;
    VkDescriptorSetLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT,
        .pNext =
            &(VkDescriptorSetLayoutBindingFlagsCreateInfo){
                .sType =
                    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
                .bindingCount = binding_count,
                .pBindingFlags =
                    (VkDescriptorBindingFlags[binding_count]){flags},
            },
        .bindingCount = binding_count,
        .pBindings =
            (VkDescriptorSetLayoutBinding[binding_count]){
                {
                    .binding = 0,
                    .descriptorCount = 2048, // HACK: High upper bound
                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    .stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                                  VK_SHADER_STAGE_FRAGMENT_BIT,
                },
            },
    };
    tb_rnd_create_set_layout(rnd_sys, &create_info,
                             "Render Object Desc Buffer Layout",
                             &sys.set_layout2);
    tb_create_descriptor_buffer(rnd_sys, sys.set_layout2,
                                "Render Object Descriptors", 4,
                                &sys.desc_buffer);
  }

  {
    const VkDescriptorBindingFlags flags =
        VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT |
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
    tb_rnd_create_set_layout(
        rnd_sys,
        &(VkDescriptorSetLayoutCreateInfo){
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext =
                &(VkDescriptorSetLayoutBindingFlagsCreateInfo){
                    .sType =
                        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
                    .bindingCount = 1,
                    .pBindingFlags = (VkDescriptorBindingFlags[1]){flags},
                },
            .bindingCount = 1,
            .pBindings =
                &(VkDescriptorSetLayoutBinding){
                    .binding = 0,
                    .descriptorCount = 2048, // HACK: Some high upper limit
                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    .stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                                  VK_SHADER_STAGE_FRAGMENT_BIT,
                },
        },
        "Object Descriptor Set Layout", &sys.set_layout);
  }

  return sys;
}

void tick_render_object_system(ecs_iter_t *it) {
  TracyCZoneNC(ctx, "Render Object System", TracyCategoryColorCore, true);
  ecs_world_t *ecs = it->world;

  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  tb_auto ro_sys = ecs_singleton_get_mut(ecs, TbRenderObjectSystem);

  TbTransformsBuffer *trans_buffer = &ro_sys->trans_buffers[rnd_sys->frame_idx];

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

  tb_reset_descriptor_buffer(rnd_sys, &ro_sys->desc_buffer);

  obj_it = ecs_query_iter(ecs, ro_sys->obj_query); // reset query

  TbCommonObjectData *obj_ptr = NULL;
  if (obj_count > prev_count) {
    // We need to resize the GPU buffer
    tb_rnd_free_gpu_buffer(ro_sys->rnd_sys, &trans_buffer->gpu);
    tb_auto create_info = (VkBufferCreateInfo){
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(TbCommonObjectData) * obj_count,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
    };
    tb_rnd_sys_create_gpu_buffer(rnd_sys, &create_info, "TbTransform Buffer",
                                 &trans_buffer->gpu, &trans_buffer->host,
                                 (void **)&obj_ptr);
  } else {
    tb_rnd_sys_update_gpu_buffer(rnd_sys, &trans_buffer->gpu,
                                 &trans_buffer->host, (void **)&obj_ptr);
  }

  {
    tb_auto obj_idx = 0ul;
    while (ecs_query_next(&obj_it)) {
      tb_auto rnd_objs = ecs_field(&obj_it, TbRenderObject, 2);
      for (tb_auto i = 0; i < obj_it.count; ++i) {
        tb_auto entity = obj_it.entities[i];
        // TODO: We want to only have to do this when transforms are dirty
        // but we need to triple buffer the transform buffers to avoid
        // stomping the transform and when a transform is dirty *all* transform
        // buffers need that new data.
        // Maybe we need some kind of queueing procedure?
        obj_ptr[obj_idx].m = tb_transform_get_world_matrix(ecs, entity);
        rnd_objs[i].index = obj_idx;
        obj_idx++;
      }
    }
  }

  // We can optimize this later but for now just always update this descriptor
  // set every frame
  tb_auto trans_count = trans_buffer->obj_count;
  if (trans_count > 0) {
#if TB_USE_DESC_BUFFER == 1
    tb_auto desc = (TbDescriptor){
        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .data = {
            .pStorageBuffer =
                &(VkDescriptorAddressInfoEXT){
                    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT,
                    .address = trans_buffer->gpu.address,
                    .range = sizeof(TbCommonObjectData) * trans_count,
                },
        }};
    tb_write_desc_to_buffer(rnd_sys, &ro_sys->desc_buffer, 0, &desc);
#else
    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
        .poolSizeCount = 1,
        .pPoolSizes = (VkDescriptorPoolSize[1]){{
            .descriptorCount = trans_count,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        }},
        .maxSets = 1,
    };
    VkDescriptorSetVariableDescriptorCountAllocateInfo alloc_info = {
        .sType =
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
        .descriptorSetCount = 1,
        .pDescriptorCounts = (uint32_t[1]){trans_count},
    };
    tb_rnd_frame_desc_pool_tick(rnd_sys, "render_object", &pool_info,
                                &ro_sys->set_layout, &alloc_info, ro_sys->pools,
                                1, trans_count);

    // Write all transform data to one descriptor
    tb_auto buffer_info =
        tb_alloc_nm_tp(rnd_sys->tmp_alloc, trans_count, VkDescriptorBufferInfo);

    for (int32_t i = 0; i < trans_count; ++i) {
      buffer_info[i] = (VkDescriptorBufferInfo){
          .offset = sizeof(TbCommonObjectData) * i,
          .range = sizeof(TbCommonObjectData),
          .buffer = trans_buffer->gpu.buffer,
      };
    }

    tb_auto write = (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .descriptorCount = trans_count,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .dstSet = tb_render_object_sys_get_set(ro_sys),
        .pBufferInfo = buffer_info,
    };
    tb_rnd_update_descriptors(rnd_sys, 1, &write);
#endif
  }
  TracyCZoneEnd(ctx);
}

VkDescriptorSet tb_render_object_sys_get_set(TbRenderObjectSystem *sys) {
  return tb_rnd_frame_desc_pool_get_set(sys->rnd_sys, sys->pools, 0);
}

VkDescriptorSetLayout tb_render_object_sys_get_set_layout(ecs_world_t *ecs) {
  tb_auto ctx = ecs_singleton_get_mut(ecs, TbRenderObjectSystem);
#if TB_USE_DESC_BUFFER == 1
  return ctx->set_layout2;
#else
  return ctx->set_layout;
#endif
}

// Returns the binding info of the render object system's descriptor buffer
VkDescriptorBufferBindingInfoEXT
tb_render_object_sys_get_table_addr(ecs_world_t *ecs) {
  tb_auto ctx = ecs_singleton_get_mut(ecs, TbRenderObjectSystem);
  return tb_desc_buff_get_binding(&ctx->desc_buffer);
}

void tb_register_render_object_sys(TbWorld *world) {
  TracyCZoneN(ctx, "Register Render Object Sys", true);
  ecs_world_t *ecs = world->ecs;

  ECS_COMPONENT_DEFINE(ecs, TbRenderObject);
  ECS_COMPONENT_DEFINE(ecs, TbRenderObjectSystem);

  // Metadata for TbRenderObject
  ecs_struct(ecs, {
                      .entity = ecs_id(TbRenderObject),
                      .members =
                          {
                              {.name = "perm", .type = ecs_id(ecs_u32_t)},
                              {.name = "index", .type = ecs_id(ecs_u32_t)},
                          },
                  });

  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);
  tb_auto sys =
      create_render_object_system(world->gp_alloc, world->tmp_alloc, rnd_sys);
  sys.obj_query =
      ecs_query(ecs, {
                         .filter.terms =
                             {
                                 {.id = ecs_id(TbTransformComponent)},
                                 {.id = ecs_id(TbRenderObject)},
                             },
                     });
  // Sets a singleton based on the value at a pointer
  ecs_set_ptr(ecs, ecs_id(TbRenderObjectSystem), TbRenderObjectSystem, &sys);

  // Register a tick function
  ECS_SYSTEM(ecs, tick_render_object_system, EcsPreStore,
             TbRenderObjectSystem(TbRenderObjectSystem));
  TracyCZoneEnd(ctx);
}

void tb_unregister_render_object_sys(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;
  tb_auto ctx = ecs_singleton_get_mut(ecs, TbRenderObjectSystem);
  tb_auto rnd_sys = ecs_singleton_get_mut(ecs, TbRenderSystem);

  tb_rnd_destroy_set_layout(rnd_sys, ctx->set_layout);
  tb_destroy_descriptor_buffer(rnd_sys, &ctx->desc_buffer);

  ecs_query_fini(ctx->obj_query);
  ecs_singleton_remove(ecs, TbRenderObjectSystem);
}
