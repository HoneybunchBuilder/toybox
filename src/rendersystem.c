#include "rendersystem.h"

#include "profiling.h"
#include "renderthread.h"
#include "tbcommon.h"
#include "world.h"

bool create_render_system(RenderSystem *self,
                          const RenderSystemDescriptor *desc) {
  TB_CHECK_RETURN(desc, "Invalid RenderSystemDescriptor", false);
  *self = (RenderSystem){
      .render_thread = desc->render_thread,
  };
  return true;
}

void destroy_render_system(RenderSystem *self) { *self = (RenderSystem){0}; }

void tick_render_system(RenderSystem *self, const SystemInput *input,
                        SystemOutput *output, float delta_seconds) {
  (void)self;
  (void)input;
  (void)output; // Won't actually have output to the world but will write to the
                // screen we hope
  (void)delta_seconds;

  TracyCZoneN(tick_ctx, "Render System Tick", true);
  TracyCZoneColor(tick_ctx, TracyCategoryColorRendering);

  // Wait for the render thread to finish the frame with this index
  tb_wait_render(self->render_thread, 0);

  // Signal the render thread to start the next frame
  tb_signal_render(self->render_thread, 0);

  TracyCZoneEnd(tick_ctx);
}

TB_DEFINE_SYSTEM(render, RenderSystem, RenderSystemDescriptor)

void tb_render_system_descriptor(SystemDescriptor *desc,
                                 const RenderSystemDescriptor *render_desc) {
  desc->name = "Render";
  desc->size = sizeof(RenderSystem);
  desc->id = RenderSystemId;
  desc->desc = (InternalDescriptor)render_desc;
  SDL_memset(desc->deps, 0,
             sizeof(SystemComponentDependencies) * MAX_DEPENDENCY_SET_COUT);
  desc->dep_count = 0;
  desc->create = tb_create_render_system;
  desc->destroy = tb_destroy_render_system;
  desc->tick = tb_tick_render_system;
}
