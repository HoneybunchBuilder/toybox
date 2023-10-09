#include "physicssystem.h"

#include "physicssystem.hpp"
#include "profiling.h"
#include "rigidbodycomponent.h"
#include "transformcomponent.h"
#include "world.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include "physlayers.h"

#include <flecs.h>

#include <cstdlib>

// Note: We don't use mimalloc here because
// jolt will try to call alloc between threads

void *jolt_alloc(size_t size) {
  void *ptr = malloc(size);
  TracyAllocN(ptr, size, "Physics");
  return ptr;
}
void jolt_free(void *ptr) {
  TracyFreeN(ptr, "Physics");
  free(ptr);
}
void *jolt_alloc_aligned(size_t size, size_t align) {
#ifdef _MSC_VER
  void *ptr = _aligned_malloc(size, align);
#else
  void *ptr = std::aligned_alloc(align, size);
#endif
  TracyAllocN(ptr, size, "Physics");
  return ptr;
}

void jolt_free_aligned(void *ptr) {
  TracyFreeN(ptr, "Physics");
#ifdef _MSC_VER
  _aligned_free(ptr);
#else
  free(ptr);
#endif
}

/// Class that determines if two object layers can collide
class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter {
public:
  virtual bool ShouldCollide(JPH::ObjectLayer inObject1,
                             JPH::ObjectLayer inObject2) const override {
    switch (inObject1) {
    case Layers::NON_MOVING:
      return inObject2 ==
             Layers::MOVING; // Non moving only collides with moving
    case Layers::MOVING:
      return true; // Moving collides with everything
    default:
      return false;
    }
  }
};

// BroadPhaseLayerInterface implementation
// This defines a mapping between object and broadphase layers.
class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
public:
  BPLayerInterfaceImpl() {
    // Create a mapping table from object to broad phase layer
    obj_to_broad_phase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
    obj_to_broad_phase[Layers::MOVING] = BroadPhaseLayers::MOVING;
  }

  uint32_t GetNumBroadPhaseLayers() const override {
    return BroadPhaseLayers::NUM_LAYERS;
  }

  JPH::BroadPhaseLayer
  GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
    return obj_to_broad_phase[layer];
  }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
  /// Get the user readable name of a broadphase layer (debugging purposes)
  const char *
  GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
    (void)layer;
    return NULL;
  };
#endif

private:
  JPH::BroadPhaseLayer obj_to_broad_phase[Layers::NUM_LAYERS];
};

/// Class that determines if an object layer can collide with a broadphase
/// layer
class ObjectVsBroadPhaseLayerFilterImpl
    : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
  virtual bool ShouldCollide(JPH::ObjectLayer layer_1,
                             JPH::BroadPhaseLayer layer_2) const override {
    switch (layer_1) {
    case Layers::NON_MOVING:
      return layer_2 == BroadPhaseLayers::MOVING;
    case Layers::MOVING:
      return true;
    default:
      return false;
    }
  }
};

void physics_update_tick(flecs::iter it) {
  ZoneScopedN("Physics Update Tick");
  auto ecs = it.world();
  auto *phys_sys = ecs.get_mut<TbPhysicsSystem>();
  auto &jolt = *phys_sys->jolt_phys;
  auto &body_iface = jolt.GetBodyInterface();
  auto *jolt_tmp_alloc = phys_sys->jolt_tmp_alloc;
  auto *jolt_job_sys = phys_sys->jolt_job_sys;

  jolt.Update(it.delta_time(), 1, 1, jolt_tmp_alloc, jolt_job_sys);

  // Iterate through query of every rigidbody and update the Toybox transform
  flecs::query<TbRigidbodyComponent, TransformComponent> query(
      ecs, phys_sys->rigidbody_query);

  query.each([&](flecs::entity e, const TbRigidbodyComponent &rigidbody,
                 TransformComponent &trans) {
    (void)e;
    auto id = (JPH::BodyID)rigidbody.body;
    JPH::Vec3 pos = body_iface.GetPosition(id);
    JPH::Quat rot = body_iface.GetRotation(id);
    Transform updated = {
        .position = {pos.GetX(), pos.GetY(), pos.GetZ()},
        .scale = trans.transform.scale,
        .rotation = {rot.GetX(), rot.GetY(), rot.GetZ(), rot.GetW()},
    };
    tb_transform_update(ecs.c_ptr(), &trans, &updated);
  });
}

void tb_register_physics_sys(TbWorld *world) {
  flecs::world ecs(world->ecs);

  // Override JPH allocator functions
  JPH::Allocate = jolt_alloc;
  JPH::Free = jolt_free;
  JPH::AlignedAllocate = jolt_alloc_aligned;
  JPH::AlignedFree = jolt_free_aligned;

  JPH::Factory::sInstance = new JPH::Factory();

  JPH::RegisterTypes();

  TbPhysicsSystem sys = {
      .std_alloc = world->std_alloc,
      .tmp_alloc = world->tmp_alloc,
      .jolt_phys = new JPH::PhysicsSystem(),
      .jolt_tmp_alloc = new JPH::TempAllocatorImpl(10 * 1024 * 1024),
      .jolt_job_sys = new JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs,
                                                   JPH::cMaxPhysicsBarriers, 1),
      .rigidbody_query =
          ecs.query_builder<TbRigidbodyComponent, TransformComponent>().build(),
      .bpl_interface = new BPLayerInterfaceImpl(),
      .obp_filter = new ObjectVsBroadPhaseLayerFilterImpl(),
      .olp_filter = new ObjectLayerPairFilterImpl(),
  };
  sys.jolt_phys->Init(1024, 0, 1024, 1024, *sys.bpl_interface, *sys.obp_filter,
                      *sys.olp_filter);

  ecs.set<TbPhysicsSystem>(sys);

  ecs.system("Physics").kind(EcsPreUpdate).iter(physics_update_tick);

  tb_register_rigidbody_component(world);
}

void tb_unregister_physics_sys(TbWorld *world) {
  flecs::world ecs(world->ecs);
  TbPhysicsSystem *sys = ecs.get_ref<TbPhysicsSystem>().get();
  delete sys->olp_filter;
  delete sys->obp_filter;
  delete sys->bpl_interface;
  delete sys->jolt_job_sys;
  delete sys->jolt_tmp_alloc;
  delete sys->jolt_phys;
  *sys = {};
  ecs.remove<TbPhysicsSystem>();

  JPH::UnregisterTypes();
}
