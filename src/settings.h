#pragma once

#include <stdint.h>

typedef enum TBMSAA {
  TBMSAA_Off = 0,
  TBMSAA_x2 = 2,
  TBMSAA_x4 = 4,
  TBMSAA_x8 = 8,
  TBMSAA_x16 = 16,
} TBMSAA;
#define TBMSAAOptionCount 5
static const TBMSAA TBMSAAOptions[TBMSAAOptionCount] = {
    TBMSAA_Off, TBMSAA_x2, TBMSAA_x4, TBMSAA_x8, TBMSAA_x16,
};
static const char *TBMSAAOptionNames[TBMSAAOptionCount] = {
    "Off", "x2", "x4", "x8", "x16",
};

typedef enum TBWindowMode {
  TBWindowMode_Windowed,
  TBWindowMode_Borderless,
  TBWindowMode_Fullscreen,
  TBWindowMode_Count
} TBWindowMode;
static const TBWindowMode TBWindowModes[TBWindowMode_Count] = {
    TBWindowMode_Windowed,
    TBWindowMode_Borderless,
    TBWindowMode_Fullscreen,
};
static const char *TBWindowModeNames[TBWindowMode_Count] = {
    "Windowed",
    "Borderless Fullscreen Window",
    "Exclusive Fullscreen",
};

typedef struct TBDisplayMode {
  uint32_t width;
  uint32_t height;
  uint32_t refresh_rate; // in Hz
} TBDisplayMode;

typedef enum TBVsyncMode {
  TBVsync_Off,
  TBVsync_Adaptive,
  TBVsync_On,
  TBVsync_Count,
} TBVsyncMode;
static const TBVsyncMode TBVsyncModes[TBVsync_Count] = {
    TBVsync_Off,
    TBVsync_Adaptive,
    TBVsync_On,
};
static const char *TBVsyncModeNames[TBVsync_Count] = {
    "Off",
    "Adaptive",
    "On",
};

typedef struct TBSettings {
  TBMSAA msaa;
  TBWindowMode windowing_mode;
  int32_t display_index;
  TBDisplayMode display_mode;
  TBVsyncMode vsync_mode;
  float resolution_scale;
} TBSettings;
