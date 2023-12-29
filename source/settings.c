#include "settings.h"

#include "fxaa.h"
#include "profiling.h"
#include "tbcommon.h"
#include "tbimgui.h"
#include "world.h"

#include <flecs.h>

static const TbFXAAPushConstants tb_fxaa_options[] = {
    {
        .on = false,
    },
    {
        .on = true,
        .edge_threshold = 1.0 / 2.0,
        .edge_threshold_min = 1.0 / 12.0,
        .search_steps = 16,
        .search_accel = 1,
        .search_threshold = 1.0 / 4.0,
        .subpixel = 0,
        .subpixel_cap = 1.0 / 4.0,
        .subpixel_trim = 1.0 / 2.0,
        .subpixel_trim_scale = 1.0 / (1.0 - (1.0 / 2.0)),
    },
    {
        .on = true,
        .edge_threshold = 1.0 / 4.0,
        .edge_threshold_min = 1.0 / 16.0,
        .search_steps = 32,
        .search_accel = 1,
        .search_threshold = 1.0 / 4.0,
        .subpixel = 1,
        .subpixel_cap = 3.0 / 4.0,
        .subpixel_trim = 1.0 / 4.0,
        .subpixel_trim_scale = 1.0 / (1.0 - (1.0 / 4.0)),
    },
    {
        .on = true,
        .edge_threshold = 1.0 / 8.0,
        .edge_threshold_min = 1.0 / 32.0,
        .search_steps = 64,
        .search_accel = 1,
        .search_threshold = 1.0 / 4.0,
        .subpixel = 1,
        .subpixel_cap = 7.0 / 8.0,
        .subpixel_trim = 1.0 / 8.0,
        .subpixel_trim_scale = 1.0 / (1.0 - (1.0 / 8.0)),
    },
    {0},
};
static const char *tb_fxaa_items[] = {
    "Off", "Low", "Medium", "High", "Custom",
};

void tick_settings_ui(ecs_iter_t *it) {
  tb_auto ecs = it->world;
  ECS_COMPONENT(ecs, TbSettings);
  ECS_COMPONENT(ecs, TbFXAASystem);
  tb_auto settings = ecs_field(it, TbSettings, 1);

  if (igBegin("Settings", NULL, 0)) {
    tb_auto fxaa = ecs_singleton_get_mut(ecs, TbFXAASystem);

    if (igCombo_Str_arr("FXAA", &settings->fxaa_option, tb_fxaa_items, 5, 5)) {
      fxaa->settings = tb_fxaa_options[settings->fxaa_option];
    }

    if (igBeginChild_Str("FXAA Values", (ImVec2){0}, true, 0)) {
      igSliderFloat("Edge Threshold", &fxaa->settings.edge_threshold, 0.25f,
                    1.0f, "%.4f", 0);
      igSliderFloat("Edge Threshold Min", &fxaa->settings.edge_threshold_min,
                    1.0f / 32.0f, 1.0f / 12.0f, "%.4f", 0);
      igSeparator();

      igSliderInt("Subpixel", &fxaa->settings.subpixel, 0, 2, "%d", 0);
      igSliderFloat("Subpixel Trim", &fxaa->settings.subpixel_trim, 0.0f, 0.5f,
                    "%.4f", 0);
      igSliderFloat("Subpixel Trim Scale", &fxaa->settings.subpixel_trim_scale,
                    0.0f, 10.0f, "%.4f", 0);
      igSliderFloat("Subpixel Cap", &fxaa->settings.subpixel_cap, 0.0f, 1.0f,
                    "%4f", 0);
      igSeparator();

      igSliderFloat("Search Steps", &fxaa->settings.search_steps, 1.0f, 128.0f,
                    "%.0f", 0);
      igSliderFloat("Search Acceleration", &fxaa->settings.search_accel, 1.0f,
                    4.0f, "%.0f", 0);
      igSliderFloat("Search Threshold", &fxaa->settings.search_threshold, 0.0f,
                    0.5f, "%.4f", 0);
      igEndChild();
    }
    igEnd();
  }
}

void tb_register_settings_system(TbWorld *world) {
  tb_auto ecs = world->ecs;
  ECS_COMPONENT(ecs, TbSettings);
  ECS_COMPONENT(ecs, TbFXAASystem);

  TbSettings settings = {0};
  // TODO: Apply saved settings loaded from disk
  settings.fxaa_option = 1;
  settings.fxaa = tb_fxaa_options[settings.fxaa_option];

  // HACK: This puts a soft dependency on initialization order.
  // Assuming this function will be called after the fxaa system is already
  // registered
  tb_auto fxaa = ecs_singleton_get_mut(ecs, TbFXAASystem);
  fxaa->settings = settings.fxaa;

  // Sets a singleton based on the value at a pointer
  ecs_set_ptr(ecs, ecs_id(TbSettings), TbSettings, &settings);

  ECS_SYSTEM(ecs, tick_settings_ui, EcsOnUpdate, TbSettings(TbSettings))
}

void tb_unregister_settings_system(TbWorld *world) {
  tb_auto ecs = world->ecs;
  ECS_COMPONENT(ecs, TbSettings);
  ecs_singleton_remove(ecs, TbSettings);
}