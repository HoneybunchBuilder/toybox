#include "physicssystem.h"

#include "SDL2/SDL_log.h"
#include "profiling.h"
#include "world.h"

#include "physicsinternal.h"

bool create_physics_system(PhysicsSystem *self,
                           const PhysicsSystemDescriptor *desc,
                           uint32_t system_dep_count,
                           System *const *system_deps) {
  (void)system_dep_count;
  (void)system_deps;
  *self = (PhysicsSystem){
      .std_alloc = desc->std_alloc,
      .tmp_alloc = desc->tmp_alloc,
      .sys = create_phys_internal(self),
  };

  return true;
}

void destroy_physics_system(PhysicsSystem *self) {
  destroy_phys_internal(self->sys);
  *self = (PhysicsSystem){0};
}

void tick_physics_system_internal(PhysicsSystem *self, const SystemInput *input,
                                  SystemOutput *output, float delta_seconds) {
  (void)input;
  (void)output;
  TracyCZoneNC(ctx, "Physics System Tick", TracyCategoryColorPhysics, true);
  tick_phys_internal(self->sys, delta_seconds);
  TracyCZoneEnd(ctx);
}

TB_DEFINE_SYSTEM(physics, PhysicsSystem, PhysicsSystemDescriptor)

void tick_physics_system(void *self, const SystemInput *input,
                         SystemOutput *output, float delta_seconds) {
  SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Tick Physics System");
  tick_physics_system_internal((PhysicsSystem *)self, input, output,
                               delta_seconds);
}

void tb_physics_system_descriptor(SystemDescriptor *desc,
                                  const PhysicsSystemDescriptor *phys_desc) {
  *desc = (SystemDescriptor){.name = "Physics",
                             .size = sizeof(PhysicsSystem),
                             .id = PhysicsSystemId,
                             .desc = (InternalDescriptor)phys_desc,
                             .system_dep_count = 0,
                             .system_deps = {0},
                             .create = tb_create_physics_system,
                             .destroy = tb_destroy_physics_system,
                             .tick_fn_count = 1,
                             .tick_fns[0] = {
                                 .order = E_TICK_PHYSICS,
                                 .dep_count = 0,
                                 .system_id = PhysicsSystemId,
                                 .function = tick_physics_system,
                             }};
}
