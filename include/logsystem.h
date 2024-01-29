#pragma once

/*
        An optional logging system that can be enabled if you want to display
   log messages through ImGui
*/
#include "dynarray.h"
#include "tblog.h"

#include <flecs.h>

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

void tb_register_log_sys(TbWorld *world);
void tb_unregister_log_sys(TbWorld *world);
