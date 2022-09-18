#include "oceansystem.h"

#include "oceancomponent.h"
#include "world.h"

bool create_ocean_system(OceanSystem *self, const OceanSystemDescriptor *desc,
                         uint32_t system_dep_count,
                         System *const *system_deps) {
  (void)system_dep_count;
  (void)system_deps;
  if (!desc) {
    return false;
  }

  *self = (OceanSystem){
      .tmp_alloc = desc->tmp_alloc,
      .std_alloc = desc->std_alloc,
  };
  return true;
}

void destroy_ocean_system(OceanSystem *self) { *self = (OceanSystem){0}; }

void tick_ocean_system(OceanSystem *self, const SystemInput *input,
                       SystemOutput *output, float delta_seconds) {
  (void)self;
  (void)input;
  (void)output;
  (void)delta_seconds;
}

TB_DEFINE_SYSTEM(ocean, OceanSystem, OceanSystemDescriptor)

void tb_ocean_system_descriptor(SystemDescriptor *desc,
                                const OceanSystemDescriptor *ocean_desc) {
  *desc = (SystemDescriptor){
      .name = "Ocean",
      .size = sizeof(OceanSystem),
      .id = OceanSystemId,
      .desc = (InternalDescriptor)ocean_desc,
      .dep_count = 1,
      .deps[0] = (SystemComponentDependencies){1, {OceanComponentId}},
      .create = tb_create_ocean_system,
      .destroy = tb_destroy_ocean_system,
      .tick = tb_tick_ocean_system,
  };
}
