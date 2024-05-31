#include "tb_mesh_component.h"

#include "tb_simd.h"

#include "common.hlsli"
#include "json.h"
#include "tb_assets.h"
#include "tb_gltf.h"
#include "tb_material_system.h"
#include "tb_mesh_system.h"
#include "tb_mesh_system2.h"
#include "tb_profiling.h"
#include "tb_render_object_system.h"
#include "tb_util.h"
#include "tb_world.h"

ECS_COMPONENT_DECLARE(TbMeshComponent);

bool create_mesh_component_internal(ecs_world_t *ecs, TbMeshComponent *self,
                                    TbMeshId id, TbAllocator gp_alloc,
                                    const char *source_path,
                                    const cgltf_node *node) {
  const uint32_t submesh_count = node->mesh->primitives_count;

  *self = (TbMeshComponent){
      .mesh_id = id,
  };
  TB_DYN_ARR_RESET(self->submeshes, gp_alloc, submesh_count);
  TB_DYN_ARR_RESIZE(self->submeshes, submesh_count);

  // Retrieve info about how to draw this mesh
  uint64_t offset = 0;
  {
    TracyCZoneN(prim_ctx, "load primitives", true);
    for (uint32_t prim_idx = 0; prim_idx < submesh_count; ++prim_idx) {
      const cgltf_primitive *prim = &node->mesh->primitives[prim_idx];
      const cgltf_accessor *indices = prim->indices;

      tb_auto submesh = &TB_DYN_ARR_AT(self->submeshes, prim_idx);

      VkIndexType index_type = VK_INDEX_TYPE_UINT16;
      if (indices->stride > 2) {
        index_type = VK_INDEX_TYPE_UINT32;
      }

      submesh->index_type = index_type;
      submesh->index_count = indices->count;
      submesh->index_offset = offset;
      submesh->vertex_count = prim->attributes[0].data->count;

      // If no material is provided we use a default
      const cgltf_material *material = prim->material;
      if (material == NULL) {
        submesh->material = tb_get_default_mat(ecs, TB_MAT_USAGE_SCENE);
      } else {
        submesh->material = tb_mat_sys_load_gltf_mat(
            ecs, source_path, material->name, TB_MAT_USAGE_SCENE);
      }

      // calculate the aligned size
      size_t index_size =
          tb_calc_aligned_size(indices->count, indices->stride, 16);
      // calculate number of indices that represent that aligned size
      offset += (index_size / indices->stride);
    }

    // While we determine the vertex offset we'll also calculate the local space
    // TbAABB for this mesh across all primitives
    self->local_aabb = tb_aabb_init();

    // Determine the vertex offset for each primitive
    uint32_t vertex_offset = 0;
    for (uint32_t prim_idx = 0; prim_idx < submesh_count; ++prim_idx) {
      const cgltf_primitive *prim = &node->mesh->primitives[prim_idx];
      tb_auto submesh = &TB_DYN_ARR_AT(self->submeshes, prim_idx);

      submesh->vertex_offset = vertex_offset;
      vertex_offset += prim->attributes[0].data->count;

      // Determine input permutation and attribute count
      uint64_t vertex_attributes = 0;
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
            vertex_attributes |= TB_INPUT_PERM_POSITION;
          } else if (type == cgltf_attribute_type_normal) {
            vertex_attributes |= TB_INPUT_PERM_NORMAL;
          } else if (type == cgltf_attribute_type_tangent) {
            vertex_attributes |= TB_INPUT_PERM_TANGENT;
          } else if (type == cgltf_attribute_type_texcoord) {
            vertex_attributes |= TB_INPUT_PERM_TEXCOORD0;
          }
        }
      }
      submesh->vertex_perm = vertex_attributes;

      // Read AABB from gltf
      {
        const cgltf_attribute *pos_attr = NULL;
        // Find position attribute
        for (size_t i = 0; i < prim->attributes_count; ++i) {
          tb_auto attr = &prim->attributes[i];
          if (attr->type == cgltf_attribute_type_position) {
            pos_attr = attr;
            break;
          }
        }

        TB_CHECK(pos_attr, "Expected a position attribute");
        TB_CHECK(pos_attr->type == cgltf_attribute_type_position,
                 "Unexpected vertex attribute type");

        float *min = pos_attr->data->min;
        float *max = pos_attr->data->max;

        tb_aabb_add_point(&self->local_aabb, (float3){min[0], min[1], min[2]});
        tb_aabb_add_point(&self->local_aabb, (float3){max[0], max[1], max[2]});
      }
    }

    TracyCZoneEnd(prim_ctx);
  }
  return true;
}

void destroy_mesh_component_internal(TbMeshComponent *self,
                                     TbMeshSystem *mesh_system) {
  TB_DYN_ARR_FOREACH(self->submeshes, i) {
    // TODO: Release materials
  }
  TB_DYN_ARR_DESTROY(self->submeshes);
  tb_mesh_system_release_mesh_ref(mesh_system, self->mesh_id);

  *self = (TbMeshComponent){0};
}

bool tb_load_mesh_comp(TbWorld *world, ecs_entity_t ent,
                       const char *source_path, const cgltf_data *data,
                       const cgltf_node *node, json_object *json) {
  TracyCZoneN(ctx, "Load Mesh Component", true);
  (void)json;
  tb_auto ecs = world->ecs;
  tb_auto mesh_sys = ecs_singleton_get_mut(ecs, TbMeshSystem);

  // Load mesh
  tb_auto id = tb_mesh_system_load_mesh(mesh_sys, source_path, node);

  // Find mesh index by indexing. This is dirty but it works
  uint32_t mesh_idx = SDL_MAX_UINT32;
  for (cgltf_size i = 0; i < data->meshes_count; ++i) {
    if (&data->meshes[i] == node->mesh) {
      mesh_idx = i;
      break;
    }
  }
  TB_CHECK(mesh_idx != SDL_MAX_UINT32, "Failed to find mesh");

  TbMeshComponent comp = {
      //.mesh2 = tb_mesh_sys_load_gltf_mesh(ecs, source_path, mesh_idx),
  };
  bool ret = create_mesh_component_internal(ecs, &comp, id, world->gp_alloc,
                                            source_path, node);
  TB_CHECK(ret, "Failed to create mesh component");
  ecs_set_ptr(ecs, ent, TbMeshComponent, &comp);

  // Mark this entity as a render object
  ecs_set(ecs, ent, TbRenderObject, {0});

  TracyCZoneEnd(ctx);
  return true;
}

ecs_entity_t tb_register_mesh_comp(TbWorld *world) {
  tb_auto ecs = world->ecs;
  ECS_COMPONENT_DEFINE(ecs, TbMeshComponent);

  // Metadata for mesh component
  ecs_struct(ecs,
             {
                 .entity = ecs_id(TbMeshComponent),
                 .members =
                     {
                         {.name = "submesh_count", .type = ecs_id(ecs_u32_t)},
                     },
             });

  return 0;
}

TB_REGISTER_COMP(tb, mesh)
