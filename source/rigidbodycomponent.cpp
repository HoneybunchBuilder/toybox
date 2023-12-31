#include "rigidbodycomponent.h"

#include "assetsystem.h"
#include "physicssystem.hpp"
#include "simd.h"
#include "tbcommon.h"
#include "tbgltf.h"
#include "transformcomponent.h"
#include "world.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include "physlayers.h"

#include <flecs.h>
#include <json-c/json.h>

JPH::ShapeRefC create_box_shape(float3 half_extents) {
  JPH::BoxShapeSettings settings(
      JPH::Vec3(half_extents.x, half_extents.y, half_extents.z));
  return settings.Create().Get();
}

JPH::ShapeRefC create_sphere_shape(float radius) {
  JPH::SphereShapeSettings settings(radius);
  return settings.Create().Get();
}

JPH::ShapeRefC create_capsule_shape(float half_height, float radius) {
  JPH::CapsuleShapeSettings settings(half_height, radius);
  return settings.Create().Get();
}

bool create_rigidbody_component(ecs_world_t *world, ecs_entity_t e,
                                const char *source_path, const cgltf_node *node,
                                json_object *extra) {
  (void)source_path;

  flecs::world ecs(world);
  bool ret = true;
  if (node && extra) {
    bool rigidbody = false;
    {
      json_object_object_foreach(extra, key, value) {
        if (SDL_strcmp(key, "id") == 0) {
          const char *id_str = json_object_get_string(value);
          if (SDL_strcmp(id_str, RigidbodyComponentIdStr) == 0) {
            rigidbody = true;
            break;
          }
        }
      }
    }

    if (rigidbody) {

      JPH::EShapeSubType shape_type = (JPH::EShapeSubType)-1;
      {
        json_object_object_foreach(extra, key, value) {
          if (SDL_strcmp(key, "shape_type") == 0) {
            const char *shape_type_str = json_object_get_string(value);
            if (SDL_strcmp(shape_type_str, "box") == 0) {
              shape_type = JPH::EShapeSubType::Box;
              break;
            } else if (SDL_strcmp(shape_type_str, "sphere") == 0) {
              shape_type = JPH::EShapeSubType::Sphere;
              break;
            } else if (SDL_strcmp(shape_type_str, "capsule") == 0) {
              shape_type = JPH::EShapeSubType::Capsule;
              break;
            }
          }
        }
      }

      JPH::ShapeRefC shape = {};

      if (shape_type == JPH::EShapeSubType::Box) {
        float3 half_extents = {};
        json_object_object_foreach(extra, key, value) {
          if (SDL_strcmp(key, "half_ext_x") == 0) {
            half_extents.x = (float)json_object_get_double(value);
          } else if (SDL_strcmp(key, "half_ext_y") == 0) {
            half_extents.y = (float)json_object_get_double(value);
          } else if (SDL_strcmp(key, "half_ext_z") == 0) {
            half_extents.z = (float)json_object_get_double(value);
          }
        }
        shape = create_box_shape(half_extents);
      } else if (shape_type == JPH::EShapeSubType::Sphere) {
        float radius = 0;
        json_object_object_foreach(extra, key, value) {
          if (SDL_strcmp(key, "sphere_radius") == 0) {
            radius = (float)json_object_get_double(value);
          }
        }
        shape = create_sphere_shape(radius);
      } else if (shape_type == JPH::EShapeSubType::Capsule) {
        float half_height = 0.5f;
        float radius = 0.0f;
        json_object_object_foreach(extra, key, value) {
          if (SDL_strcmp(key, "capsule_half_height") == 0) {
            radius = (float)json_object_get_double(value);
          }
          if (SDL_strcmp(key, "capsule_radius") == 0) {
            radius = (float)json_object_get_double(value);
          }
        }
        shape = create_capsule_shape(half_height, radius);
      } else {
        TB_CHECK(false, "Invalid physics shape");
      }

      JPH::ObjectLayer layer = {};
      JPH::EMotionType motion = {};
      {
        json_object_object_foreach(extra, key, value) {
          if (SDL_strcmp(key, "layer") == 0) {
            const char *layer_str = json_object_get_string(value);
            if (SDL_strcmp(layer_str, "moving") == 0) {
              layer = Layers::MOVING;
            } else if (SDL_strcmp(layer_str, "non_moving") == 0) {
              layer = Layers::NON_MOVING;
            } else {
              TB_CHECK(false, "Invalid physics layer");
            }
          } else if (SDL_strcmp(key, "motion") == 0) {
            const char *motion_str = json_object_get_string(value);
            if (SDL_strcmp(motion_str, "static") == 0) {
              motion = JPH::EMotionType::Static;
            } else if (SDL_strcmp(motion_str, "kinematic") == 0) {
              motion = JPH::EMotionType::Kinematic;
            } else if (SDL_strcmp(motion_str, "dynamic") == 0) {
              motion = JPH::EMotionType::Dynamic;
            } else {
              TB_CHECK(false, "Invalid physics motion");
            }
          }
        }
      }

      // All degrees of freedom are allowed unless specified otherwise
      JPH::EAllowedDOFs allowed_dofs = JPH::EAllowedDOFs::All;
      {
        json_object_object_foreach(extra, key, value) {
          if (SDL_strcmp(key, "trans_x") == 0) {
            if (!json_object_get_boolean(value)) {
              allowed_dofs &= ~JPH::EAllowedDOFs::TranslationX;
            }
          } else if (SDL_strcmp(key, "tranx_y") == 0) {
            if (!json_object_get_boolean(value)) {
              allowed_dofs &= ~JPH::EAllowedDOFs::TranslationY;
            }
          } else if (SDL_strcmp(key, "trans_z") == 0) {
            if (!json_object_get_boolean(value)) {
              allowed_dofs &= ~JPH::EAllowedDOFs::TranslationZ;
            }
          } else if (SDL_strcmp(key, "rot_x") == 0) {
            if (!json_object_get_boolean(value)) {
              allowed_dofs &= ~JPH::EAllowedDOFs::RotationX;
            }
          } else if (SDL_strcmp(key, "rot_y") == 0) {
            if (!json_object_get_boolean(value)) {
              allowed_dofs &= ~JPH::EAllowedDOFs::RotationY;
            }
          } else if (SDL_strcmp(key, "rot_z") == 0) {
            if (!json_object_get_boolean(value)) {
              allowed_dofs &= ~JPH::EAllowedDOFs::RotationZ;
            }
          }
        }
      }

      auto *phys_sys = ecs.get_mut<TbPhysicsSystem>();
      auto *jolt = (JPH::PhysicsSystem *)(phys_sys->jolt_phys);
      auto &bodies = jolt->GetBodyInterface();

      // Position and rotation set in post load
      JPH::BodyCreationSettings body_settings(
          shape, JPH::Vec3::sZero(), JPH::Quat::sIdentity(), motion, layer);
      body_settings.mAllowedDOFs = allowed_dofs;

      JPH::BodyID body =
          bodies.CreateAndAddBody(body_settings, JPH::EActivation::Activate);

      TbRigidbodyComponent comp = {
          body.GetIndexAndSequenceNumber(),
      };
      ecs.entity(e).set<TbRigidbodyComponent>(comp);
    }
  }
  return ret;
}

void post_load_rigidbody_component(ecs_world_t *world, ecs_entity_t e) {
  flecs::world ecs(world);

  if (!ecs.entity(e).has<TbRigidbodyComponent>() ||
      !ecs.entity(e).has<TbTransformComponent>()) {
    return;
  }

  auto *phys_sys = ecs.get_mut<TbPhysicsSystem>();
  auto *jolt = (JPH::PhysicsSystem *)(phys_sys->jolt_phys);
  auto &bodies = jolt->GetBodyInterface();

  auto rb = ecs.entity(e).get_mut<TbRigidbodyComponent>();
  auto trans = ecs.entity(e).get_mut<TbTransformComponent>();

  // Set the body position and rotation based on the final world transform
  // of the entity.
  auto world_trans = tb_transform_get_world_trans(ecs.c_ptr(), trans);
  auto pos = world_trans.position;
  // This only works because we're assuming that there is no offset rotation
  auto rot = world_trans.rotation;

  auto position = JPH::Vec3(pos.x, pos.y, pos.z);
  auto rotation = JPH::Quat(rot.x, rot.y, rot.z, rot.w);

  const auto body = (JPH::BodyID)rb->body;
  bodies.SetPositionAndRotation(body, position, rotation,
                                JPH::EActivation::Activate);
}

void remove_rigidbody_components(ecs_world_t *world) {
  flecs::world ecs(world);

  auto *phys_sys = ecs.get_mut<TbPhysicsSystem>();
  auto *jolt = (JPH::PhysicsSystem *)(phys_sys->jolt_phys);
  auto &bodies = jolt->GetBodyInterface();

  // Remove rigidbody components from entities
  auto f = ecs.filter<TbRigidbodyComponent>();
  f.each([&](TbRigidbodyComponent &comp) {
    bodies.RemoveBody(JPH::BodyID(comp.body));
  });
  ecs.remove_all<TbRigidbodyComponent>();
}

void tb_register_rigidbody_component(TbWorld *world) {
  flecs::world ecs(world->ecs);

  // Create an observer to trigger when a component is added

  TbAssetSystem asset = {
      .add_fn = create_rigidbody_component,
      .post_load_fn = post_load_rigidbody_component,
      .rem_fn = remove_rigidbody_components,
  };

  // Sets the TbAssetSystem component on the TbPhysicsSystem singleton entity
  ecs.entity<TbPhysicsSystem>().set<TbAssetSystem>(asset);
}
