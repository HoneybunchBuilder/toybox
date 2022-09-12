#include "inputcomponent.h"

#include "world.h"

bool create_input_component(InputComponent *self, const void *desc,
                            uint32_t system_dep_count,
                            System *const *system_deps) {
  (void)system_dep_count;
  (void)system_deps;
  (void)desc;
  *self = (InputComponent){0};
  return true;
}

void destroy_input_component(InputComponent *self, uint32_t system_dep_count,
                             System *const *system_deps) {
  (void)system_dep_count;
  (void)system_deps;
  *self = (InputComponent){0};
}

TB_DEFINE_COMPONENT(input, InputComponent, void)

void tb_input_component_descriptor(ComponentDescriptor *desc) {
  desc->name = "Input";
  desc->size = sizeof(InputComponent);
  desc->id = InputComponentId;
  desc->create = tb_create_input_component;
  desc->destroy = tb_destroy_input_component;
}
