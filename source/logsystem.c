#include "logsystem.h"

#include "coreuisystem.h"
#include "dynarray.h"
#include "profiling.h"
#include "tbimgui.h"
#include "world.h"

ECS_COMPONENT_DECLARE(TbLogSystem);

static float tb_log_time = 0.0f;

void tb_log_hook(void *userdata, int32_t category, SDL_LogPriority priority,
                 const char *message) {
  tb_auto sys = (TbLogSystem *)(userdata);
  if (sys->enabled) {
    // Min of 32 characters to ensure space for formatting
    tb_auto msg_size = (SDL_strlen(message) + 32);
    tb_auto msg = (char *)mi_malloc(msg_size);
    SDL_snprintf(msg, msg_size, "%s", message);

    TracyCMessage(msg, SDL_strlen(msg));

    TbLogMessage tb_msg = {tb_log_time, category, priority, msg};
    TB_DYN_ARR_APPEND(sys->messages, tb_msg);
  }

  // Fall in to the original SDL log impl so we also get info written to the
  // console
  if (sys->orig_log_fn) {
    tb_auto fn = (SDL_LogOutputFunction)sys->orig_log_fn;
    fn(sys->orig_userdata, category, priority, message);
  }
}

void log_ui_tick(ecs_iter_t *it) {
  TracyCZoneN(ctx, "Log System UI Tick", true);
  TracyCZoneColor(ctx, TracyCategoryColorUI);
  tb_auto sys = ecs_field(it, TbLogSystem, 1);

  const int32_t log_cols = 4;

  tb_log_time += it->delta_time;

  // Some helper blocks for formatting
  tb_auto prio_to_str = ^const char *(SDL_LogPriority prio) {
    static const char *prio_strings[] = {
        "Verbose", "Debug", "Info", "Warn", "Error", "Critical",
    };
    int32_t idx = ((int32_t)prio) - 1;
    return prio_strings[idx];
  };
  tb_auto cat_to_str = ^const char *(int32_t cat) {
    // These are the built-in categories for SDL
    if (cat >= SDL_LOG_CATEGORY_APPLICATION &&
        cat < SDL_LOG_CATEGORY_RESERVED1) {
      static const char *sdl_cat_strings[] = {
          "Application", "Error", "Assert", "System", "Error", "Critical",
      };
      return sdl_cat_strings[cat];
      // These are our custom categories for toybox
    } else if (cat >= TB_LOG_CATEGORY_RENDER_THREAD &&
               cat < TB_LOG_CATEGORY_CUSTOM) {
      static const char *tb_cat_strings[] = {
          "RenderThread",
      };
      int32_t idx = (cat - TB_LOG_CATEGORY_RENDER_THREAD);
      return tb_cat_strings[idx];
    }
    // Theoretically the application could want custom log categories
    // Figure out a way to facilitate that. Maybe a user-provided block?
    static const char *unknown_cat_str = "Unknown";
    return unknown_cat_str;
  };

  if (sys->ui && *sys->ui) {
    if (igBegin("Log", sys->ui, 0)) {
      igCheckbox("Enabled", &sys->enabled);
      igSameLine(0, 128);
      igCheckbox("Autoscroll", &sys->autoscroll);
      igSameLine(0, 6);
      if (igButton("Clear", (ImVec2){0})) {
        TB_DYN_ARR_CLEAR(sys->messages);
      }
      if (igBeginChild_Str("##log", (ImVec2){0}, 0,
                           ImGuiWindowFlags_NoScrollbar)) {
        igSpacing();
        tb_auto table_flags = ImGuiTableFlags_BordersOuter |
                              ImGuiTableFlags_BordersInner |
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY;
        if (igBeginTable("##log_table", log_cols, table_flags, (ImVec2){0},
                         0)) {
          igTableSetupScrollFreeze(0, 1); // Make top row always visible
          igTableSetupColumn("Time", 0, 0, 0);
          igTableSetupColumn("Category", 0, 0, 1);
          igTableSetupColumn("Priority", 0, 0, 2);
          igTableSetupColumn("Message", 0, 0, 3);
          igTableHeadersRow();

          TB_DYN_ARR_FOREACH(sys->messages, i) {
            tb_auto message = TB_DYN_ARR_AT(sys->messages, i);
            igTableNextRow(0, 0);

            igTableNextColumn();
            igText("%.4f", message.time);
            igTableNextColumn();
            igText("%s", cat_to_str(message.category));
            igTableNextColumn();
            igText("%s", prio_to_str(message.priority));
            igTableNextColumn();
            igText("%s", message.message);
          }
          if (sys->autoscroll && sys->enabled) {
            igSetScrollHereY(1.0f);
          }
        }
        igEndTable();
      }
      igEndChild();
    }
    igEnd();
  }

  TracyCZoneEnd(ctx);
}

void tb_register_log_sys(TbWorld *world) {
  tb_auto ecs = world->ecs;
  ECS_COMPONENT_DEFINE(ecs, TbLogSystem);

  SDL_LogSetAllPriority(SDL_LOG_PRIORITY_VERBOSE);

  tb_auto coreui = ecs_singleton_get_mut(ecs, TbCoreUISystem);

  tb_auto sys = ecs_singleton_get_mut(ecs, TbLogSystem);
  *sys = (TbLogSystem){
      .log_alloc = world->std_alloc,
      .ui = tb_coreui_register_menu(coreui, "Log"),
      .enabled = true,
      .autoscroll = true,
  };

  SDL_LogGetOutputFunction((SDL_LogOutputFunction *)sys->orig_log_fn,
                           &sys->orig_userdata);

  TB_DYN_ARR_RESET(sys->messages, sys->log_alloc, 1024);
  ECS_SYSTEM(ecs, log_ui_tick, EcsPreUpdate, TbLogSystem(TbLogSystem));

  SDL_LogSetOutputFunction(tb_log_hook,
                           ecs_singleton_get_mut(ecs, TbLogSystem));
}

void tb_unregister_log_sys(TbWorld *world) {
  tb_auto ecs = world->ecs;
  tb_auto sys = ecs_singleton_get_mut(ecs, TbLogSystem);

  SDL_LogSetOutputFunction((SDL_LogOutputFunction)sys->orig_log_fn,
                           sys->orig_userdata);

  TB_DYN_ARR_FOREACH(sys->messages, i) {
    tb_auto message = TB_DYN_ARR_AT(sys->messages, i);
    mi_free(message.message);
  }

  TB_DYN_ARR_DESTROY(sys->messages);
}
