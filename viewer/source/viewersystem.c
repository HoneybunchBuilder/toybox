#include "viewersystem.h"

#include "coreuisystem.h"
#include "profiling.h"
#include "tbcommon.h"
#include "tbimgui.h"
#ifdef TB_COOKED
#include "toybox_assetmanifest.h"
#endif
#include "world.h"

#include <flecs.h>

void viewer_update_tick(ecs_iter_t *it) {
  TracyCZoneN(ctx, "Viewer System Tick", true);
  TracyCZoneColor(ctx, TracyCategoryColorUI);

  TbViewerSystem *sys = ecs_field(it, TbViewerSystem, 1);

  if (sys->viewer_menu && *sys->viewer_menu) {
    if (igBegin("Viewer", sys->viewer_menu, 0)) {
#ifdef TB_COOKED
      // Show all scenes available in the asset registry
      const char *selected_scene =
          tb_asset_database[tb_scene_database[sys->selected_scene_idx]];
      if (igBeginCombo("Scene", selected_scene, 0)) {
        for (int32_t i = 0; i < (int32_t)tb_scene_database_num; ++i) {
          bool selected = (i == sys->selected_scene_idx);
          int32_t scene_idx = tb_scene_database[i];
          if (igSelectable_Bool(tb_asset_database[scene_idx], selected, 0,
                                (ImVec2){0, 0})) {
            sys->selected_scene_idx = i;
          }
          if (selected) {
            igSetItemDefaultFocus();
          }
        }
        igEndCombo();
      }
      sys->selected_scene = tb_asset_database[sys->selected_scene_idx];

      igSeparator();
      if (igButton("Load", (ImVec2){0, 0})) {
        sys->load_scene_signal = true;
      }
      igSameLine(0, -1);
      if (igButton("Unload", (ImVec2){0, 0})) {
        sys->unload_scene_signal = true;
      };
#else
      igText("%s", "No assets were cooked");
#endif
    }
    igEnd();
  }

  TracyCZoneEnd(ctx);
}

void tb_register_viewer_sys(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;
  ECS_COMPONENT(ecs, TbViewerSystem);
  ECS_COMPONENT(ecs, TbCoreUISystem);

  TbCoreUISystem *coreui = ecs_singleton_get_mut(ecs, TbCoreUISystem);

  TbViewerSystem sys = {
      .std_alloc = world->std_alloc,
      .tmp_alloc = world->tmp_alloc,
      .viewer_menu = tb_coreui_register_menu(coreui, "Viewer"),
      .selected_scene = 0,
  };
  // Sets a singleton based on the value at a pointer
  ecs_set_ptr(ecs, ecs_id(TbViewerSystem), TbViewerSystem, &sys);

  ECS_SYSTEM(ecs, viewer_update_tick, EcsOnUpdate, TbViewerSystem(TbViewerSystem));
}

void tb_unregister_viewer_sys(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;
  ECS_COMPONENT(ecs, TbViewerSystem);
  TbViewerSystem *sys = ecs_singleton_get_mut(ecs, TbViewerSystem);
  *sys = (TbViewerSystem){0};
  ecs_singleton_remove(ecs, TbViewerSystem);
}
