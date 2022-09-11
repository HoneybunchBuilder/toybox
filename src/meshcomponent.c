#include "meshcomponent.h"

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

  *self = (MeshComponent){
      .index_count = 0,
      .index_offset = 0,
      .vertex_offset = 0,
  };
}

void destroy_mesh_component(MeshComponent *self) { *self = (MeshComponent){0}; }

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
