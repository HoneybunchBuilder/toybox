#include "coreuisystem.h"

#include "coreuicomponent.h"
#include "imguicomponent.h"
#include "profiling.h"
#include "tbcommon.h"
#include "tbimgui.h"

bool create_coreui_system(CoreUISystem *self,
                          const CoreUISystemDescriptor *desc,
                          uint32_t system_dep_count,
                          System *const *system_deps) {
  (void)system_dep_count;
  (void)system_deps;
  TB_CHECK_RETURN(desc, "Invalid descriptor", false);

  *self = (CoreUISystem){
      .tmp_alloc = desc->tmp_alloc,
  };
  return true;
}

void destroy_coreui_system(CoreUISystem *self) { *self = (CoreUISystem){0}; }

void coreui_show_about(bool *open) {
  if (igBegin("About Toybox", open, 0)) {
    igEnd();
  }
}

void tick_coreui_system(CoreUISystem *self, const SystemInput *input,
                        SystemOutput *output, float delta_seconds) {
  (void)delta_seconds;
  TracyCZoneN(ctx, "Core UI System Tick", true);
  TracyCZoneColor(ctx, TracyCategoryColorUI);

  // Find expected components
  uint32_t entity_count = 0;
  const EntityId *entities = NULL;
  const PackedComponentStore *coreui_comp_store = NULL;
  const PackedComponentStore *imgui_comp_store = NULL;
  TB_CHECK(input->dep_set_count == 1, "Unexpected number of dependency sets");
  for (uint32_t dep_set_idx = 0; dep_set_idx < input->dep_set_count;
       ++dep_set_idx) {
    const SystemDependencySet *dep_set = &input->dep_sets[dep_set_idx];
    entities = dep_set->entity_ids;
    entity_count = dep_set->entity_count;
    for (uint32_t col_idx = 0; col_idx < dep_set->column_count; ++col_idx) {
      const PackedComponentStore *column = &dep_set->columns[col_idx];
      if (column->id == CoreUIComponentId) {
        coreui_comp_store = column;
      }
      if (column->id == ImGuiComponentId) {
        imgui_comp_store = column;
      }
    }
  }
  if (entity_count > 0) {
    TB_CHECK(entities, "Invalid input entities");
    TB_CHECK(coreui_comp_store, "Failed to find coreui component store");
    TB_CHECK(imgui_comp_store, "Failed to find imgui component store");

    CoreUIComponent *out_coreui =
        tb_alloc_nm_tp(self->tmp_alloc, entity_count, CoreUIComponent);
    SDL_memset(out_coreui, 0, sizeof(CoreUIComponent) * entity_count);

    for (uint32_t entity_idx = 0; entity_idx < entity_count; ++entity_idx) {
      const CoreUIComponent *coreui =
          &((const CoreUIComponent *)coreui_comp_store->components)[entity_idx];
      const ImGuiComponent *imgui =
          &((const ImGuiComponent *)imgui_comp_store->components)[entity_idx];

      *out_coreui = *coreui;

      igSetCurrentContext(imgui->context);

      if (igBeginMainMenuBar()) {
        if (igBeginMenu("About", true)) {
          out_coreui->show_about = !out_coreui->show_about;
          igEndMenu();
        }
        if (igBeginMenu("Demo", true)) {
          out_coreui->show_demo = !out_coreui->show_demo;
          igEndMenu();
        }
        if (igBeginMenu("Metrics", true)) {
          out_coreui->show_metrics = !out_coreui->show_metrics;
          igEndMenu();
        }
        igEndMainMenuBar();
      }

      if (out_coreui->show_all) {
        if (out_coreui->show_about) {
          coreui_show_about(&out_coreui->show_about);
        }
        if (out_coreui->show_demo) {
          igShowDemoWindow(&out_coreui->show_demo);
        }
        if (out_coreui->show_metrics) {
          igShowMetricsWindow(&out_coreui->show_metrics);
        }
      }
    }

    output->set_count = 1;
    output->write_sets[0] = (SystemWriteSet){
        .id = CoreUIComponentId,
        .count = entity_count,
        .entities = entities,
        .components = (uint8_t *)out_coreui,
    };
  }

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
  desc->dep_count = 1;
  desc->deps[0] = (SystemComponentDependencies){
      .count = 2,
      .dependent_ids = {CoreUIComponentId, ImGuiComponentId},
  };
  desc->create = tb_create_coreui_system;
  desc->destroy = tb_destroy_coreui_system;
  desc->tick = tb_tick_coreui_system;
}
