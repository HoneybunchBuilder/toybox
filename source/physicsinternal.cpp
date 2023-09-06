#include "physicssystem.h"

// Seem to have to define this because the vcpkg portfile defines it
#define JPH_PROFILE_ENABLED 1

#include <new> // has to be before jolt

#include <Jolt/Jolt.h> // Must come before other jolt includes

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include "physicsinternal.h"

// Layer that objects can be in, determines which other objects it can collide
// with Typically you at least want to have 1 layer for moving bodies and 1
// layer for static bodies, but you can have more layers if you want. E.g. you
// could have a layer for high detail collision (which is not used by the
// physics simulation but only if you do collision testing).
namespace Layers {
static constexpr JPH::ObjectLayer NON_MOVING = 0;
static constexpr JPH::ObjectLayer MOVING = 1;
static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
}; // namespace Layers

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

// Each broadphase layer results in a separate bounding volume tree in the broad
// phase. You at least want to have a layer for non-moving and moving objects to
// avoid having to update a tree full of static objects every frame. You can
// have a 1-on-1 mapping between object layers and broadphase layers (like in
// this case) but if you have many object layers you'll be creating many broad
// phase trees, which is not efficient. If you want to fine tune your broadphase
// layers define JPH_TRACK_BROADPHASE_STATS and look at the stats reported on
// the TTY.
namespace BroadPhaseLayers {
static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
static constexpr JPH::BroadPhaseLayer MOVING(1);
static constexpr uint32_t NUM_LAYERS(2);
}; // namespace BroadPhaseLayers

// BroadPhaseLayerInterface implementation
// This defines a mapping between object and broadphase layers.
class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
public:
  BPLayerInterfaceImpl() {
    // Create a mapping table from object to broad phase layer
    object_to_broad_phase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
    object_to_broad_phase[Layers::MOVING] = BroadPhaseLayers::MOVING;
  }

  virtual uint32_t GetNumBroadPhaseLayers() const override {
    return BroadPhaseLayers::NUM_LAYERS;
  }

  virtual JPH::BroadPhaseLayer
  GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override {
    return object_to_broad_phase[inLayer];
  }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
  virtual const char *
  GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override {
    switch ((JPH::BroadPhaseLayer::Type)inLayer) {
    case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING:
      return "NON_MOVING";
    case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::MOVING:
      return "MOVING";
    default:
      return "INVALID";
    }
  }
#endif

private:
  JPH::BroadPhaseLayer object_to_broad_phase[Layers::NUM_LAYERS];
};

/// Class that determines if an object layer can collide with a broadphase layer
class ObjectVsBroadPhaseLayerFilterImpl
    : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
  virtual bool ShouldCollide(JPH::ObjectLayer inLayer1,
                             JPH::BroadPhaseLayer inLayer2) const override {
    switch (inLayer1) {
    case Layers::NON_MOVING:
      return inLayer2 == BroadPhaseLayers::MOVING;
    case Layers::MOVING:
      return true;
    default:
      return false;
    }
  }
};

class JoltPhysicsSystem {
public:
  JoltPhysicsSystem(PhysicsSystem *self) {
    this->self = self;

    JPH::RegisterDefaultAllocator();

    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

    // TODO: Wrap with toybox allocator
    tmp_alloc = new JPH::TempAllocatorImpl(10 * 1024 * 1024);
    job_system = new JPH::JobSystemThreadPool(
        JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
        std::thread::hardware_concurrency() - 1);

    const uint32_t max_bodies = 1024;
    const uint32_t num_body_mutexes = 0;
    const uint32_t max_body_pairs = 1024;
    const uint32_t max_contract_constraints = 1024;

    BPLayerInterfaceImpl broad_phase_layer_interface = {};
    ObjectVsBroadPhaseLayerFilterImpl object_vs_broadphase_layer_filter = {};
    ObjectLayerPairFilterImpl object_vs_object_layer_filter = {};

    jolt_phys = new JPH::PhysicsSystem();
    jolt_phys->Init(max_bodies, num_body_mutexes, max_body_pairs,
                    max_contract_constraints, broad_phase_layer_interface,
                    object_vs_broadphase_layer_filter,
                    object_vs_object_layer_filter);
    body_interface = &jolt_phys->GetBodyInterface();
  }
  ~JoltPhysicsSystem() {
    JPH::UnregisterTypes();

    delete jolt_phys;

    delete job_system;
    delete tmp_alloc;

    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
  }

  void Tick(float delta_seconds) {
    jolt_phys->Update(delta_seconds, 1, 1, tmp_alloc, job_system);
  }

private:
  PhysicsSystem *self;

  JPH::PhysicsSystem *jolt_phys;
  JPH::BodyInterface *body_interface;
  JPH::TempAllocatorImpl *tmp_alloc;
  JPH::JobSystemThreadPool *job_system;
};

extern "C" {
PhysicsSystemImpl *create_phys_internal(PhysicsSystem *self) {
  return (PhysicsSystemImpl *)new JoltPhysicsSystem(self);
}
void tick_phys_internal(PhysicsSystemImpl *impl, float delta_seconds) {
  ((JoltPhysicsSystem *)impl)->Tick(delta_seconds);
}
void destroy_phys_internal(PhysicsSystemImpl *impl) {
  delete ((JoltPhysicsSystem *)impl);
}
}