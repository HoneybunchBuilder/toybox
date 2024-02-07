#pragma once

/*
        An optional logging system that can be enabled if you want to display
   log messages through ImGui
*/
#include "coreuisystem.h"
#include "dynarray.h"
#include "tblog.h"

#include <flecs.h>

#define TB_LOG_SYS_PRIO (TB_COREUI_SYS_PRIO + 1)

typedef struct TbWorld TbWorld;

typedef struct TbLogMessage {
  float time;
  int32_t category;
  SDL_LogPriority priority;
  char *message;
} TbLogMessage;

typedef struct TbLogSystem {
  TbAllocator log_alloc;
  bool *ui;
  bool enabled;
  bool autoscroll;
  void *orig_log_fn;
  void *orig_userdata;
  TB_DYN_ARR_OF(TbLogMessage) messages;
} TbLogSystem;
extern ECS_COMPONENT_DECLARE(TbLogSystem);
