#include "imguisystem.h"

#include "imguicomponent.h"
#include "inputcomponent.h"
#include "profiling.h"
#include "tbcommon.h"

bool create_imgui_system(ImGuiSystem *self, const ImGuiSystemDescriptor *desc) {
  TB_CHECK_RETURN(desc, "Invalid descriptor", false);

  *self = (ImGuiSystem){
      .render_thread = desc->render_thread,
      .tmp_alloc = desc->tmp_alloc,
  };
  return true;
}

void destroy_imgui_system(ImGuiSystem *self) { *self = (ImGuiSystem){0}; }

void tick_imgui_system(ImGuiSystem *self, const SystemInput *input,
                       SystemOutput *output, float delta_seconds) {
  (void)self;
  (void)input;
  (void)output; // No output for this system
  (void)delta_seconds;
  TracyCZoneN(ctx, "ImGui System", true);
  TracyCZoneColor(ctx, TracyCategoryColorUI);

  TracyCZoneEnd(ctx);
}

TB_DEFINE_SYSTEM(imgui, ImGuiSystem, ImGuiSystemDescriptor)

void tb_imgui_system_descriptor(SystemDescriptor *desc,
                                const ImGuiSystemDescriptor *input_desc) {
  desc->name = "ImGui";
  desc->size = sizeof(ImGuiSystem);
  desc->id = ImGuiSystemId;
  desc->desc = (InternalDescriptor)input_desc;
  SDL_memset(desc->deps, 0,
             sizeof(SystemComponentDependencies) * MAX_DEPENDENCY_SET_COUT);
  desc->dep_count = 1;
  desc->deps[0] = (SystemComponentDependencies){
      .count = 1,
      .dependent_ids = {InputComponentId},
  };
  desc->deps[1] = (SystemComponentDependencies){
      .count = 1,
      .dependent_ids = {ImGuiComponentId},
  };
  desc->create = tb_create_imgui_system;
  desc->destroy = tb_destroy_imgui_system;
  desc->tick = tb_tick_imgui_system;
}
