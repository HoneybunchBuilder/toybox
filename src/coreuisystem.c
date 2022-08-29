#include "coreuisystem.h"

#include "coreuicomponent.h"
#include "imguicomponent.h"
#include "profiling.h"
#include "tbcommon.h"
#include "tbimgui.h"

bool create_coreui_system(CoreUISystem *self,
                          const CoreUISystemDescriptor *desc) {
  TB_CHECK_RETURN(desc, "Invalid descriptor", false);

  *self = (CoreUISystem){
      .tmp_alloc = desc->tmp_alloc,
  };
  return true;
}

void destroy_coreui_system(CoreUISystem *self) { *self = (CoreUISystem){0}; }

void tick_coreui_system(CoreUISystem *self, const SystemInput *input,
                        SystemOutput *output, float delta_seconds) {
  (void)self;
  (void)input;
  (void)output;
  (void)delta_seconds;
  TracyCZoneN(ctx, "Core UI System Tick", true);
  TracyCZoneColor(ctx, TracyCategoryColorUI);

  TracyCZoneEnd(ctx);
}

TB_DEFINE_SYSTEM(coreui, CoreUISystem, CoreUISystemDescriptor)

void tb_coreui_system_descriptor(SystemDescriptor *desc,
                                 const CoreUISystemDescriptor *coreui_desc) {
  desc->name = "CoreUI";
  desc->size = sizeof(CoreUISystem);
  desc->id = CoreUISystemId;
  desc->desc = (InternalDescriptor)coreui_desc;
  SDL_memset(desc->deps, 0,
             sizeof(SystemComponentDependencies) * MAX_DEPENDENCY_SET_COUT);
  desc->dep_count = 2;
  desc->deps[0] = (SystemComponentDependencies){
      .count = 2,
      .dependent_ids = {CoreUIComponentId, ImGuiComponentId},
  };
  desc->create = tb_create_coreui_system;
  desc->destroy = tb_destroy_coreui_system;
  desc->tick = tb_tick_coreui_system;
}
