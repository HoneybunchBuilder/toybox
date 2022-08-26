#include "noclipcontrollersystem.h"

#include "noclipcomponent.h"
#include "profiling.h"
#include "tbsdl.h"
#include "transformcomponent.h"
#include "world.h"

bool create_noclip_system(NoClipControllerSystem *self,
                          const NoClipControllerSystemDescriptor *desc) {
  if (!desc) {
    return false;
  }

  *self = (NoClipControllerSystem){0};
  return true;
}

void destroy_noclip_system(NoClipControllerSystem *self) {
  *self = (NoClipControllerSystem){0};
}

void tick_noclip_system(NoClipControllerSystem *self, const SystemInput *input,
                        SystemOutput *output, float delta_seconds) {
  (void)self;
  (void)input;
  (void)output;
  (void)delta_seconds;
  TracyCZoneN(tick_ctx, "NoClip System Tick", true);
  TracyCZoneColor(tick_ctx, TracyCategoryColorGame);

  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Ticking NoClip System");

  TracyCZoneEnd(tick_ctx);
}

TB_DEFINE_SYSTEM(noclip, NoClipControllerSystem,
                 NoClipControllerSystemDescriptor)

void tb_noclip_controller_system_descriptor(
    SystemDescriptor *desc,
    const NoClipControllerSystemDescriptor *noclip_desc) {
  desc->name = "NoClip";
  desc->size = sizeof(NoClipControllerSystem);
  desc->id = NoClipControllerSystemId;
  desc->desc = (InternalDescriptor)noclip_desc;
  SDL_memset(desc->deps, 0,
             sizeof(SystemComponentDependencies) * MAX_DEPENDENCY_SET_COUT);
  desc->dep_count = 2;
  desc->deps[0] = (SystemComponentDependencies){2,
                                                {
                                                    TransformComponentId,
                                                    NoClipComponentId,
                                                }};
  desc->create = tb_create_noclip_system;
  desc->destroy = tb_destroy_noclip_system;
  desc->tick = tb_tick_noclip_system;
}
