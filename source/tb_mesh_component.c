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

bool tb_load_mesh_comp(TbWorld *world, ecs_entity_t ent,
                       const char *source_path, const cgltf_data *data,
                       const cgltf_node *node, json_object *json) {
  TracyCZoneN(ctx, "Load Mesh Component", true);
  (void)json;
  tb_auto ecs = world->ecs;

  // Find mesh index by indexing. This is dirty but it works
  uint32_t mesh_idx = SDL_MAX_UINT32;
  for (cgltf_size i = 0; i < data->meshes_count; ++i) {
    if (&data->meshes[i] == node->mesh) {
      mesh_idx = i;
      break;
    }
  }
  TB_CHECK(mesh_idx != SDL_MAX_UINT32, "Failed to find mesh");

  // We don't reserve here because we're expecting
  // all meshes to already be loading
  const uint32_t max_name_len = 256;
  char name[max_name_len] = {0};
  SDL_snprintf(name, max_name_len, "mesh_%d", mesh_idx);
  TbMeshComponent comp = {
      .mesh2 = tb_mesh_sys_load_gltf_mesh(ecs, (cgltf_data *)data, source_path,
                                          name, mesh_idx),
  };
  ecs_set_ptr(ecs, ent, TbMeshComponent, &comp);

  // Mark this entity as a render object
  ecs_set(ecs, ent, TbRenderObject, {0});

  TracyCZoneEnd(ctx);
  return true;
}

ecs_entity_t tb_register_mesh_comp(TbWorld *world) {
  tb_auto ecs = world->ecs;
  ECS_COMPONENT_DEFINE(ecs, TbMeshComponent);
  return 0;
}

TB_REGISTER_COMP(tb, mesh)
