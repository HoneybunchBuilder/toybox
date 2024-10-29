#include "tb_common.h"
#include "tb_imgui.h"
#include "tb_material_system.h"
#include "tb_mesh_component.h"
#include "tb_mesh_system.h"
#include "tb_system_priority.h"
#include "tb_world.h"

typedef struct TbLoadUICtx {
  bool visible;
} TbLoadUICtx;
ECS_COMPONENT_DECLARE(TbLoadUICtx);

// From tb_scene.c
typedef uint32_t TbSceneEntityCount;
typedef uint32_t TbSceneEntParseCounter;
typedef uint32_t TbSceneEntReadyCounter;
extern ECS_COMPONENT_DECLARE(TbSceneEntityCount);
extern ECS_COMPONENT_DECLARE(TbSceneEntParseCounter);
extern ECS_COMPONENT_DECLARE(TbSceneEntReadyCounter);

void tb_load_ui_tick(ecs_iter_t *it) {
  TB_TRACY_SCOPE("Load UI Tick");
  tb_auto ecs = it->world;

  if (igBegin("Loading", NULL, 0)) {

    uint64_t total_counter = 0;
    uint64_t counter = 0;

    // For each scene root
    for (int32_t i = 0; i < it->count; ++i) {
      TbScene scene = it->entities[i];
      tb_auto scene_name = ecs_get_name(ecs, scene);
      tb_auto loaded_state =
          tb_is_scene_ready(ecs, scene) ? "Ready" : "Loading";
      igText("Scene %s - : %s", scene_name, loaded_state);

      if (ecs_has(ecs, scene, TbSceneEntityCount) &&
          ecs_has(ecs, scene, TbSceneEntParseCounter) &&
          ecs_has(ecs, scene, TbSceneEntReadyCounter)) {
        tb_auto ent_count = *ecs_get(ecs, scene, TbSceneEntityCount);
        tb_auto ents_to_parse = *ecs_get(ecs, scene, TbSceneEntParseCounter);
        tb_auto ents_ready = *ecs_get(ecs, scene, TbSceneEntReadyCounter);
        if (ents_to_parse > 0) {
          igText("%d/%d to parse", ents_to_parse, ent_count);
        }
        igText("%d/%d ready", ents_ready, ent_count);

        total_counter += ent_count;
        counter += ents_ready;
      }
    }

    //  Check Mesh State
    {
      uint64_t mesh_count = 0;
      uint64_t ready_mesh_count = 0;
      tb_auto mesh_filter =
          ecs_query(ecs, {
                             .terms = {{.id = ecs_id(TbMeshComponent)}},
                         });
      tb_auto mesh_it = ecs_query_iter(ecs, mesh_filter);
      while (ecs_iter_next(&mesh_it)) {
        mesh_count += mesh_it.count;
        tb_auto mesh_comps = ecs_field(&mesh_it, TbMeshComponent, 0);
        for (int32_t i = 0; i < mesh_it.count; ++i) {
          if (tb_is_mesh_ready(ecs, mesh_comps[i].mesh2)) {
            ready_mesh_count++;
          }
        }
      }
      total_counter += mesh_count;
      counter += ready_mesh_count;
      igText("Meshes %d/%d", ready_mesh_count, mesh_count);
      ecs_query_fini(mesh_filter);
    }

    // Check Material State
    {
      uint64_t mat_count = 0;
      uint64_t ready_mat_count = 0;
      tb_auto mat_filter =
          ecs_query(ecs, {
                             .terms = {{.id = ecs_id(TbMaterialComponent)}},
                         });
      tb_auto mat_it = ecs_query_iter(ecs, mat_filter);
      while (ecs_iter_next(&mat_it)) {
        mat_count += mat_it.count;
        for (int32_t i = 0; i < mat_it.count; ++i) {
          if (tb_is_material_ready(ecs, mat_it.entities[i])) {
            ready_mat_count++;
          }
        }
      }
      total_counter += mat_count;
      counter += ready_mat_count;
      igText("Materials %d/%d", ready_mat_count, mat_count);
      ecs_query_fini(mat_filter);
    }

    // Check Texture State
    {
      uint64_t tex_count = 0;
      uint64_t ready_tex_count = 0;
      tb_auto tex_filter =
          ecs_query(ecs, {
                             .terms = {{.id = ecs_id(TbTextureComponent)}},
                         });
      tb_auto tex_it = ecs_query_iter(ecs, tex_filter);
      while (ecs_iter_next(&tex_it)) {
        tex_count += tex_it.count;
        for (int32_t i = 0; i < tex_it.count; ++i) {
          if (tb_is_texture_ready(ecs, tex_it.entities[i])) {
            ready_tex_count++;
          }
        }
      }
      total_counter += tex_count;
      counter += ready_tex_count;
      igText("Textures %d/%d", ready_tex_count, tex_count);
      ecs_query_fini(tex_filter);
    }

    if (total_counter > 0) {
      igProgressBar((float)counter / (float)total_counter, (ImVec2){0}, NULL);
    }

    igEnd();
  }
}

void tb_register_load_ui_sys(TbWorld *world) {
  tb_auto ecs = world->ecs;
  ECS_COMPONENT_DEFINE(ecs, TbLoadUICtx);

  ECS_SYSTEM(ecs, tb_load_ui_tick, EcsOnUpdate, TbSceneRoot);

  ecs_singleton_set(ecs, TbLoadUICtx, {true});
}

void tb_unregister_load_ui_sys(TbWorld *world) { (void)world; }

TB_REGISTER_SYS(tb, load_ui, TB_SYSTEM_NORMAL)
