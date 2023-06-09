#include "renderobjectsystem.h"

#include "common.hlsli"
#include "meshcomponent.h"
#include "profiling.h"
#include "rendersystem.h"
#include "tbcommon.h"
#include "transformcomponent.h"
#include "world.h"

bool create_render_object_system(RenderObjectSystem *self,
                                 const RenderObjectSystemDescriptor *desc,
                                 uint32_t system_dep_count,
                                 System *const *system_deps) {
  // Find the necessary systems
  RenderSystem *render_system =
      tb_get_system(system_deps, system_dep_count, RenderSystem);
  TB_CHECK_RETURN(render_system,
                  "Failed to find render system which render objects depend on",
                  false);

  *self = (RenderObjectSystem){
      .render_system = render_system,
      .tmp_alloc = desc->tmp_alloc,
      .std_alloc = desc->std_alloc,
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
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            },
    };
    err = tb_rnd_create_set_layout(render_system, &create_info,
                                   "Object Descriptor Set Layout",
                                   &self->set_layout);
    TB_VK_CHECK_RET(err, "Failed to create render object descriptor set",
                    false);
  }

  return true;
}

void destroy_render_object_system(RenderObjectSystem *self) {
  tb_rnd_destroy_set_layout(self->render_system, self->set_layout);

  for (uint32_t i = 0; i < TB_MAX_FRAME_STATES; ++i) {
    RenderObjectSystemFrameState *state = &self->frame_states[i];
    tb_rnd_destroy_descriptor_pool(self->render_system, state->set_pool);
  }

  *self = (RenderObjectSystem){0};
}

void tick_render_object_system(RenderObjectSystem *self,
                               const SystemInput *input, SystemOutput *output,
                               float delta_seconds) {
  (void)input;
  (void)output;
  (void)delta_seconds;
  TracyCZoneNC(ctx, "Render Object System Tick", TracyCategoryColorRendering,
               true);

  if (self->render_object_count == 0) {
    TracyCZoneEnd(ctx);
    return;
  }

  VkResult err = VK_SUCCESS;

  RenderSystem *render_system = self->render_system;

  VkBuffer tmp_gpu_buffer = tb_rnd_get_gpu_tmp_buffer(render_system);

  RenderObjectSystemFrameState *state =
      &self->frame_states[render_system->frame_idx];
  // Allocate all the descriptor sets for this frame
  if (state->set_count < self->render_object_count) {
    TracyCZoneN(alloc_ctx, "Allocate Descriptor Sets", true);

    if (state->set_pool) {
      tb_rnd_destroy_descriptor_pool(render_system, state->set_pool);
    }

    VkDescriptorPoolCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = self->render_object_count,
        .poolSizeCount = 1,
        .pPoolSizes =
            &(VkDescriptorPoolSize){
                .descriptorCount = self->render_object_count,
                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            },
        .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
    };
    err = tb_rnd_create_descriptor_pool(
        render_system, &create_info, "View System Frame State Descriptor Pool",
        &state->set_pool);
    TB_VK_CHECK(
        err,
        "Failed to create render object system frame state descriptor pool");

    state->set_count = self->render_object_count;
    state->sets = tb_realloc_nm_tp(self->std_alloc, state->sets,
                                   state->set_count, VkDescriptorSet);

    const uint32_t set_count = self->render_object_count;

    VkDescriptorSetLayout *layouts =
        tb_alloc_nm_tp(self->tmp_alloc, set_count, VkDescriptorSetLayout);
    for (uint32_t i = 0; i < set_count; ++i) {
      layouts[i] = self->set_layout;
    }

    VkDescriptorSetAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorSetCount = set_count,
        .descriptorPool = state->set_pool,
        .pSetLayouts = layouts,
    };
    err = vkAllocateDescriptorSets(render_system->render_thread->device,
                                   &alloc_info, state->sets);
    TB_VK_CHECK(err, "Failed to re-allocate render object descriptor sets");
    TracyCZoneEnd(alloc_ctx);
  }

  // Just upload and write all objects for now, they tend to be important
  // anyway
  // NOTE: This is currently a hot spot. Since all objects in the scene have
  // their object data written, this can often take a sizeable amount of
  // time on the main thread. This should only process objects after view
  // culling.
  VkWriteDescriptorSet *writes = tb_alloc_nm_tp(
      self->tmp_alloc, self->render_object_count, VkWriteDescriptorSet);
  VkDescriptorBufferInfo *buffer_info = tb_alloc_nm_tp(
      self->tmp_alloc, self->render_object_count, VkDescriptorBufferInfo);
  TbHostBuffer *buffers =
      tb_alloc_nm_tp(self->tmp_alloc, self->render_object_count, TbHostBuffer);
  for (uint32_t obj_idx = 0; obj_idx < self->render_object_count; ++obj_idx) {
    const CommonObjectData *data = &self->render_object_data[obj_idx];
    TbHostBuffer *buffer = &buffers[obj_idx];

    // Write object data into the tmp buffer we know will wind up on the GPU
    err = tb_rnd_sys_alloc_tmp_host_buffer(
        render_system, sizeof(CommonObjectData), 0x40, buffer);
    TB_VK_CHECK(err,
                "Failed to make tmp host buffer allocation for render object");

    // Copy object data to the allocated buffer
    SDL_memcpy(buffer->ptr, data, sizeof(CommonObjectData));

    // Get the descriptor we want to write to
    VkDescriptorSet obj_set = state->sets[obj_idx];

    buffer_info[obj_idx] = (VkDescriptorBufferInfo){
        .buffer = tmp_gpu_buffer,
        .offset = buffer->offset,
        .range = sizeof(CommonObjectData),
    }; // Construct a write descriptor
    writes[obj_idx] = (VkWriteDescriptorSet){
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = obj_set,
        .dstBinding = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .pBufferInfo = &buffer_info[obj_idx],
    };
  }
  vkUpdateDescriptorSets(self->render_system->render_thread->device,
                         self->render_object_count, writes, 0, NULL);

  TracyCZoneEnd(ctx);
}

TB_DEFINE_SYSTEM(render_object, RenderObjectSystem,
                 RenderObjectSystemDescriptor)

void tb_render_object_system_descriptor(
    SystemDescriptor *desc, const RenderObjectSystemDescriptor *object_desc) {
  *desc = (SystemDescriptor){
      .name = "Render Object",
      .size = sizeof(RenderObjectSystem),
      .id = RenderObjectSystemId,
      .desc = (InternalDescriptor)object_desc,
      .dep_count = 1,
      .deps[0] = {2, {MeshComponentId, TransformComponentId}},
      .system_dep_count = 1,
      .system_deps[0] = RenderSystemId,
      .create = tb_create_render_object_system,
      .destroy = tb_destroy_render_object_system,
      .tick = tb_tick_render_object_system,
  };
}

TbRenderObjectId tb_render_object_system_create(RenderObjectSystem *self) {
  TbRenderObjectId object = self->render_object_count;
  uint32_t new_count = self->render_object_count + 1;
  if (new_count > self->render_object_max) {
    // Reallocate collections
    const uint32_t new_max = new_count * 2;

    Allocator alloc = self->std_alloc;

    self->render_object_ids = tb_realloc_nm_tp(alloc, self->render_object_ids,
                                               new_max, TbRenderObjectId);
    self->render_object_data = tb_realloc_nm_tp(alloc, self->render_object_data,
                                                new_max, CommonObjectData);
    self->render_object_max = new_max;
  }

  self->render_object_data[object] = (CommonObjectData){.m = {.col0 = {0}}};
  // Supply an identity matrix for the world space model matrix
  self->render_object_data[object].m = mf44_identity();

  self->render_object_count = new_count;

  return object;
}

void tb_render_object_system_set_object_data(RenderObjectSystem *self,
                                             TbRenderObjectId object,
                                             const CommonObjectData *data) {
  if (object >= self->render_object_count) {
    TB_CHECK(false, "Render Object Id out of range");
  }
  self->render_object_data[object] = *data;
}

VkDescriptorSet
tb_render_object_system_get_descriptor(RenderObjectSystem *self,
                                       TbRenderObjectId object) {
  if (object >= self->render_object_count) {
    TB_CHECK(false, "View Id out of range");
  }

  return self->frame_states[self->render_system->frame_idx].sets[object];
}

const CommonObjectData *
tb_render_object_system_get_data(RenderObjectSystem *self,
                                 TbRenderObjectId object) {
  if (object >= self->render_object_count) {
    TB_CHECK_RETURN(false, "View Id out of range", NULL);
  }
  return &self->render_object_data[object];
}
