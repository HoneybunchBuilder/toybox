#include "coreuicomponent.h"

#include "world.h"

bool create_coreui_component(CoreUIComponent *self, const void *desc) {
  (void)desc;
  *self = (CoreUIComponent){0};
  return true;
}

void destroy_coreui_component(CoreUIComponent *self) {
  *self = (CoreUIComponent){0};
}

TB_DEFINE_COMPONENT(coreui, CoreUIComponent, void)

void tb_coreui_component_descriptor(ComponentDescriptor *desc) {
  desc->name = "Input";
  desc->size = sizeof(CoreUIComponent);
  desc->id = CoreUIComponentId;
  desc->create = tb_create_coreui_component;
  desc->destroy = tb_destroy_coreui_component;
}
