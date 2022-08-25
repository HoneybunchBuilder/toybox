#include "noclipcomponent.h"

#include "world.h"

void create_noclip_component(NoClipComponent *comp,
                             const NoClipComponentDescriptor *desc) {
  *comp = (NoClipComponent){
      .move_speed = desc->move_speed,
      .look_speed = desc->look_speed,
  };
}

void destroy_noclip_component(NoClipComponent *comp) {
  *comp = (NoClipComponent){0};
}

bool tb_create_noclip_component(void *comp, InternalDescriptor desc) {
  create_noclip_component((NoClipComponent *)comp,
                          (const NoClipComponentDescriptor *)desc);
  return true;
}

void tb_destroy_noclip_component(void *comp) {
  destroy_noclip_component((NoClipComponent *)comp);
}

void tb_noclip_component_descriptor(ComponentDescriptor *desc) {
  desc->name = "NoClip";
  desc->size = sizeof(NoClipComponent);
  desc->id = NoClipComponentId;
  desc->create = tb_create_noclip_component;
  desc->destroy = tb_destroy_noclip_component;
}
