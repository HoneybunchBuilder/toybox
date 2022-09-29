#include "rendertargetsystem.h"

#include "rendersystem.h"
#include "tbcommon.h"
#include "world.h"

typedef struct RenderTarget {
  uint32_t ref_count;
  TbImage images[TB_MAX_FRAME_STATES];
} RenderTarget;

bool create_render_target_system(RenderTargetSystem *self,
                                 const RenderTargetSystemDescriptor *desc,
                                 uint32_t system_dep_count,
                                 System *const *system_deps) {
  // Find necessary systems
  RenderSystem *render_system = (RenderSystem *)tb_find_system_dep_self_by_id(
      system_deps, system_dep_count, RenderSystemId);
  TB_CHECK_RETURN(render_system,
                  "Failed to find render system which render targets depend on",
                  false);

  *self = (RenderTargetSystem){
      .render_system = render_system,
      .tmp_alloc = desc->tmp_alloc,
      .std_alloc = desc->std_alloc,
  };

  // Create some default render targets
  {
    const uint32_t width = self->render_system->render_thread->swapchain.width;
    const uint32_t height =
        self->render_system->render_thread->swapchain.height;
  }

  return true;
}

void destroy_render_target_system(RenderTargetSystem *self) {
  *self = (RenderTargetSystem){0};
}

void tick_render_target_system(RenderTargetSystem *self,
                               const SystemInput *input, SystemOutput *output,
                               float delta_seconds) {
  (void)self;
  (void)input;
  (void)output;
  (void)delta_seconds;
}

TB_DEFINE_SYSTEM(render_target, RenderTargetSystem,
                 RenderTargetSystemDescriptor)

void tb_render_target_system_descriptor(
    SystemDescriptor *desc, const RenderTargetSystemDescriptor *rt_desc) {
  *desc = (SystemDescriptor){
      .name = "Render Target",
      .size = sizeof(RenderTargetSystem),
      .id = RenderTargetSystemId,
      .desc = (InternalDescriptor)rt_desc,
      .dep_count = 0,
      .system_dep_count = 1,
      .system_deps[0] = RenderSystemId,
      .create = tb_create_render_target_system,
      .destroy = tb_destroy_render_target_system,
      .tick = tb_tick_render_target_system,
  };
}