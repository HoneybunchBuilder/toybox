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

CoreUISystem create_coreui_system_internal(Allocator std_alloc,
                                           Allocator tmp_alloc,
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

bool create_coreui_system(CoreUISystem *self,
                          const CoreUISystemDescriptor *desc,
                          uint32_t system_dep_count,
                          System *const *system_deps) {
  TB_CHECK_RETURN(desc, "Invalid descriptor", false);

  ImGuiSystem *imgui_system =
      tb_get_system(system_deps, system_dep_count, ImGuiSystem);
  TB_CHECK_RETURN(imgui_system,
                  "Failed to find imgui system which coreui depends on", false);

  *self = create_coreui_system_internal(desc->std_alloc, desc->tmp_alloc,
                                        imgui_system);
  return true;
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

void tick_coreui_system_internal(CoreUISystem *self) {
  TracyCZoneN(ctx, "Core UI System Tick", true);
  TracyCZoneColor(ctx, TracyCategoryColorUI);

  if (self->imgui->context_count > 0) {
    const UIContext *ui_ctx = &self->imgui->contexts[0];
    igSetCurrentContext(ui_ctx->context);

    if (igBeginMainMenuBar()) {
      TB_DYN_ARR_FOREACH(self->menu_registry, i) {
        CoreUIMenu *menu = &TB_DYN_ARR_AT(self->menu_registry, i);
        if (igBeginMenu(menu->name, true)) {
          *menu->active = !*menu->active;
          igEndMenu();
        }
      }
      igEndMainMenuBar();
    }

    if (*self->about) {
      coreui_show_about(self->about);
    }
    if (*self->metrics) {
      igShowMetricsWindow(self->metrics);
    }
  }

  TracyCZoneEnd(ctx);
}

TB_DEFINE_SYSTEM(coreui, CoreUISystem, CoreUISystemDescriptor)

void tick_coreui_system(void *self, const SystemInput *input,
                        SystemOutput *output, float delta_seconds) {
  (void)input;
  (void)output;
  (void)delta_seconds;
  SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Tick CoreUI System");
  tick_coreui_system_internal((CoreUISystem *)self);
}

void tb_coreui_system_descriptor(SystemDescriptor *desc,
                                 const CoreUISystemDescriptor *coreui_desc) {
  *desc = (SystemDescriptor){
      .name = "CoreUI",
      .size = sizeof(CoreUISystem),
      .id = CoreUISystemId,
      .desc = (InternalDescriptor)coreui_desc,
      .system_dep_count = 1,
      .system_deps[0] = ImGuiSystemId,
      .create = tb_create_coreui_system,
      .destroy = tb_destroy_coreui_system,
      .tick_fn_count = 1,
      .tick_fns = {{
          .system_id = CoreUISystemId,
          .order = E_TICK_PRE_UI,
          .function = tick_coreui_system,
      }},
  };
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

void flecs_core_ui_tick(ecs_iter_t *it) {
  CoreUISystem *sys = ecs_field(it, CoreUISystem, 1);
  tick_coreui_system_internal(sys);
}

void destroy_core_ui_sys(ecs_iter_t *it) {
  CoreUISystem *sys = ecs_field(it, CoreUISystem, 1);
  destroy_coreui_system(sys);
}

void tb_register_core_ui_sys(ecs_world_t *ecs, Allocator std_alloc,
                             Allocator tmp_alloc) {
  ECS_COMPONENT(ecs, ImGuiSystem);
  ECS_COMPONENT(ecs, CoreUISystem);

  ImGuiSystem *imgui_sys = ecs_singleton_get_mut(ecs, ImGuiSystem);
  CoreUISystem sys =
      create_coreui_system_internal(std_alloc, tmp_alloc, imgui_sys);

  // Sets a singleton based on the value at a pointer
  ecs_set_ptr(ecs, ecs_id(CoreUISystem), CoreUISystem, &sys);

  ECS_SYSTEM(ecs, flecs_core_ui_tick, EcsOnUpdate, CoreUISystem(CoreUISystem));

  ECS_OBSERVER(ecs, destroy_core_ui_sys, EcsOnDelete,
               CoreUISystem(CoreUISystem));
}

void tb_unregister_core_ui_sys(ecs_world_t *ecs) {
  ECS_COMPONENT(ecs, CoreUISystem);
  ecs_singleton_remove(ecs, CoreUISystem);
}
