#include "meshcomponent.h"

#include "simd.h"

#include "common.hlsli"
#include "materialsystem.h"
#include "meshsystem.h"
#include "profiling.h"
#include "renderobjectsystem.h"
#include "tbgltf.h"
#include "world.h"

#include <flecs.h>

bool create_mesh_component_internal(MeshComponent *self,
                                    const char *source_path,
                                    const cgltf_node *node,
                                    MeshSystem *mesh_system,
                                    MaterialSystem *mat_system,
                                    RenderObjectSystem *render_object_system) {
  TbMeshId id = tb_mesh_system_load_mesh(mesh_system, source_path, node);
  TB_CHECK_RETURN(id != InvalidMeshId, "Failed to load mesh", false);

  TbRenderObjectId obj_id =
      tb_render_object_system_create(render_object_system);

  const uint32_t submesh_count = node->mesh->primitives_count;
  TB_CHECK_RETURN(submesh_count <= TB_SUBMESH_MAX, "Too many submeshes", false);

  *self = (MeshComponent){
      .mesh_id = id,
      .object_id = obj_id,
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

bool create_mesh_component(MeshComponent *self,
                           const MeshComponentDescriptor *desc,
                           uint32_t system_dep_count,
                           System *const *system_deps) {
  TracyCZoneN(ctx, "Create Mesh Component", true);
  // Ensure we have a reference to the necessary systems
  MeshSystem *mesh_system =
      tb_get_system(system_deps, system_dep_count, MeshSystem);
  TB_CHECK_RETURN(mesh_system, "Failed to get mesh system reference", false);
  MaterialSystem *mat_system =
      tb_get_system(system_deps, system_dep_count, MaterialSystem);
  TB_CHECK_RETURN(mat_system, "Failed to get material system reference", false);
  RenderObjectSystem *render_object_system =
      tb_get_system(system_deps, system_dep_count, RenderObjectSystem);
  TB_CHECK_RETURN(render_object_system,
                  "Failed to get render object system reference", false);

  bool ret = create_mesh_component_internal(self, desc->source_path, desc->node,
                                            mesh_system, mat_system,
                                            render_object_system);

  TracyCZoneEnd(ctx);
  return ret;
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

void destroy_mesh_component(MeshComponent *self, uint32_t system_dep_count,
                            System *const *system_deps) {
  // Ensure we have a reference to the required systems
  MeshSystem *mesh_system = (MeshSystem *)tb_find_system_dep_self_by_id(
      system_deps, system_dep_count, MeshSystemId);
  TB_CHECK(mesh_system, "Failed to get mesh system reference");
  MaterialSystem *mat_system = (MaterialSystem *)tb_find_system_dep_self_by_id(
      system_deps, system_dep_count, MaterialSystemId);
  TB_CHECK(mat_system, "Failed to get material system reference");

  destroy_mesh_component_internal(self, mesh_system, mat_system);
}

TB_DEFINE_COMPONENT(mesh, MeshComponent, MeshComponentDescriptor)

void tb_mesh_component_descriptor(ComponentDescriptor *desc) {
  *desc = (ComponentDescriptor){
      .name = "Mesh",
      .size = sizeof(MeshComponent),
      .id = MeshComponentId,
      .system_dep_count = 3,
      .system_deps[0] = MeshSystemId,
      .system_deps[1] = MaterialSystemId,
      .system_deps[2] = RenderObjectSystemId,
      .create = tb_create_mesh_component,
      .destroy = tb_destroy_mesh_component,
  };
}

bool tb_create_mesh_component2(ecs_world_t *ecs, ecs_entity_t e,
                               const char *source_path, cgltf_node *node,
                               json_object *extra) {
  (void)extra;
  bool ret = true;
  if (node->mesh) {
    ECS_COMPONENT(ecs, MeshSystem);
    ECS_COMPONENT(ecs, MaterialSystem);
    ECS_COMPONENT(ecs, RenderObjectSystem);
    ECS_COMPONENT(ecs, MeshComponent);

    MeshSystem *mesh_sys = ecs_singleton_get_mut(ecs, MeshSystem);
    MaterialSystem *mat_sys = ecs_singleton_get_mut(ecs, MaterialSystem);
    RenderObjectSystem *ro_sys = ecs_singleton_get_mut(ecs, RenderObjectSystem);

    MeshComponent comp = {0};
    ret = create_mesh_component_internal(&comp, source_path, node, mesh_sys,
                                         mat_sys, ro_sys);
    ecs_set_ptr(ecs, e, MeshComponent, &comp);
  }
  return ret;
}

void tb_destroy_mesh_component2(ecs_world_t *ecs) {
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
    MeshSystem *mesh_sys = ecs_singleton_get_mut(mesh_it.world, MeshSystem);
    MaterialSystem *mat_sys =
        ecs_singleton_get_mut(mesh_it.world, MaterialSystem);

    for (int32_t i = 0; i < mesh_it.count; ++i) {
      destroy_mesh_component_internal(&mesh[i], mesh_sys, mat_sys);
    }

    ecs_singleton_modified(mesh_it.world, MeshSystem);
    ecs_singleton_modified(mesh_it.world, MaterialSystem);
  }
  ecs_filter_fini(filter);
}
