#pragma once

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct PhysicsSystem PhysicsSystem;
typedef struct PhysicsSystemImpl PhysicsSystemImpl;

PhysicsSystemImpl *create_phys_internal(PhysicsSystem *self);
void tick_phys_internal(PhysicsSystemImpl *impl, float delta_seconds);
void destroy_phys_internal(PhysicsSystemImpl *impl);
#if defined(__cplusplus)
}
#endif
