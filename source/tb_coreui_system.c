#include "tb_coreui_system.h"

#include "tb_common.h"
#include "tb_imgui.h"
#include "tb_imgui_system.h"
#include "tb_profiling.h"
#include "tbengineconfig.h"

ECS_COMPONENT_DECLARE(TbCoreUISystem);

typedef struct TbCoreUIMenu {
  bool *active;
  const char *name;
} TbCoreUIMenu;

void tb_register_core_ui_sys(TbWorld *world);
void tb_unregister_core_ui_sys(TbWorld *world);

TB_REGISTER_SYS(tb, core_ui, TB_COREUI_SYS_PRIO)

TbCoreUISystem create_coreui_system(TbAllocator gp_alloc, TbAllocator tmp_alloc,
                                    TbImGuiSystem *imgui_system) {
  TbCoreUISystem sys = {
      .gp_alloc = gp_alloc,
      .tmp_alloc = tmp_alloc,
      .imgui = imgui_system,
  };

  TB_DYN_ARR_RESET(sys.menu_registry, gp_alloc, 1);

  sys.metrics = tb_coreui_register_menu(&sys, "Metrics");
  sys.about = tb_coreui_register_menu(&sys, "About");

  return sys;
}

void destroy_coreui_system(TbCoreUISystem *self) {
  // Clean up registry
  TB_DYN_ARR_FOREACH(self->menu_registry, i) {
    TbCoreUIMenu menu = TB_DYN_ARR_AT(self->menu_registry, i);
    tb_free(self->gp_alloc, menu.active);
  }
  TB_DYN_ARR_DESTROY(self->menu_registry);
  *self = (TbCoreUISystem){0};
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
  TbCoreUISystem *sys = ecs_field(it, TbCoreUISystem, 1);

  if (sys->imgui->context_count > 0) {
    const TbUIContext *ui_ctx = &sys->imgui->contexts[0];
    igSetCurrentContext(ui_ctx->context);

    if (igBeginMainMenuBar()) {
      igBringWindowToDisplayFront(igGetCurrentWindow());
      TB_DYN_ARR_FOREACH(sys->menu_registry, i) {
        TbCoreUIMenu *menu = &TB_DYN_ARR_AT(sys->menu_registry, i);
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
  TbCoreUISystem *sys = ecs_field(it, TbCoreUISystem, 1);
  destroy_coreui_system(sys);
}

void tb_register_core_ui_sys(TbWorld *world) {
  TracyCZoneN(ctx, "Register Core UI Sys", true);
  ecs_world_t *ecs = world->ecs;

  ECS_COMPONENT_DEFINE(ecs, TbCoreUISystem);

  TbImGuiSystem *imgui_sys = ecs_singleton_get_mut(ecs, TbImGuiSystem);
  TbCoreUISystem sys =
      create_coreui_system(world->gp_alloc, world->tmp_alloc, imgui_sys);

  // Sets a singleton based on the value at a pointer
  ecs_set_ptr(ecs, ecs_id(TbCoreUISystem), TbCoreUISystem, &sys);

  ECS_SYSTEM(ecs, coreui_update_tick, EcsOnUpdate,
             TbCoreUISystem(TbCoreUISystem));
  TracyCZoneEnd(ctx);
}

void tb_unregister_core_ui_sys(TbWorld *world) {
  ecs_world_t *ecs = world->ecs;
  TbCoreUISystem *sys = ecs_singleton_get_mut(ecs, TbCoreUISystem);
  *sys = (TbCoreUISystem){0};
  ecs_singleton_remove(ecs, TbCoreUISystem);
}

bool *tb_coreui_register_menu(TbCoreUISystem *self, const char *name) {
  // Store the bool on the heap so it survives registry resizes
  bool *active = tb_alloc_tp(self->gp_alloc, bool);
  TbCoreUIMenu menu = {
      .active = active,
      .name = name,
  };
  TB_DYN_ARR_APPEND(self->menu_registry, menu);
  return active;
}
