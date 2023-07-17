#include "viewersystem.h"

#include "coreuisystem.h"
#include "profiling.h"
#include "tbcommon.h"
#include "tbimgui.h"
#ifdef TB_COOKED
#include "toybox_assetmanifest.h"
#endif
#include "world.h"

bool create_viewer_system(ViewerSystem *self,
                          const ViewerSystemDescriptor *desc,
                          uint32_t system_dep_count,
                          System *const *system_deps) {
  TB_CHECK_RETURN(desc, "Invalid descriptor", false);

  CoreUISystem *coreui =
      tb_get_system(system_deps, system_dep_count, CoreUISystem);

  *self = (ViewerSystem){
      .std_alloc = desc->std_alloc,
      .tmp_alloc = desc->tmp_alloc,
      .viewer_menu = tb_coreui_register_menu(coreui, "Viewer"),
      .selected_scene = 0,
  };
  return true;
}

void destroy_viewer_system(ViewerSystem *self) { *self = (ViewerSystem){0}; }

void tick_viewer_system(ViewerSystem *self, const SystemInput *input,
                        SystemOutput *output, float delta_seconds) {
  (void)self;
  (void)input;
  (void)output;
  (void)delta_seconds;
  TracyCZoneN(ctx, "Viewer System Tick", true);
  TracyCZoneColor(ctx, TracyCategoryColorUI);

  if (self->viewer_menu && *self->viewer_menu) {
    if (igBegin("Viewer", self->viewer_menu, 0)) {
#ifdef TB_COOKED
      // Show all scenes available in the asset registry
      const char *selected_scene =
          tb_asset_database[tb_scene_database[self->selected_scene_idx]];
      if (igBeginCombo("Scene", selected_scene, 0)) {
        for (int32_t i = 0; i < (int32_t)tb_scene_database_num; ++i) {
          bool selected = (i == self->selected_scene_idx);
          int32_t scene_idx = tb_scene_database[i];
          if (igSelectable_Bool(tb_asset_database[scene_idx], selected, 0,
                                (ImVec2){0, 0})) {
            self->selected_scene_idx = i;
          }
          if (selected) {
            igSetItemDefaultFocus();
          }
        }
        igEndCombo();
      }
      self->selected_scene = tb_asset_database[self->selected_scene_idx];

      igSeparator();
      if (igButton("Load", (ImVec2){0, 0})) {
        self->load_scene_signal = true;
      }
      igSameLine(0, -1);
      if (igButton("Unload", (ImVec2){0, 0})) {
        self->unload_scene_signal = true;
      };
#else
      igText("%s", "No assets were cooked");
#endif

      igEnd();
    }
  }

  TracyCZoneEnd(ctx);
}

TB_DEFINE_SYSTEM(viewer, ViewerSystem, ViewerSystemDescriptor)

void tb_viewer_system_descriptor(SystemDescriptor *desc,
                                 const ViewerSystemDescriptor *viewer_desc) {
  *desc = (SystemDescriptor){
      .name = "Viewer",
      .size = sizeof(ViewerSystem),
      .id = ViewerSystemId,
      .desc = (InternalDescriptor)viewer_desc,
      .system_dep_count = 1,
      .system_deps[0] = CoreUISystemId,
      .create = tb_create_viewer_system,
      .destroy = tb_destroy_viewer_system,
      .tick = tb_tick_viewer_system,
  };
}
