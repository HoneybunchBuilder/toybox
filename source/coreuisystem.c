#include "coreuisystem.h"

#include "imguisystem.h"
#include "profiling.h"
#include "tbcommon.h"
#include "tbengineconfig.h"
#include "tbimgui.h"

#include <flecs.h>

typedef struct CoreUIMenu {
  bool *active;
  const char *name;
} CoreUIMenu;

CoreUISystem create_coreui_system(TbAllocator std_alloc, TbAllocator tmp_alloc,
                                  ImGuiSystem *imgui_system) {
  CoreUISystem sys = {
      .std_alloc = std_alloc,
      .tmp_alloc = tmp_alloc,
      .imgui = imgui_system,
  };

  TB_DYN_ARR_RESET(sys.menu_registry, std_alloc, 1);

  sys.metrics = tb_coreui_register_menu(&sys, "Metrics");
  sys.about = tb_coreui_register_menu(&sys, "About");

  return sys;
}

void destroy_coreui_system(CoreUISystem *self) {
  // Clean up registry
  TB_DYN_ARR_FOREACH(self->menu_registry, i) {
    CoreUIMenu menu = TB_DYN_ARR_AT(self->menu_registry, i);
    tb_free(self->std_alloc, menu.active);
  }
  TB_DYN_ARR_DESTROY(self->menu_registry);
  *self = (CoreUISystem){0};
}

void coreui_show_about(bool *open) {
  if (igBegin("About Toybox", open, 0)) {
    igText("Version: %s", TB_ENGINE_VERSION);
    igText("Platform: %s", TB_PLATFORM);
    igText("Arch: %s", TB_ARCH);
    igText("Git Hash: %s", GIT_COMMIT_HASH);
    igEnd();
  }
}

void coreui_update_tick(ecs_iter_t *it) {
  TracyCZoneNC(ctx, "Core UI System Tick", TracyCategoryColorUI, true);
  CoreUISystem *sys = ecs_field(it, CoreUISystem, 1);

  if (sys->imgui->context_count > 0) {
    const UIContext *ui_ctx = &sys->imgui->contexts[0];
    igSetCurrentContext(ui_ctx->context);

    if (igBeginMainMenuBar()) {
      TB_DYN_ARR_FOREACH(sys->menu_registry, i) {
        CoreUIMenu *menu = &TB_DYN_ARR_AT(sys->menu_registry, i);
        if (igBeginMenu(menu->name, true)) {
          *menu->active = !*menu->active;
          igEndMenu();
        }
      }
      igEndMainMenuBar();
    }

    if (*sys->about) {
      coreui_show_about(sys->about);
    }
    if (*sys->metrics) {
      igShowMetricsWindow(sys->metrics);
    }
  }

  TracyCZoneEnd(ctx);
}

void destroy_core_ui_sys(ecs_iter_t *it) {
  CoreUISystem *sys = ecs_field(it, CoreUISystem, 1);
  destroy_coreui_system(sys);
}

void tb_register_core_ui_sys(ecs_world_t *ecs, TbAllocator std_alloc,
                             TbAllocator tmp_alloc) {
  ECS_COMPONENT(ecs, ImGuiSystem);
  ECS_COMPONENT(ecs, CoreUISystem);

  ImGuiSystem *imgui_sys = ecs_singleton_get_mut(ecs, ImGuiSystem);
  CoreUISystem sys = create_coreui_system(std_alloc, tmp_alloc, imgui_sys);

  // Sets a singleton based on the value at a pointer
  ecs_set_ptr(ecs, ecs_id(CoreUISystem), CoreUISystem, &sys);

  ECS_SYSTEM(ecs, coreui_update_tick, EcsOnUpdate, CoreUISystem(CoreUISystem));
}

void tb_unregister_core_ui_sys(ecs_world_t *ecs) {
  ECS_COMPONENT(ecs, CoreUISystem);
  CoreUISystem *sys = ecs_singleton_get_mut(ecs, CoreUISystem);
  *sys = (CoreUISystem){0};
  ecs_singleton_remove(ecs, CoreUISystem);
}

bool *tb_coreui_register_menu(CoreUISystem *self, const char *name) {
  // Store the bool on the heap so it survives registry resizes
  bool *active = tb_alloc_tp(self->std_alloc, bool);
  CoreUIMenu menu = {
      .active = active,
      .name = name,
  };
  TB_DYN_ARR_APPEND(self->menu_registry, menu);
  return active;
}
