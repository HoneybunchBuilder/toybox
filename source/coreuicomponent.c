#include "coreuicomponent.h"

#include "world.h"

bool create_coreui_component(CoreUIComponent *self, const void *desc,
                             uint32_t system_dep_count,
                             System *const *system_deps) {
  (void)system_dep_count;
  (void)system_deps;
  (void)desc;
  *self = (CoreUIComponent){
      .show_all = 1, // Show UI by default
  };
  return true;
}

void destroy_coreui_component(CoreUIComponent *self, uint32_t system_dep_count,
                              System *const *system_deps) {
  (void)system_dep_count;
  (void)system_deps;
  *self = (CoreUIComponent){0};
}

TB_DEFINE_COMPONENT(coreui, CoreUIComponent, void)

void tb_coreui_component_descriptor(ComponentDescriptor *desc) {
  desc->name = "CoreUI";
  desc->size = sizeof(CoreUIComponent);
  desc->id = CoreUIComponentId;
  desc->create = tb_create_coreui_component;
  desc->destroy = tb_destroy_coreui_component;
}
