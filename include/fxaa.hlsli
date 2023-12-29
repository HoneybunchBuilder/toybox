#pragma once

#include "common.hlsli"

#define TB_FXAA_SUBPIXEL_OFF 0
#define TB_FXAA_SUBPIXEL_ON 1
#define TB_FXAA_SUBPIXEL_FASTER 2

typedef struct TbFXAAPushConstants {
  int32_t on;
  float edge_threshold_min;
  float edge_threshold;

  int32_t subpixel;
  float subpixel_trim;
  float subpixel_trim_scale;
  float subpixel_cap;

  float search_steps;
  float search_accel;
  float search_threshold;
}
TbFXAAPushConstants;

#define FXAA_SET(space)                                                        \
  Texture2D input : register(t0, space);                                       \
  SamplerState in_sampler : register(s1, space);                               \
  [[vk::push_constant]]                                                        \
  ConstantBuffer<TbFXAAPushConstants> consts : register(b2, space);

// If not in a shader, make a quick static assert check
#ifndef __HLSL_VERSION
_Static_assert(sizeof(TbFXAAPushConstants) <= PUSH_CONSTANT_BYTES,
               "Push constant structure exceeds max size");
#endif
