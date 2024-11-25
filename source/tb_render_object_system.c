#include "tb_render_object_system.h"

#include "tb_common.h"
#include "tb_common.slangh"
#include "tb_mesh_component.h"
#include "tb_profiling.h"
#include "tb_render_system.h"
#include "tb_transform_component.h"
#include "tb_world.h"

#include "blocks/Block.h"

// Configuration
static const uint32_t TbMaxRenderObjects = 1 << 12; // 4096

void tb_register_render_object_sys(TbWorld *world);
void tb_unregister_render_object_sys(TbWorld *world);

TB_REGISTER_SYS(tb, render_object, TB_RND_OBJ_SYS_PRIO)

ECS_COMPONENT_DECLARE(TbRenderObject);
ECS_COMPONENT_DECLARE(TbRenderObjectSystem);
ECS_TAG_DECLARE(TbRenderObjectDirty);

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
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    .stageFlags = VK_SHADER_STAGE_VERTEX_BIT |
                                  VK_SHADER_STAGE_MESH_BIT_EXT |
                                  VK_SHADER_STAGE_FRAGMENT_BIT,
                },
        },
        "Object Descriptor Set Layout", &sys.set_layout);
  }

  // TODO: Must initialize render object GPU buffer

  // Pool just needs to point to one buffer
  tb_create_dyn_desc_pool(rnd_sys, "Render Object Descriptor Pool",
                          sys.set_layout, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                          &sys.desc_pool, 0);

  tb_reset_free_list(gp_alloc, &sys.free_list, TbMaxRenderObjects);

  return sys;
}

VkDescriptorSet tb_render_object_sys_get_set(ecs_world_t *ecs) {
  tb_auto rnd_sys = ecs_singleton_ensure(ecs, TbRenderSystem);
  tb_auto ctx = ecs_singleton_ensure(ecs, TbRenderObjectSystem);
  return tb_dyn_desc_pool_get_set(rnd_sys, &ctx->desc_pool);
}

VkDescriptorSetLayout tb_render_object_sys_get_set_layout(ecs_world_t *ecs) {
  tb_auto ctx = ecs_singleton_ensure(ecs, TbRenderObjectSystem);
  return ctx->set_layout;
}

// Returns the binding info of the render object system's descriptor buffer
VkDescriptorBufferBindingInfoEXT
tb_render_object_sys_get_table_addr(ecs_world_t *ecs) {
  (void)ecs;
  // TODO: Remove
  return (VkDescriptorBufferBindingInfoEXT){0};
}

void tb_mark_as_render_object(ecs_world_t *ecs, ecs_entity_t ent) {
  tb_auto ctx = ecs_singleton_ensure(ecs, TbRenderObjectSystem);
  uint32_t idx = 0;
  bool ok = tb_pull_index(&ctx->free_list, &idx);
  TB_CHECK(ok, "Failed to retrieve index from free list");
  ecs_set(ecs, ent, TbRenderObject,
          {
              .index = idx,
          });
  tb_render_object_mark_dirty(ecs, ent);
}

void tb_render_object_mark_dirty(ecs_world_t *ecs, ecs_entity_t ent) {
  if (ecs_has(ecs, ent, TbRenderObject)) {
    ecs_add(ecs, ent, TbRenderObjectDirty);
  }
}

void tb_update_ro_pool(ecs_iter_t *it) {
  TB_TRACY_SCOPE("Update Render Object Pool");
  tb_auto ctx = ecs_field(it, TbRenderObjectSystem, 0);
  tb_auto rnd_sys = ecs_field(it, TbRenderSystem, 1);

  tb_tick_dyn_desc_pool(rnd_sys, &ctx->desc_pool);
}

void tb_upload_transforms(ecs_iter_t *it) {
  TB_TRACY_SCOPE("Upload Render Object Buffer");
  tb_auto ecs = it->world;

  tb_auto ctx = ecs_field(it, TbRenderObjectSystem, 0);
  tb_auto rnd_sys = ecs_field(it, TbRenderSystem, 1);

  uint32_t dirty_count = 0;
  ecs_iter_t dirty_it = ecs_query_iter(ecs, ctx->dirty_query);
  while (ecs_query_next(&dirty_it)) {
    dirty_count += dirty_it.count;
  }
  if (dirty_count == 0) {
    return;
  }
  // Reset query
  dirty_it = ecs_query_iter(ecs, ctx->dirty_query);

  // If we have any dirty objects just mark the whole buffer for upload
  // We may want to do this in chunks instead
  TbCommonObjectData *write_ptr = NULL;
  tb_rnd_sys_update_gpu_buffer(rnd_sys, &ctx->trans_buffer.gpu,
                               &ctx->trans_buffer.host, (void **)&write_ptr);
  while (ecs_query_next(&dirty_it)) {
    tb_auto render_objects = ecs_field(&dirty_it, TbRenderObject, 0);
    for (int32_t i = 0; i < dirty_it.count; ++i) {
      tb_auto dst_idx = render_objects[i].index;
      write_ptr[dst_idx].m =
          tb_transform_get_world_matrix(dirty_it.world, dirty_it.entities[i]);
      ecs_remove(dirty_it.world, dirty_it.entities[i], TbRenderObjectDirty);
    }
  }
}

void tb_register_render_object_sys(TbWorld *world) {
  TB_TRACY_SCOPE("Register Render Object System");
  ecs_world_t *ecs = world->ecs;

  ECS_COMPONENT_DEFINE(ecs, TbRenderObject);
  ECS_COMPONENT_DEFINE(ecs, TbRenderObjectSystem);
  ECS_TAG_DEFINE(ecs, TbRenderObjectDirty);

  // Metadata for TbRenderObject
  ecs_struct(ecs, {
                      .entity = ecs_id(TbRenderObject),
                      .members =
                          {
                              {.name = "index", .type = ecs_id(ecs_u32_t)},
                          },
                  });

  tb_auto rnd_sys = ecs_singleton_ensure(ecs, TbRenderSystem);
  tb_auto sys =
      create_render_object_system(world->gp_alloc, world->tmp_alloc, rnd_sys);

  sys.dirty_query = ecs_query(
      ecs,
      {
          .terms =
              {
                  {.id = ecs_id(TbRenderObject), .inout = EcsIn},
                  {.id = ecs_id(TbTransformComponent), .inout = EcsInOutNone},
                  {.id = ecs_id(TbRenderObjectDirty), .inout = EcsInOutNone},
              },
          .cache_kind = EcsQueryCacheAuto,
      });

  VkBufferCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = sizeof(TbCommonObjectData) * TbMaxRenderObjects,
      .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
               VK_BUFFER_USAGE_TRANSFER_DST_BIT |
               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
               VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
  };
  void *write_ptr = NULL;
  tb_rnd_sys_create_gpu_buffer(rnd_sys, &create_info, "TbTransform Buffer",
                               &sys.trans_buffer.gpu, &sys.trans_buffer.host,
                               (void **)&write_ptr);
  (void)write_ptr; // Unused

  // Only need to write this once
  {
    // HACK: This leaks but we need it to survive
    tb_auto write = tb_alloc_tp(rnd_sys->gp_alloc, TbDynDescWrite);
    *write = (TbDynDescWrite){
        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .desc.buffer =
            (VkDescriptorBufferInfo){
                .offset = 0,
                .buffer = sys.trans_buffer.gpu.buffer,
                .range = sys.trans_buffer.gpu.info.size,
            },
    };
    tb_write_dyn_desc_pool(&sys.desc_pool, 1, write, 0);
  }

  // Sets a singleton based on the value at a pointer
  ecs_set_ptr(ecs, ecs_id(TbRenderObjectSystem), TbRenderObjectSystem, &sys);

  ECS_SYSTEM(ecs, tb_update_ro_pool,
             EcsPreStore, [in] TbRenderObjectSystem($), [in] TbRenderSystem($));
  ECS_SYSTEM(ecs, tb_upload_transforms,
             EcsOnStore, [in] TbRenderObjectSystem($), [in] TbRenderSystem($));
}

void tb_unregister_render_object_sys(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;
  tb_auto ctx = ecs_singleton_ensure(ecs, TbRenderObjectSystem);
  tb_auto rnd_sys = ecs_singleton_ensure(ecs, TbRenderSystem);

  tb_rnd_destroy_set_layout(rnd_sys, ctx->set_layout);
  tb_destroy_free_list(&ctx->free_list);

  tb_rnd_free_gpu_buffer(rnd_sys, &ctx->trans_buffer.gpu);
  ctx->trans_buffer = (TbTransformsBuffer){0};

  ecs_singleton_remove(ecs, TbRenderObjectSystem);
}
