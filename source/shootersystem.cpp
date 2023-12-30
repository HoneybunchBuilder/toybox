#include "shootersystem.h"

#include "assetsystem.h"
#include "inputsystem.h"
#include "physicssystem.hpp"
#include "profiling.h"
#include "transformcomponent.h"
#include "world.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/PhysicsSystem.h>

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

  auto ent = ecs.entity(e);

  {
    bool has_comp = false;

    json_object_object_foreach(extra, key, value) {
      if (SDL_strcmp(key, "id") == 0) {
        const char *id_str = json_object_get_string(value);
        if (SDL_strcmp(id_str, "shooter") == 0) {
          has_comp = true;
          break;
        }
      }
    }

    if (!has_comp) {
      // no error, we just don't need to keep going
      return true;
    }

    ShooterComponent comp = {};
    json_object_object_foreach(extra, key2, value2) {
      if (SDL_strcmp(key2, "projectile") == 0) {
        comp.prefab_name = json_object_get_string(value2);
      }
    }
    ent.set<ShooterComponent>(comp);
  }

  {
    bool has_comp = false;

    json_object_object_foreach(extra, key, value) {
      if (SDL_strcmp(key, "id") == 0) {
        const char *id_str = json_object_get_string(value);
        if (SDL_strcmp(id_str, "projectile") == 0) {
          has_comp = true;
          break;
        }
      }
    }

    if (!has_comp) {
      // no error, we just don't need to keep going
      return true;
    }

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

void shooter_tick(flecs::iter it) {
  ZoneScopedN("Shooter Update Tick");
  auto ecs = it.world();

  auto input_sys = ecs.get_mut<TbInputSystem>();

  bool fire_input = false;
  float3 dir = {};
  // Determine if there was an input and if so what direction it should be
  // spawned relative to the player
  if (input_sys->mouse.left) {
    fire_input = true;
    // TODO: How to convert mouse position into direction relative to player
  }
  if (input_sys->controller_count > 0) {
    auto controller = input_sys->controller_states[0];
    if (controller.right_trigger > 0.15f) {
      fire_input = true;
      dir = controller.right_stick.xxy;
      dir.x = 0;
    }
  }
  if (!fire_input || tb_magf3(dir) == 0) {
    return;
  }
  dir = tb_normf3(dir);

  // Spawn the projectile

  // Apply velocity
  auto *phys_sys = ecs.get_mut<TbPhysicsSystem>();
  auto &jolt = *phys_sys->jolt_phys;
  auto &body_iface = jolt.GetBodyInterface();
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
