#include "meshcomponent.h"

#include "simd.h"

#include "cgltf.h"
#include "common.hlsli"
#include "meshsystem.h"
#include "world.h"

bool create_mesh_component(MeshComponent *self,
                           const MeshComponentDescriptor *desc,
                           uint32_t system_dep_count,
                           System *const *system_deps) {
  // Ensure we have a reference to the mesh system
  MeshSystem *mesh_system = (MeshSystem *)tb_find_system_dep_self_by_id(
      system_deps, system_dep_count, MeshSystemId);
  TB_CHECK_RETURN(mesh_system, "Failed to get mesh system reference", false);

  TbMeshId id =
      tb_mesh_system_load_mesh(mesh_system, desc->source_path, desc->mesh);
  TB_CHECK_RETURN(id != InvalidMeshId, "Failed to load mesh", false);

  VkBuffer geom_buffer = tb_mesh_system_get_gpu_mesh(mesh_system, id);
  TB_CHECK_RETURN(geom_buffer != VK_NULL_HANDLE, "Failed to reference mesh",
                  false);

  const uint32_t submesh_count = desc->mesh->primitives_count;
  SubMesh *submeshes =
      tb_alloc_nm_tp(mesh_system->std_alloc, submesh_count, SubMesh);

  // Retrieve info about how to draw this mesh
  uint64_t offset = 0;
  {
    for (uint32_t prim_idx = 0; prim_idx < submesh_count; ++prim_idx) {
      const cgltf_primitive *prim = &desc->mesh->primitives[prim_idx];
      const cgltf_accessor *indices = prim->indices;

      submeshes[prim_idx].index_count = indices->count;
      submeshes[prim_idx].index_offset = offset;

      offset += indices->buffer_view->size;
    }
    // Provide padding between vertex and index sections of the buffer
    // to ensure alignment is correct
    offset += offset % (sizeof(float) * 3);

    // Determine the vertex offset for each primitive
    for (uint32_t prim_idx = 0; prim_idx < submesh_count; ++prim_idx) {
      const cgltf_primitive *prim = &desc->mesh->primitives[prim_idx];

      submeshes[prim_idx].vertex_offset = offset;

      // Determine input permutation and attribute count
      uint64_t input_perm = 0;
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
            input_perm |= VA_INPUT_PERM_POSITION;
          } else if (type == cgltf_attribute_type_normal) {
            input_perm |= VA_INPUT_PERM_NORMAL;
          } else if (type == cgltf_attribute_type_tangent) {
            input_perm |= VA_INPUT_PERM_TANGENT;
          } else if (type == cgltf_attribute_type_texcoord) {
            input_perm |= VA_INPUT_PERM_TEXCOORD0;
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
          if (input_perm & VA_INPUT_PERM_TANGENT) {
            attr_order[3] = attr_idx;
          } else {
            attr_order[2] = attr_idx;
          }
        }
      }

      for (cgltf_size attr_idx = 0; attr_idx < attrib_count; ++attr_idx) {
        cgltf_attribute *attr = &prim->attributes[attr_order[attr_idx]];
        cgltf_accessor *accessor = attr->data;

        size_t attr_size = accessor->stride * accessor->count;

        offset += attr_size;
      }
    }
  }

  *self = (MeshComponent){
      .mesh_id = id,
      .geom_buffer = geom_buffer,
      .submesh_count = submesh_count,
      .submeshes = submeshes,
  };

  return true;
}

void destroy_mesh_component(MeshComponent *self, uint32_t system_dep_count,
                            System *const *system_deps) {
  // Ensure we have a reference to the mesh system
  MeshSystem *mesh_system = (MeshSystem *)tb_find_system_dep_self_by_id(
      system_deps, system_dep_count, MeshSystemId);
  TB_CHECK(mesh_system, "Failed to get mesh system reference");

  tb_mesh_system_release_mesh_ref(mesh_system, self->mesh_id);

  tb_free(mesh_system->std_alloc, self->submeshes);

  *self = (MeshComponent){0};
}

TB_DEFINE_COMPONENT(mesh, MeshComponent, MeshComponentDescriptor)

void tb_mesh_component_descriptor(ComponentDescriptor *desc) {
  *desc = (ComponentDescriptor){
      .name = "Mesh",
      .size = sizeof(MeshComponent),
      .id = MeshComponentId,
      .system_dep_count = 1,
      .system_deps[0] = MeshSystemId,
      .create = tb_create_mesh_component,
      .destroy = tb_destroy_mesh_component,
  };
}
