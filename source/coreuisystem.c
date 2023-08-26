#include "coreuisystem.h"

#include "imguisystem.h"
#include "profiling.h"
#include "tbcommon.h"
#include "tbengineconfig.h"
#include "tbimgui.h"

typedef struct CoreUIMenu {
  bool *active;
  const char *name;
} CoreUIMenu;

bool create_coreui_system(CoreUISystem *self,
                          const CoreUISystemDescriptor *desc,
                          uint32_t system_dep_count,
                          System *const *system_deps) {
  TB_CHECK_RETURN(desc, "Invalid descriptor", false);

  ImGuiSystem *imgui_system =
      tb_get_system(system_deps, system_dep_count, ImGuiSystem);
  TB_CHECK_RETURN(imgui_system,
                  "Failed to find imgui system which coreui depends on", false);

  *self = (CoreUISystem){
      .std_alloc = desc->std_alloc,
      .tmp_alloc = desc->tmp_alloc,
      .imgui = imgui_system,
  };
  TB_DYN_ARR_RESET(self->menu_registry, self->std_alloc, 1);

  self->metrics = tb_coreui_register_menu(self, "Metrics");
  self->about = tb_coreui_register_menu(self, "About");

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

void tick_coreui_system_internal(CoreUISystem *self, const SystemInput *input,
                                 SystemOutput *output, float delta_seconds) {
  (void)input;
  (void)output;
  (void)delta_seconds;

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

void tick_coreui_system(CoreUISystem *self, const SystemInput *input,
                        SystemOutput *output, float delta_seconds) {
  SDL_LogVerbose(SDL_LOG_CATEGORY_SYSTEM, "V1 Tick CoreUI System");
  tick_coreui_system_internal(self, input, output, delta_seconds);
}

TB_DEFINE_SYSTEM(coreui, CoreUISystem, CoreUISystemDescriptor)

void tick_coreui(void *self, const SystemInput *input, SystemOutput *output,
                 float delta_seconds) {
  SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "V2 Tick CoreUI System");
  tick_coreui_system_internal((CoreUISystem *)self, input, output,
                              delta_seconds);
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
      .tick = tb_tick_coreui_system,
      .tick_fn_count = 1,
      .tick_fns = {{
          .system_id = CoreUISystemId,
          .order = E_TICK_PRE_UI,
          .function = tick_coreui,
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
