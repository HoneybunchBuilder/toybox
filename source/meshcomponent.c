#include "meshcomponent.h"

#include "simd.h"

#include "assetsystem.h"
#include "common.hlsli"
#include "json.h"
#include "materialsystem.h"
#include "meshsystem.h"
#include "profiling.h"
#include "renderobjectsystem.h"
#include "tbgltf.h"

#include <flecs.h>

bool create_mesh_component_internal(MeshComponent *self, TbMeshId id,
                                    const char *source_path,
                                    const cgltf_node *node,
                                    MeshSystem *mesh_system,
                                    MaterialSystem *mat_system) {
  const uint32_t submesh_count = node->mesh->primitives_count;
  TB_CHECK_RETURN(submesh_count <= TB_SUBMESH_MAX, "Too many submeshes", false);

  *self = (MeshComponent){
      .mesh_id = id,
      .submesh_count = submesh_count,
  };

  // Retrieve info about how to draw this mesh
  uint64_t offset = 0;
  {
    TracyCZoneN(prim_ctx, "load primitives", true);
    for (uint32_t prim_idx = 0; prim_idx < submesh_count; ++prim_idx) {
      const cgltf_primitive *prim = &node->mesh->primitives[prim_idx];
      const cgltf_accessor *indices = prim->indices;

      VkIndexType index_type = VK_INDEX_TYPE_UINT16;
      if (indices->stride > 2) {
        index_type = VK_INDEX_TYPE_UINT32;
      }

      self->submeshes[prim_idx].index_type = index_type;
      self->submeshes[prim_idx].index_count = indices->count;
      self->submeshes[prim_idx].index_offset = offset;
      self->submeshes[prim_idx].vertex_count = prim->attributes[0].data->count;

      // If no material is provided we use a default
      const cgltf_material *material = prim->material;
      if (material == NULL) {
        material = mat_system->default_material;
      }

      // Load materials
      self->submeshes[prim_idx].material =
          tb_mat_system_load_material(mat_system, source_path, material);
      TB_CHECK_RETURN(self->submeshes[prim_idx].material,
                      "Failed to load material", false);

      offset += (indices->count * indices->stride);
    }
    // Provide padding between vertex and index sections of the buffer
    // to ensure alignment is correct
    offset += offset % (sizeof(uint16_t) * 4);

    // While we determine the vertex offset we'll also calculate the local space
    // AABB for this mesh across all primitives
    self->local_aabb = aabb_init();

    // Determine the vertex offset for each primitive
    for (uint32_t prim_idx = 0; prim_idx < submesh_count; ++prim_idx) {
      const cgltf_primitive *prim = &node->mesh->primitives[prim_idx];

      self->submeshes[prim_idx].vertex_offset = offset;

      uint64_t vertex_attributes = 0;

      // Determine input permutation and attribute count
      uint32_t attrib_count = 0;
      for (cgltf_size attr_idx = 0; attr_idx < prim->attributes_count;
           ++attr_idx) {
        cgltf_attribute_type type = prim->attributes[attr_idx].type;
        int32_t index = prim->attributes[attr_idx].index;
        if ((type == cgltf_attribute_type_position ||
             type == cgltf_attribute_type_normal ||
             type == cgltf_attribute_type_tangent ||
             type == cgltf_attribute_type_texcoord) &&
            index == 0) {
          if (type == cgltf_attribute_type_position) {
            vertex_attributes |= VA_INPUT_PERM_POSITION;
          } else if (type == cgltf_attribute_type_normal) {
            vertex_attributes |= VA_INPUT_PERM_NORMAL;
          } else if (type == cgltf_attribute_type_tangent) {
            vertex_attributes |= VA_INPUT_PERM_TANGENT;
          } else if (type == cgltf_attribute_type_texcoord) {
            vertex_attributes |= VA_INPUT_PERM_TEXCOORD0;
          }

          attrib_count++;
        }
      }

      // Reorder attributes
      uint32_t *attr_order =
          tb_alloc(mesh_system->tmp_alloc, sizeof(uint32_t) * attrib_count);
      for (uint32_t attr_idx = 0; attr_idx < (uint32_t)prim->attributes_count;
           ++attr_idx) {
        cgltf_attribute_type attr_type = prim->attributes[attr_idx].type;
        int32_t idx = prim->attributes[attr_idx].index;
        if (attr_type == cgltf_attribute_type_position) {
          attr_order[0] = attr_idx;
        } else if (attr_type == cgltf_attribute_type_normal) {
          attr_order[1] = attr_idx;
        } else if (attr_type == cgltf_attribute_type_tangent) {
          attr_order[2] = attr_idx;
        } else if (attr_type == cgltf_attribute_type_texcoord && idx == 0) {
          if (vertex_attributes & VA_INPUT_PERM_TANGENT) {
            attr_order[3] = attr_idx;
          } else {
            attr_order[2] = attr_idx;
          }
        }
      }

      // Read AABB from gltf
      {
        const cgltf_attribute *pos_attr = &prim->attributes[attr_order[0]];

        TB_CHECK(pos_attr->type == cgltf_attribute_type_position,
                 "Unexpected vertex attribute type");

        float *min = pos_attr->data->min;
        float *max = pos_attr->data->max;

        aabb_add_point(&self->local_aabb, (float3){min[0], min[1], min[2]});
        aabb_add_point(&self->local_aabb, (float3){max[0], max[1], max[2]});
      }

      // Decode vertex attributes into full vertex input layouts
      TbVertexInput vertex_input = VI_Count;
      {
        if (vertex_attributes & VA_INPUT_PERM_POSITION) {
          if (vertex_attributes & VA_INPUT_PERM_NORMAL) {
            vertex_input = VI_P3N3;
            if (vertex_attributes & VA_INPUT_PERM_TEXCOORD0) {
              vertex_input = VI_P3N3U2;
              if (vertex_attributes & VA_INPUT_PERM_TANGENT) {
                vertex_input = VI_P3N3T4U2;
              }
            }
          }
        }
      }
      TB_CHECK_RETURN(vertex_input < VI_Count, "Unexpected vertex input",
                      false);

      self->submeshes[prim_idx].vertex_input = vertex_input;

      for (cgltf_size attr_idx = 0; attr_idx < attrib_count; ++attr_idx) {
        cgltf_attribute *attr = &prim->attributes[attr_order[attr_idx]];
        cgltf_accessor *accessor = attr->data;

        offset += accessor->stride * accessor->count;
      }
    }

    TracyCZoneEnd(prim_ctx);
  }
  return true;
}

void destroy_mesh_component_internal(MeshComponent *self,
                                     MeshSystem *mesh_system,
                                     MaterialSystem *mat_system) {
  for (uint32_t i = 0; i < self->submesh_count; ++i) {
    tb_mat_system_release_material_ref(mat_system, self->submeshes[i].material);
  }
  tb_mesh_system_release_mesh_ref(mesh_system, self->mesh_id);

  *self = (MeshComponent){0};
}

bool create_mesh_component(ecs_world_t *ecs, ecs_entity_t e,
                           const char *source_path, const cgltf_node *node,
                           json_object *extra) {
  (void)extra;
  bool ret = true;
  if (node->mesh) {
    ECS_COMPONENT(ecs, MeshSystem);
    ECS_COMPONENT(ecs, MaterialSystem);
    ECS_COMPONENT(ecs, RenderObjectSystem);
    ECS_COMPONENT(ecs, RenderObject);
    ECS_COMPONENT(ecs, MeshComponent);
    ECS_TAG(ecs, MeshRenderObject);

    MeshSystem *mesh_sys = ecs_singleton_get_mut(ecs, MeshSystem);
    MaterialSystem *mat_sys = ecs_singleton_get_mut(ecs, MaterialSystem);

    /*
        We want everything to be as instanced as possible but we can't guarantee
      that gltfpack will be able to construct perfect instanced batches for us.

      Instead we check the node before we decide how to create a component.
        If we already have a component that uses this mesh id, we instead
      append it and its material to an existing component.
        If there is no existing component that uses this mesh, create one.
      There may be components with only 1 instance; that is okay.

      Thoughts on Node Mobility
        If the user can mark up in Blender which nodes are stationary that info
      can be used to organize meshes into different components based on whether
      or not we expect the transform to ever update. That way we only have to
      shuffle transforms to the GPU for the set of transforms that actually
      could possibly be updated.
    */

    // Load mesh
    TbMeshId id = tb_mesh_system_load_mesh(mesh_sys, source_path, node);
    TB_CHECK_RETURN(id != InvalidMeshId, "Failed to load mesh", false);

    // Find Mesh Component
    ecs_entity_t mesh_ent = 0;
    MeshComponent *mesh_comp = NULL;
    ecs_iter_t mesh_it = ecs_query_iter(ecs, mesh_sys->mesh_query);
    while (ecs_query_next(&mesh_it)) {
      MeshComponent *meshes = ecs_field(&mesh_it, MeshComponent, 1);
      for (int32_t mesh_idx = 0; mesh_idx < mesh_it.count; ++mesh_idx) {
        MeshComponent *mesh = &meshes[mesh_idx];
        if (mesh->mesh_id == id) {
          mesh_ent = mesh_it.entities[mesh_idx];
          mesh_comp = mesh;
          break;
        }
      }
    }
    if (mesh_comp == NULL) {
      MeshComponent comp = {0};
      ret = create_mesh_component_internal(&comp, id, source_path, node,
                                           mesh_sys, mat_sys);
      TB_DYN_ARR_RESET(comp.entities, mesh_sys->std_alloc, 16);
      ecs_set_ptr(ecs, e, MeshComponent, &comp);

      mesh_comp = ecs_get_mut(ecs, e, MeshComponent);
      mesh_ent = e;
    }

    // Add a RenderObject to this entity
    ecs_set(ecs, e, RenderObject, {0});
    ecs_add_id(ecs, e, MeshRenderObject);

    // And let the mesh component know about this entity
    TB_DYN_ARR_APPEND(mesh_comp->entities, e);
    ecs_set_ptr(ecs, mesh_ent, MeshComponent, mesh_comp);
  }
  return ret;
}

void destroy_mesh_component(ecs_world_t *ecs) {
  ECS_COMPONENT(ecs, MeshSystem);
  ECS_COMPONENT(ecs, MaterialSystem);
  ECS_COMPONENT(ecs, MeshComponent);

  // Remove mesh component from entities
  ecs_filter_t *filter =
      ecs_filter(ecs, {
                          .terms =
                              {
                                  {.id = ecs_id(MeshComponent)},
                              },
                      });

  ecs_iter_t mesh_it = ecs_filter_iter(ecs, filter);
  while (ecs_filter_next(&mesh_it)) {
    MeshComponent *mesh = ecs_field(&mesh_it, MeshComponent, 1);
    MeshSystem *mesh_sys = ecs_singleton_get_mut(ecs, MeshSystem);
    MaterialSystem *mat_sys = ecs_singleton_get_mut(ecs, MaterialSystem);

    for (int32_t i = 0; i < mesh_it.count; ++i) {
      destroy_mesh_component_internal(&mesh[i], mesh_sys, mat_sys);
    }

    ecs_singleton_modified(ecs, MeshSystem);
    ecs_singleton_modified(ecs, MaterialSystem);
  }
  ecs_filter_fini(filter);
}

void tb_register_mesh_component(ecs_world_t *ecs) {
  ECS_COMPONENT(ecs, AssetSystem);
  ECS_COMPONENT(ecs, MeshSystem);
  // Mark the mesh system entity as also having an asset
  // system that can parse and load mesh components
  AssetSystem asset = {
      .add_fn = create_mesh_component,
      .rem_fn = destroy_mesh_component,
  };
  ecs_set_ptr(ecs, ecs_id(MeshSystem), AssetSystem, &asset);
}
