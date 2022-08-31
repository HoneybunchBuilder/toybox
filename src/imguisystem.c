#include "imguisystem.h"

#include "imguicomponent.h"
#include "inputcomponent.h"
#include "profiling.h"
#include "tbcommon.h"
#include "tbimgui.h"

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
  (void)output; // No output for this system
  (void)delta_seconds;
  TracyCZoneN(ctx, "ImGui System", true);
  TracyCZoneColor(ctx, TracyCategoryColorUI);

  // Find expected components
  uint32_t imgui_entity_count = 0;
  const EntityId *imgui_entities = NULL;
  const PackedComponentStore *imgui_comp_store = NULL;

  uint32_t input_entity_count = 0;
  const PackedComponentStore *input_comp_store = NULL;
  TB_CHECK(input->dep_set_count == 2, "Unexpected number of dependency sets");
  for (uint32_t dep_set_idx = 0; dep_set_idx < input->dep_set_count;
       ++dep_set_idx) {
    const SystemDependencySet *dep_set = &input->dep_sets[dep_set_idx];

    for (uint32_t col_idx = 0; col_idx < dep_set->column_count; ++col_idx) {
      const PackedComponentStore *column = &dep_set->columns[col_idx];
      if (column->id == ImGuiComponentId) {
        imgui_comp_store = column;
        imgui_entities = dep_set->entity_ids;
        imgui_entity_count = dep_set->entity_count;
      }

      if (column->id == InputComponentId) {
        input_comp_store = column;
        input_entity_count = dep_set->entity_count;
      }
    }
  }
  if (imgui_entity_count > 0) {
    TB_CHECK(imgui_entities, "Invalid input entities");
    TB_CHECK(imgui_comp_store, "Failed to find imgui component store");

    for (uint32_t entity_idx = 0; entity_idx < imgui_entity_count;
         ++entity_idx) {
      const ImGuiComponent *imgui =
          &((const ImGuiComponent *)imgui_comp_store->components)[entity_idx];

      igSetCurrentContext(imgui->context);

      // Apply this frame's input
      for (uint32_t input_idx = 0; input_idx < input_entity_count;
           ++input_idx) {
        const InputComponent *input =
            &((const InputComponent *)input_comp_store->components)[input_idx];
        for (uint32_t event_idx = 0; event_idx < input->event_count;
             ++event_idx) {
          const SDL_Event *event = &input->events[event_idx];

          // TODO: Feed event to imgui
          (void)event;
        }
      }

      // Apply basic IO
      ImGuiIO *io = igGetIO();
      io->DeltaTime = delta_seconds; // Note that ImGui expects seconds
      // TODO: Fetch this from the renderer
      io->DisplaySize = (ImVec2){1600.0f, 900.0f};

      igRender();

      ImDrawData *draw_data = igGetDrawData();
      (void)draw_data;
      TB_CHECK(draw_data, "Failed to retrieve draw data");

      igNewFrame();
    }
  }

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
  desc->dep_count = 2;
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
