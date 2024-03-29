#include "physicssystem.h"

#include "dynarray.h"
#include "physicssystem.hpp"
#include "profiling.h"
#include "rigidbodycomponent.h"
#include "transformcomponent.h"
#include "world.h"

#include <SDL3/SDL_mutex.h>

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
  // Mingw uses the microsoft aligned malloc/free impl
#if defined(_MSC_VER) || defined(__MINGW32__)
  void *ptr = _aligned_malloc(size, align);
#else
  void *ptr = std::aligned_alloc(align, size);
#endif
  TracyAllocN(ptr, size, "Physics");
  return ptr;
}

void jolt_free_aligned(void *ptr) {
  TracyFreeN(ptr, "Physics");
#if defined(_MSC_VER) || defined(__MINGW32__)
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
    case Layers::STATIC:
      return inObject2 == Layers::MOVING || inObject2 == Layers::MOVING_MESH;
    case Layers::STATIC_MESH:
      return inObject2 == Layers::MOVING;
    case Layers::MOVING:
      return true;            // Moving collides with everything
    case Layers::MOVING_MESH: // Moving mesh may not interact with any other
                              // mesh type
      return inObject2 != Layers::MOVING_MESH &&
             inObject2 != Layers::STATIC_MESH;
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
    obj_to_broad_phase[Layers::STATIC] = BroadPhaseLayers::NON_MOVING;
    obj_to_broad_phase[Layers::STATIC_MESH] = BroadPhaseLayers::NON_MOVING;
    obj_to_broad_phase[Layers::MOVING] = BroadPhaseLayers::MOVING;
    obj_to_broad_phase[Layers::MOVING_MESH] = BroadPhaseLayers::MOVING;
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
    case Layers::STATIC:
    case Layers::STATIC_MESH:
      return layer_2 == BroadPhaseLayers::MOVING;
    case Layers::MOVING:
    case Layers::MOVING_MESH:
      return true;
    default:
      return false;
    }
  }
};

struct TbContactCallback {
  tb_contact_fn fn;
  ecs_entity_t user_e;
};

struct TbContactEvent {
  TbContactCallback callback;
  ecs_entity_t other_e;
};

class TbContactListener : public JPH::ContactListener {
public:
  TbContactListener(TbWorld *world) {
    ecs = world->ecs;
    TB_DYN_ARR_RESET(callbacks, world->gp_alloc, 16);
    TB_DYN_ARR_RESET(event_queue, world->gp_alloc, 1024);

    callback_mut = SDL_CreateMutex();
    queue_mut = SDL_CreateMutex();
  }

  ~TbContactListener() {
    SDL_DestroyMutex(queue_mut);
    SDL_DestroyMutex(callback_mut);
  }

  void AddCallback(TbContactCallback callback) {
    SDL_LockMutex(callback_mut);
    TB_DYN_ARR_APPEND(callbacks, callback);
    SDL_UnlockMutex(callback_mut);
  }

  virtual void OnContactAdded(const JPH::Body &inBody1,
                              const JPH::Body &inBody2,
                              const JPH::ContactManifold &inManifold,
                              JPH::ContactSettings &ioSettings) override {
    ZoneScopedN("Physics On Contact");
    (void)inManifold;
    (void)ioSettings;
    // Report this to toybox listeners
    auto e1 = (ecs_entity_t)inBody1.GetUserData();
    auto e2 = (ecs_entity_t)inBody2.GetUserData();

    SDL_LockMutex(callback_mut);
    TB_DYN_ARR_FOREACH(callbacks, i) {
      auto &callback = TB_DYN_ARR_AT(callbacks, i);

      ecs_entity_t user_e = callback.user_e;
      ecs_entity_t other = 0;
      if (user_e == e1) {
        other = e2;
      } else if (user_e == e2) {
        other = e1;
      } else {
        continue;
      }

      SDL_LockMutex(queue_mut);
      TbContactEvent event = {callback, other};
      TB_DYN_ARR_APPEND(event_queue, event);
      SDL_UnlockMutex(queue_mut);
    }
    SDL_UnlockMutex(callback_mut);
  }

  virtual void OnContactPersisted(const JPH::Body &inBody1,
                                  const JPH::Body &inBody2,
                                  const JPH::ContactManifold &inManifold,
                                  JPH::ContactSettings &ioSettings) override {
    OnContactAdded(inBody1, inBody2, inManifold, ioSettings);
  }

  void ResolveCallbacks() {
    SDL_LockMutex(queue_mut);
    TB_DYN_ARR_FOREACH(event_queue, i) {
      auto &event = TB_DYN_ARR_AT(event_queue, i);
      event.callback.fn(ecs, event.callback.user_e, event.other_e);
    }
    TB_DYN_ARR_CLEAR(event_queue);
    SDL_UnlockMutex(queue_mut);
  }

private:
  ecs_world_t *ecs;

  SDL_Mutex *callback_mut;
  TB_DYN_ARR_OF(TbContactCallback) callbacks;

  SDL_Mutex *queue_mut;
  TB_DYN_ARR_OF(TbContactEvent) event_queue;
};

void physics_update_tick(flecs::iter it) {
  ZoneScopedN("Physics Update Tick");
  auto ecs = it.world();
  auto *phys_sys = ecs.get_mut<TbPhysicsSystem>();
  auto &jolt = *phys_sys->jolt_phys;
  auto &body_iface = jolt.GetBodyInterface();

  jolt.Update(it.delta_time(), 1, phys_sys->jolt_tmp_alloc,
              phys_sys->jolt_job_sys);

  phys_sys->listener->ResolveCallbacks();

  // Iterate through query of every rigidbody and update the entity transform
  // based on the result from the physics sim
  // TODO: Only do this for rigidbodies with transforms marked movable
  {
    flecs::query<TbRigidbodyComponent, TbTransformComponent> query(
        ecs, phys_sys->rigidbody_query);
    query.each([&](flecs::entity e, const TbRigidbodyComponent &rigidbody,
                   TbTransformComponent &trans) {
      (void)e;
      auto id = (JPH::BodyID)rigidbody.body;
      JPH::Vec3 pos = body_iface.GetPosition(id);
      JPH::Quat rot = body_iface.GetRotation(id);
      TbTransform updated = {
          .position = {pos.GetX(), pos.GetY(), pos.GetZ()},
          .scale = trans.transform.scale,
          .rotation = {rot.GetX(), rot.GetY(), rot.GetZ(), rot.GetW()},
      };
      tb_transform_set_world(ecs.c_ptr(), e, &updated);
    });
  }
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
      .gp_alloc = world->gp_alloc,
      .tmp_alloc = world->tmp_alloc,
      .jolt_phys = new JPH::PhysicsSystem(),
      .jolt_tmp_alloc = new JPH::TempAllocatorImpl(10 * 1024 * 1024),
      .jolt_job_sys = new JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs,
                                                   JPH::cMaxPhysicsBarriers, 1),
      .rigidbody_query =
          ecs.query_builder<TbRigidbodyComponent, TbTransformComponent>()
              .build(),
      .bpl_interface = new BPLayerInterfaceImpl(),
      .obp_filter = new ObjectVsBroadPhaseLayerFilterImpl(),
      .olp_filter = new ObjectLayerPairFilterImpl(),
      .listener = new TbContactListener(world),
  };
  sys.jolt_phys->Init(1024, 0, 1024, 1024, *sys.bpl_interface, *sys.obp_filter,
                      *sys.olp_filter);
  sys.jolt_phys->SetContactListener(sys.listener);

  ecs.set<TbPhysicsSystem>(sys);

  ecs.system("PhysicsUpdate").kind(EcsPostUpdate).iter(physics_update_tick);
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

extern "C" {
void tb_phys_add_velocity(TbPhysicsSystem *phys_sys,
                          const TbRigidbodyComponent *body, float3 vel) {
  auto &body_iface = phys_sys->jolt_phys->GetBodyInterface();
  auto body_id = (JPH::BodyID)body->body;
  body_iface.SetLinearAndAngularVelocity(
      body_id, JPH::Vec3(vel.x, vel.y, vel.z), JPH::Vec3(0, 0, 0));
}

void tb_phys_add_contact_callback(TbPhysicsSystem *phys_sys,
                                  ecs_entity_t user_e, tb_contact_fn cb) {
  phys_sys->listener->AddCallback({cb, user_e});
}

// Helper macro to auto-register system
TB_REGISTER_SYS(tb, physics, TB_PHYS_SYS_PRIO);
}
