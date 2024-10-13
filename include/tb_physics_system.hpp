#pragma once

#include "tb_allocator.h"
#include "tb_physics_system.h"
#include "tb_rigidbody_component.h"

#include <memory>

namespace JPH {
class PhysicsSystem;
class TempAllocator;
class JobSystem;
} // namespace JPH

namespace flecs {
struct query_base;
} // namespace flecs
class ObjectLayerPairFilterImpl;
class BPLayerInterfaceImpl;
class ObjectVsBroadPhaseLayerFilterImpl;
class TbContactListener;
class TbJobSystem;

struct TbPhysicsSystem {
  TbAllocator gp_alloc;
  TbAllocator tmp_alloc;

  JPH::PhysicsSystem *jolt_phys;
  JPH::TempAllocator *jolt_tmp_alloc;
  TbJobSystem *jolt_job_sys;

  flecs::query_base *rigidbody_query;

  BPLayerInterfaceImpl *bpl_interface;
  ObjectVsBroadPhaseLayerFilterImpl *obp_filter;
  ObjectLayerPairFilterImpl *olp_filter;
  TbContactListener *listener;
};
