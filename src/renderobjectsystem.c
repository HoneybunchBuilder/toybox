#include "renderobjectsystem.h"

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

  *self = (RenderObjectSystem){0};
}

void tick_render_object_system(RenderObjectSystem *self,
                               const SystemInput *input, SystemOutput *output,
                               float delta_seconds) {
  (void)self;
  (void)input;
  (void)output;
  (void)delta_seconds;
  TracyCZoneNC(ctx, "Render Object System Tick", TracyCategoryColorRendering,
               true);
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

TbRenderObjectId tb_render_object_system_create(RenderObjectSystem *self,
                                                const CommonObjectData *data) {
  (void)self;
  (void)data;
  return InvalidRenderObjectId;
}

void tb_render_object_system_set(RenderObjectSystem *self,
                                 TbRenderObjectId object,
                                 const CommonObjectData *data) {
  (void)self;
  (void)object;
  (void)data;
}
