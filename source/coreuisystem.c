#include "coreuisystem.h"

#include "imguicomponent.h"
#include "profiling.h"
#include "tbcommon.h"
#include "tbengineconfig.h"
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
  TB_DYN_ARR_RESET(self->menu_registry, self->std_alloc, 1);

  self->metrics = tb_coreui_register_menu(self, "Metrics");
  self->about = tb_coreui_register_menu(self, "About");

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
    igText("Version: %s", TB_ENGINE_VERSION);
    igText("Platform: %s", TB_PLATFORM);
    igText("Arch: %s", TB_ARCH);
    igText("Git Hash: %s", GIT_COMMIT_HASH);
    igEnd();
  }
}

void tick_coreui_system(CoreUISystem *self, const SystemInput *input,
                        SystemOutput *output, float delta_seconds) {
  (void)output;
  (void)delta_seconds;

  TracyCZoneN(ctx, "Core UI System Tick", true);
  TracyCZoneColor(ctx, TracyCategoryColorUI);

  // Find expected components
  uint32_t entity_count = 0;
  const EntityId *entities = NULL;
  const PackedComponentStore *imgui_comp_store =
      tb_get_column_check_id(input, 0, 0, ImGuiComponentId);
  for (uint32_t dep_set_idx = 0; dep_set_idx < input->dep_set_count;
       ++dep_set_idx) {
    const SystemDependencySet *dep_set = &input->dep_sets[dep_set_idx];
    entities = dep_set->entity_ids;
    entity_count = dep_set->entity_count;
  }
  if (entity_count > 0) {
    TB_CHECK(entities, "Invalid input entities");

    for (uint32_t entity_idx = 0; entity_idx < entity_count; ++entity_idx) {
      const ImGuiComponent *imgui =
          tb_get_component(imgui_comp_store, entity_idx, ImGuiComponent);

      igSetCurrentContext(imgui->context);

      if (igBeginMainMenuBar()) {
        TB_DYN_ARR_FOREACH(self->menu_registry, i) {
          CoreUIMenu *menu = &TB_DYN_ARR_AT(self->menu_registry, i);
          if (igBeginMenu(menu->name, true)) {
            *menu->active = !*menu->active;
            igEndMenu();
          }
        }
        igEndMainMenuBar();
      }

      if (*self->about) {
        coreui_show_about(self->about);
      }
      if (*self->metrics) {
        igShowMetricsWindow(self->metrics);
      }
    }
  }

  TracyCZoneEnd(ctx);
}

TB_DEFINE_SYSTEM(coreui, CoreUISystem, CoreUISystemDescriptor)

void tb_coreui_system_descriptor(SystemDescriptor *desc,
                                 const CoreUISystemDescriptor *coreui_desc) {
  *desc = (SystemDescriptor){
      .name = "CoreUI",
      .size = sizeof(CoreUISystem),
      .id = CoreUISystemId,
      .desc = (InternalDescriptor)coreui_desc,
      .dep_count = 1,
      .deps[0] = {1, {ImGuiComponentId}},
      .create = tb_create_coreui_system,
      .destroy = tb_destroy_coreui_system,
      .tick = tb_tick_coreui_system,
  };
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
