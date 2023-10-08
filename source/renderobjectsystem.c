#include "renderobjectsystem.h"

#include "common.hlsli"
#include "meshcomponent.h"
#include "profiling.h"
#include "rendersystem.h"
#include "tbcommon.h"
#include "transformcomponent.h"
#include "world.h"

#include <flecs.h>

RenderObjectSystem create_render_object_system(Allocator std_alloc,
                                               Allocator tmp_alloc,
                                               RenderSystem *rnd_sys) {
  RenderObjectSystem sys = (RenderObjectSystem){
      .render_system = rnd_sys,
      .tmp_alloc = tmp_alloc,
      .std_alloc = std_alloc,
  };

  TB_DYN_ARR_RESET(sys.render_object_data, sys.std_alloc, 8);

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
  TB_DYN_ARR_DESTROY(self->render_object_data);

  tb_rnd_destroy_set_layout(self->render_system, self->set_layout);

  for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
    RenderObjectSystemFrameState *state = &self->frame_states[i];
    tb_rnd_destroy_descriptor_pool(self->render_system, state->pool);
    TB_DYN_ARR_DESTROY(state->sets);
  }

  *self = (RenderObjectSystem){0};
}

void tb_register_render_object_sys(ecs_world_t *ecs, Allocator std_alloc,
                                   Allocator tmp_alloc) {
  ECS_COMPONENT(ecs, RenderSystem);
  ECS_COMPONENT(ecs, RenderObjectSystem);
  RenderSystem *rnd_sys = ecs_singleton_get_mut(ecs, RenderSystem);
  RenderObjectSystem sys =
      create_render_object_system(std_alloc, tmp_alloc, rnd_sys);
  // Sets a singleton based on the value at a pointer
  ecs_set_ptr(ecs, ecs_id(RenderObjectSystem), RenderObjectSystem, &sys);
}

void tb_unregister_render_object_sys(ecs_world_t *ecs) {
  ECS_COMPONENT(ecs, RenderObjectSystem);
  RenderObjectSystem *sys = ecs_singleton_get_mut(ecs, RenderObjectSystem);
  destroy_render_object_system(sys);
  ecs_singleton_remove(ecs, RenderObjectSystem);
}

TbRenderObjectId tb_render_object_system_create(RenderObjectSystem *self) {
  TbRenderObjectId object = TB_DYN_ARR_SIZE(self->render_object_data);
  TB_DYN_ARR_APPEND(self->render_object_data,
                    (CommonObjectData){.m = mf44_identity()});
  return object;
}

void tb_render_object_system_set_object_data(RenderObjectSystem *self,
                                             TbRenderObjectId object,
                                             const CommonObjectData *data) {
  TB_CHECK(object < TB_DYN_ARR_SIZE(self->render_object_data),
           "Render Object Id out of range");
  TB_DYN_ARR_AT(self->render_object_data, object) = *data;
}

VkDescriptorSet
tb_render_object_system_get_descriptor(RenderObjectSystem *self,
                                       TbRenderObjectId object) {
  TB_CHECK(object < TB_DYN_ARR_SIZE(self->render_object_data),
           "Render Object Id out of range");
  RenderObjectSystemFrameState *state =
      &self->frame_states[self->render_system->frame_idx];
  return TB_DYN_ARR_AT(state->sets, object);
}

const CommonObjectData *
tb_render_object_system_get_data(RenderObjectSystem *self,
                                 TbRenderObjectId object) {
  TB_CHECK(object < TB_DYN_ARR_SIZE(self->render_object_data),
           "Render Object Id out of range");
  return &TB_DYN_ARR_AT(self->render_object_data, object);
}
