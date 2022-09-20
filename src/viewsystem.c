#include "viewsystem.h"

#include "cameracomponent.h"
#include "profiling.h"
#include "rendersystem.h"
#include "tbcommon.h"
#include "transformcomponent.h"
#include "world.h"

bool create_view_system(ViewSystem *self, const ViewSystemDescriptor *desc,
                        uint32_t system_dep_count, System *const *system_deps) {
  // Find the necessary systems
  RenderSystem *render_system =
      tb_get_system(system_deps, system_dep_count, RenderSystem);
  TB_CHECK_RETURN(render_system,
                  "Failed to find render system which view depends on", false);

  *self = (ViewSystem){
      .render_system = render_system,
      .tmp_alloc = desc->tmp_alloc,
      .std_alloc = desc->std_alloc,
  };

  VkResult err = VK_SUCCESS;

  // Create view descriptor set layout
  {
    VkDescriptorSetLayoutCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings =
            &(VkDescriptorSetLayoutBinding){
                .binding = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .stageFlags =
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            },
    };
    err = tb_rnd_create_set_layout(render_system, &create_info,
                                   "View Descriptor Set Layout",
                                   &self->set_layout);
    TB_VK_CHECK_RET(err, "Failed to create view descriptor set", false);
  }

  return true;
}

void destroy_view_system(ViewSystem *self) {
  tb_rnd_destroy_set_layout(self->render_system, self->set_layout);

  *self = (ViewSystem){0};
}

void tick_view_system(ViewSystem *self, const SystemInput *input,
                      SystemOutput *output, float delta_seconds) {
  (void)self;
  (void)input;
  (void)output;
  (void)delta_seconds;
  TracyCZoneNC(ctx, "View System Tick", TracyCategoryColorRendering, true);
  TracyCZoneEnd(ctx);
}

TB_DEFINE_SYSTEM(view, ViewSystem, ViewSystemDescriptor)

void tb_view_system_descriptor(SystemDescriptor *desc,
                               const ViewSystemDescriptor *view_desc) {
  *desc = (SystemDescriptor){
      .name = "View",
      .size = sizeof(ViewSystem),
      .id = ViewSystemId,
      .desc = (InternalDescriptor)view_desc,
      .dep_count = 1,
      .deps[0] = {2, {CameraComponentId, TransformComponentId}},
      .system_dep_count = 1,
      .system_deps[0] = RenderSystemId,
      .create = tb_create_view_system,
      .destroy = tb_destroy_view_system,
      .tick = tb_tick_view_system,
  };
}

TbViewId tb_view_system_create_view(ViewSystem *self) {
  (void)self;
  return InvalidViewId;
}
