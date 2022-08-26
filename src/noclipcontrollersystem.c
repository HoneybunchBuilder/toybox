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

void tick_noclip_system(SystemDependencyColumns *columns,
                        NoClipControllerSystem *self, float delta_seconds) {
  (void)self;
  (void)columns;
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
  desc->deps = (SystemComponentDependencies){2,
                                             {
                                                 TransformComponentId,
                                                 NoClipComponentId,
                                             }};
  desc->create = tb_create_noclip_system;
  desc->destroy = tb_destroy_noclip_system;
  desc->tick = tb_tick_noclip_system;
}
