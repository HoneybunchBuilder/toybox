#include "meshcomponent.h"

#include "cgltf.h"
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

  // TODO: Figure out how we want to clean these up
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
  }

  *self = (MeshComponent){
      .mesh_id = id,
      .geom_buffer = geom_buffer,
      .vertex_offset = offset,
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
