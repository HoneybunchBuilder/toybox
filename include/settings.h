#pragma once

#include <stdint.h>

#include "fxaa.hlsli"

typedef struct TbWorld TbWorld;

typedef enum TbWindowMode {
  TBWindowMode_Windowed,
  TBWindowMode_Borderless,
  TBWindowMode_Count
} TbWindowMode;
static const TbWindowMode TBWindowModes[TBWindowMode_Count] = {
    TBWindowMode_Windowed,
    TBWindowMode_Borderless,
};
// static const char *TBWindowModeNames[TBWindowMode_Count] = {
//     "Windowed",
//     "Borderless Fullscreen Window",
// };

typedef struct TbDisplayMode {
  uint32_t width;
  uint32_t height;
  uint32_t refresh_rate; // in Hz
} TbDisplayMode;

typedef enum TbVsyncMode {
  TBVsync_Off,
  TBVsync_Adaptive,
  TBVsync_On,
  TBVsync_Count,
} TbVsyncMode;
static const TbVsyncMode TBVsyncModes[TBVsync_Count] = {
    TBVsync_Off,
    TBVsync_Adaptive,
    TBVsync_On,
};
// static const char *TBVsyncModeNames[TBVsync_Count] = {
//     "Off",
//     "Adaptive",
//     "On",
// };

typedef struct TbSettings {
  TbWindowMode windowing_mode;
  int32_t display_index;
  TbDisplayMode display_mode;
  TbVsyncMode vsync_mode;
  float resolution_scale;
  int32_t fxaa_option;
  TbFXAAPushConstants fxaa;
} TbSettings;

void tb_register_settings_system(TbWorld *world);
void tb_unregister_settings_system(TbWorld *world);
