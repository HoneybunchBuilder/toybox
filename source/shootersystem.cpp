#include "shootersystem.h"

#include "assetsystem.h"
#include "inputsystem.h"
#include "meshcomponent.h"
#include "physicssystem.hpp"
#include "profiling.h"
#include "rigidbodycomponent.h"
#include "tbcommon.h"
#include "transformcomponent.h"
#include "world.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include "physlayers.h"

#include <flecs.h>
#include <json.h>

bool create_shooter_components(ecs_world_t *world, ecs_entity_t e,
                               const char *source_path, const cgltf_node *node,
                               json_object *extra) {
  (void)source_path;
  flecs::world ecs(world);

  if (!node || !extra) {
    return true;
  }

  auto json_id = json_object_object_get(extra, "id");
  if (!json_id) {
    return true;
  }

  const char *id_str = json_object_get_string(json_id);
  auto ent = ecs.entity(e);

  if (SDL_strcmp(id_str, "shooter") == 0) {
    ShooterComponent comp = {};
    if (auto proj_id = json_object_object_get(extra, "projectile")) {
      if (auto name_id = json_object_object_get(proj_id, "name")) {
        comp.prefab_name = json_object_get_string(name_id);
      }
    }
    ent.set<ShooterComponent>(comp);
  } else if (SDL_strcmp(id_str, "projectile") == 0) {
    // Tag the entity as being a projectile
    ent.add<Projectile>();
  }

  return true;
}

void post_load_shooter_components(ecs_world_t *world, ecs_entity_t e) {
  flecs::world ecs(world);

  if (!ecs.entity(e).has<ShooterComponent>()) {
    return;
  }

  auto shooter = ecs.entity(e).get_mut<ShooterComponent>();
  shooter->projectile_prefab = ecs.lookup(shooter->prefab_name);
}

void remove_shooter_components(ecs_world_t *world) {
  flecs::world ecs(world);
  ecs.remove_all<ShooterComponent>();
}

void shooter_tick(flecs::iter &it, ShooterComponent *shooters,
                  TbTransformComponent *transforms) {
  ZoneScopedN("Shooter Update Tick");
  auto ecs = it.world();

  static float timer = 0.0f;
  timer += it.delta_time();

  auto input_sys = ecs.get_mut<TbInputSystem>();
  auto phys_sys = ecs.get_mut<TbPhysicsSystem>();

  float3 dir = {};
  // Determine if there was an input and if so what direction it should be
  // spawned relative to the player
  if (input_sys->mouse.left) {
    // TODO: How to convert mouse position into direction relative to player
  }
  if (input_sys->controller_count > 0) {
    auto stick = input_sys->controller_states[0].right_stick;
    dir = -tb_f3(stick.x, 0, stick.y);
  }
  if (tb_magf3(dir) < 0.0001f) {
    return;
  }
  dir = tb_normf3(dir);

  for (auto i : it) {
    auto entity = it.entity(i);
    auto &shooter = shooters[i];
    auto &shooter_trans = transforms[i];

    if (timer - shooter.last_fire_time >= 0.1f) {
      shooter.last_fire_time = timer;
    } else {
      continue;
    }

    // Spawn the projectile by cloning the prefab entity
    auto prefab = ecs.entity(shooter.projectile_prefab);
    flecs::entity mesh;
    prefab.children([&](flecs::entity child) {
      if (child.has<TbMeshComponent>()) {
        mesh = child;
      }
    });

    auto pos = tb_transform_get_world_trans(ecs.c_ptr(), entity).position;

    // Add a copy of the mesh entity as a child
    {
      auto clone = ecs.entity()
                       .is_a(shooter.projectile_prefab)
                       .enable()
                       .override(ecs_id(TbRigidbodyComponent));
      // Must manually create a unique rigid body
      {
        auto jolt = (JPH::PhysicsSystem *)(phys_sys->jolt_phys);
        auto &bodies = jolt->GetBodyInterface();

        float radius = 0.25f;
        auto shape = JPH::SphereShapeSettings(radius).Create().Get();

        JPH::BodyCreationSettings settings(
            shape, JPH::Vec3(pos.x, pos.y, pos.z), JPH::Quat::sIdentity(),
            JPH::EMotionType::Dynamic, Layers::MOVING);

        auto body =
            bodies.CreateAndAddBody(settings, JPH::EActivation::Activate);
        TbRigidbodyComponent comp = {body.GetIndexAndSequenceNumber()};
        clone.set<TbRigidbodyComponent>(comp);

        // Now apply velocity manually
        float3 vel = dir * 15.0f;
        bodies.SetLinearAndAngularVelocity(body, JPH::Vec3(vel.x, vel.y, vel.z),
                                           JPH::Vec3(0, 0, 0));
      }
      ecs.entity().is_a(mesh).child_of(clone).enable();
    }
  }
}

void tb_register_shooter_system(TbWorld *world) {
  flecs::world ecs(world->ecs);

  TbAssetSystem asset = {
      .add_fn = create_shooter_components,
      .post_load_fn = post_load_shooter_components,
      .rem_fn = remove_shooter_components,
  };
  struct ShooterAssetSystem {};
  ecs.singleton<ShooterAssetSystem>().set(asset);

  ecs.system<ShooterComponent, TbTransformComponent>("Shooter System")
      .kind(EcsOnUpdate)
      .iter(shooter_tick);
}

void tb_unregister_shooter_system(TbWorld *world) {
  auto ecs = world->ecs;
  (void)ecs;
}
