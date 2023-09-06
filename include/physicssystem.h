#pragma once

#define PhysicsSystemId 0x1BADB003

#ifdef __cplusplus
extern "C" {
#endif

#include "allocator.h"

typedef struct SystemDescriptor SystemDescriptor;
typedef struct PhysicsSystemImpl PhysicsSystemImpl;

typedef struct PhysicsSystem {
  Allocator std_alloc;
  Allocator tmp_alloc;

  // The actual internal system implemented in C++
  PhysicsSystemImpl *sys;
} PhysicsSystem;

typedef struct PhysicsSystemDescriptor {
  Allocator std_alloc;
  Allocator tmp_alloc;
} PhysicsSystemDescriptor;

void tb_physics_system_descriptor(SystemDescriptor *desc,
                                  const PhysicsSystemDescriptor *phys_desc);

#ifdef __cplusplus
}
#endif
