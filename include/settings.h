#pragma once

#include <stdint.h>

typedef enum TbMSAA {
  TBMSAA_Off = 0,
  TBMSAA_x2 = 2,
  TBMSAA_x4 = 4,
  TBMSAA_x8 = 8,
  TBMSAA_x16 = 16,
} TbMSAA;
#define TBMSAAOptionCount 5
static const TbMSAA TBMSAAOptions[TBMSAAOptionCount] = {
    TBMSAA_Off, TBMSAA_x2, TBMSAA_x4, TBMSAA_x8, TBMSAA_x16,
};
// static const char *TBMSAAOptionNames[TBMSAAOptionCount] = {
//     "Off", "x2", "x4", "x8", "x16",
// };

typedef enum TbWindowMode {
  TBWindowMode_Windowed,
  TBWindowMode_Borderless,
  TBWindowMode_Fullscreen,
  TBWindowMode_Count
} TbWindowMode;
static const TbWindowMode TBWindowModes[TBWindowMode_Count] = {
    TBWindowMode_Windowed,
    TBWindowMode_Borderless,
    TBWindowMode_Fullscreen,
};
// static const char *TBWindowModeNames[TBWindowMode_Count] = {
//     "Windowed",
//     "Borderless Fullscreen Window",
//     "Exclusive Fullscreen",
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
  TbMSAA msaa;
  TbWindowMode windowing_mode;
  int32_t display_index;
  TbDisplayMode display_mode;
  TbVsyncMode vsync_mode;
  float resolution_scale;
} TbSettings;
