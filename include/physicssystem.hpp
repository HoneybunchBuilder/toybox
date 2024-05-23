#pragma once

#include "physicssystem.h"
#include "rigidbodycomponent.h"
#include "tb_allocator.h"

namespace JPH {
class PhysicsSystem;
class TempAllocator;
class JobSystem;
} // namespace JPH

struct ecs_query_t;
namespace flecs {
typedef ecs_query_t query_t;
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

  flecs::query_t *rigidbody_query;

  BPLayerInterfaceImpl *bpl_interface;
  ObjectVsBroadPhaseLayerFilterImpl *obp_filter;
  ObjectLayerPairFilterImpl *olp_filter;
  TbContactListener *listener;
};
