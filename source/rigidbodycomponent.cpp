#include "rigidbodycomponent.h"

#include "assetsystem.h"
#include "meshsystem.h"
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
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include "physlayers.h"

#include <flecs.h>
#include <json-c/json.h>

// From meshsystem.c
extern "C" cgltf_result decompress_buffer_view(TbAllocator alloc,
                                               cgltf_buffer_view *view);

ECS_COMPONENT_DECLARE(TbRigidbodyComponent);

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

JPH::ShapeRefC create_cylinder_shape(float half_height, float radius) {
  JPH::CylinderShapeSettings settings(half_height, radius);
  return settings.Create().Get();
}

JPH::ShapeRefC create_mesh_shape(TbAllocator gp_alloc, const cgltf_node *node) {
  JPH::VertexList phys_verts = {};
  JPH::IndexedTriangleList phys_tris = {};

  // Making some assumptions
  TB_CHECK(node->children_count == 1, "Expecting child node");
  const auto *mesh_node = node->children[0];

  // Need the mesh node's transform for dequantization
  auto trans = tb_transform_from_node(mesh_node);
  auto dequant_mat = tb_transform_to_matrix(&trans);

  // Read all indices and positions for this mesh
  TB_CHECK(mesh_node->mesh, "Expecting mesh");
  const auto *mesh = mesh_node->mesh;

  // All primitive geometry will be merged
  for (cgltf_size i = 0; i < mesh->primitives_count; ++i) {
    const auto &primitive = mesh->primitives[i];
    // Indices
    {
      auto *indices = primitive.indices;
      auto *view = indices->buffer_view;

      auto res = decompress_buffer_view(gp_alloc, view);
      TB_CHECK(res == cgltf_result_success, "Failed to decode buffer view");

      auto stride = indices->stride;
      auto tri_count = indices->count / 3;
      phys_tris.reserve(phys_tris.size() + tri_count);

      // Physics system always wants 32-bit indexed triangles
      auto *data = (uint8_t *)view->data + indices->offset;
      for (cgltf_size t = 0; t < indices->count; t += 3) {
        if (stride == 2) {
          auto t0 = (uint32_t)(*((uint16_t *)(data + ((t + 0) * stride))));
          auto t1 = (uint32_t)(*((uint16_t *)(data + ((t + 1) * stride))));
          auto t2 = (uint32_t)(*((uint16_t *)(data + ((t + 2) * stride))));
          phys_tris.push_back({t0, t1, t2});
        } else if (stride == 4) {
          auto t0 = *((uint32_t *)(data + ((t + 0) * stride)));
          auto t1 = *((uint32_t *)(data + ((t + 1) * stride)));
          auto t2 = *((uint32_t *)(data + ((t + 2) * stride)));
          phys_tris.push_back({t0, t1, t2});
        } else {
          TB_CHECK(false, "Unexpected");
        }
      }
    }
    // Vertices
    {
      uint32_t pos_idx = SDL_MAX_UINT32;
      for (cgltf_size i = 0; i < primitive.attributes_count; ++i) {
        if (primitive.attributes[i].type == cgltf_attribute_type_position) {
          pos_idx = i;
          break;
        }
      }
      TB_CHECK(pos_idx != SDL_MAX_UINT32, "Failed to find position");

      auto &positions = primitive.attributes[pos_idx].data;
      auto *view = positions->buffer_view;

      auto res = decompress_buffer_view(gp_alloc, view);
      TB_CHECK(res == cgltf_result_success, "Failed to decode buffer view");

      auto vert_count = positions->count;
      phys_verts.reserve(phys_verts.size() + vert_count);

      // Use the node's matrix to dequantize the position into something
      // the physics system can understand
      auto *data = (int16_t4 *)(&((uint8_t *)view->data)[positions->offset]);
      for (cgltf_size i = 0; i < vert_count; ++i) {
        int16_t4 quant_pos = data[i];
        float4 quant_posf = tb_f4(quant_pos.x, quant_pos.y, quant_pos.z, 1);
        float3 pos = tb_mulf44f4(dequant_mat, quant_posf).xyz;
        phys_verts.push_back(JPH::Float3(pos.x, pos.y, pos.z));
      }
    }
  }

  JPH::MeshShapeSettings settings(phys_verts, phys_tris);
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
            } else if (SDL_strcmp(shape_type_str, "cylinder") == 0) {
              shape_type = JPH::EShapeSubType::Cylinder;
              break;
            } else if (SDL_strcmp(shape_type_str, "mesh") == 0) {
              shape_type = JPH::EShapeSubType::Mesh;
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
      } else if (shape_type == JPH::EShapeSubType::Cylinder) {
        float half_height = 0.5f;
        float radius = 0.0f;
        json_object_object_foreach(extra, key, value) {
          if (SDL_strcmp(key, "cylinder_half_height") == 0) {
            radius = (float)json_object_get_double(value);
          }
          if (SDL_strcmp(key, "cylinder_radius") == 0) {
            radius = (float)json_object_get_double(value);
          }
        }
        shape = create_cylinder_shape(half_height, radius);
      } else if (shape_type == JPH::EShapeSubType::Mesh) {
        // HACK:
        // This is stupid.. get the standard allocator from some singleton
        auto *mesh_sys = ecs.get_mut<TbMeshSystem>();
        shape = create_mesh_shape(mesh_sys->std_alloc, node);
      } else {
        TB_CHECK(false, "Invalid physics shape");
      }

      bool sensor = false;
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
          } else if (SDL_strcmp(key, "sensor") == 0) {
            sensor = (bool)json_object_get_boolean(value);
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
          } else if (SDL_strcmp(key, "trans_y") == 0) {
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
      body_settings.mIsSensor = sensor;
      body_settings.mUserData = (uint64_t)e;
      body_settings.mOverrideMassProperties =
          JPH::EOverrideMassProperties::CalculateInertia;
      body_settings.mMassPropertiesOverride.mMass = 1.0f;

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
  flecs::entity ent = ecs.entity(e);

  if (!ent.has<TbRigidbodyComponent>() || !ent.has<TbTransformComponent>()) {
    return;
  }

  auto *phys_sys = ecs.get_mut<TbPhysicsSystem>();
  auto *jolt = (JPH::PhysicsSystem *)(phys_sys->jolt_phys);
  auto &bodies = jolt->GetBodyInterface();

  auto rb = ent.get_mut<TbRigidbodyComponent>();

  // Set the body position and rotation based on the final world transform
  // of the entity.
  auto world_trans = tb_transform_get_world_trans(ecs.c_ptr(), e);
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

  // Is there a better way to avoid having to use this macro in C++?
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
  ECS_COMPONENT_DEFINE(world->ecs, TbRigidbodyComponent);
#pragma clang diagnostic pop

  TbAssetSystem asset = {
      .add_fn = create_rigidbody_component,
      .post_load_fn = post_load_rigidbody_component,
      .rem_fn = remove_rigidbody_components,
  };

  // Sets the TbAssetSystem component on the TbPhysicsSystem singleton entity
  ecs.entity<TbPhysicsSystem>().set<TbAssetSystem>(asset);
}
