#include "coreuisystem.h"

#include "coreuicomponent.h"
#include "imguicomponent.h"
#include "profiling.h"
#include "tbcommon.h"
#include "tbimgui.h"

typedef struct CoreUIMenu {
  bool *active;
  const char *name;
} CoreUIMenu;

bool create_coreui_system(CoreUISystem *self,
                          const CoreUISystemDescriptor *desc,
                          uint32_t system_dep_count,
                          System *const *system_deps) {
  (void)system_dep_count;
  (void)system_deps;
  TB_CHECK_RETURN(desc, "Invalid descriptor", false);

  *self = (CoreUISystem){
      .std_alloc = desc->std_alloc,
      .tmp_alloc = desc->tmp_alloc,
  };
  TB_DYN_ARR_RESET(self->menu_registry, self->std_alloc, 8);
  return true;
}

void destroy_coreui_system(CoreUISystem *self) {
  // Clean up registry
  TB_DYN_ARR_FOREACH(self->menu_registry, i) {
    CoreUIMenu menu = TB_DYN_ARR_AT(self->menu_registry, i);
    tb_free(self->std_alloc, menu.active);
  }
  TB_DYN_ARR_DESTROY(self->menu_registry);
  *self = (CoreUISystem){0};
}

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
  const PackedComponentStore *coreui_comp_store =
      tb_get_column_check_id(input, 0, 0, CoreUIComponentId);
  const PackedComponentStore *imgui_comp_store =
      tb_get_column_check_id(input, 0, 1, ImGuiComponentId);
  for (uint32_t dep_set_idx = 0; dep_set_idx < input->dep_set_count;
       ++dep_set_idx) {
    const SystemDependencySet *dep_set = &input->dep_sets[dep_set_idx];
    entities = dep_set->entity_ids;
    entity_count = dep_set->entity_count;
  }
  if (entity_count > 0) {
    TB_CHECK(entities, "Invalid input entities");

    CoreUIComponent *out_coreui =
        tb_alloc_nm_tp(self->tmp_alloc, entity_count, CoreUIComponent);
    SDL_memset(out_coreui, 0, sizeof(CoreUIComponent) * entity_count);

    for (uint32_t entity_idx = 0; entity_idx < entity_count; ++entity_idx) {
      const CoreUIComponent *coreui =
          tb_get_component(coreui_comp_store, entity_idx, CoreUIComponent);
      const ImGuiComponent *imgui =
          tb_get_component(imgui_comp_store, entity_idx, ImGuiComponent);

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
        TB_DYN_ARR_FOREACH(self->menu_registry, i) {
          CoreUIMenu *menu = &TB_DYN_ARR_AT(self->menu_registry, i);
          if (igBeginMenu(menu->name, true)) {
            *menu->active = !*menu->active;
            igEndMenu();
          }
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
             sizeof(SystemComponentDependencies) * MAX_DEPENDENCY_SET_COUNT);
  desc->dep_count = 1;
  desc->deps[0] = (SystemComponentDependencies){
      .count = 2,
      .dependent_ids = {CoreUIComponentId, ImGuiComponentId},
  };
  desc->create = tb_create_coreui_system;
  desc->destroy = tb_destroy_coreui_system;
  desc->tick = tb_tick_coreui_system;
}

bool *tb_coreui_register_menu(CoreUISystem *self, const char *name) {
  // Store the bool on the heap so it survives registry resizes
  bool *active = tb_alloc_tp(self->std_alloc, bool);
  CoreUIMenu menu = {
      .active = active,
      .name = name,
  };
  TB_DYN_ARR_APPEND(self->menu_registry, menu);
  return active;
}
