#include "noclipcomponent.h"

#include "world.h"

bool create_noclip_component(NoClipComponent *comp,
                             const NoClipComponentDescriptor *desc,
                             uint32_t system_dep_count,
                             System *const *system_deps) {
  (void)system_dep_count;
  (void)system_deps;
  *comp = (NoClipComponent){
      .move_speed = desc->move_speed,
      .look_speed = desc->look_speed,
  };
  return true;
}

void destroy_noclip_component(NoClipComponent *comp) {
  *comp = (NoClipComponent){0};
}

TB_DEFINE_COMPONENT(noclip, NoClipComponent, NoClipComponentDescriptor)

void tb_noclip_component_descriptor(ComponentDescriptor *desc) {
  desc->name = "NoClip";
  desc->size = sizeof(NoClipComponent);
  desc->id = NoClipComponentId;
  desc->create = tb_create_noclip_component;
  desc->destroy = tb_destroy_noclip_component;
}
