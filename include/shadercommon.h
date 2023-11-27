#pragma once

#include "simd.h"

#include "common.hlsli"
#include "imgui.hlsli"
#include "shadow.hlsli"

_Static_assert(sizeof(TbSkyPushConstants) <= PUSH_CONSTANT_BYTES,
               "Too Many Push Constants");
_Static_assert(sizeof(TbImGuiPushConstants) <= PUSH_CONSTANT_BYTES,
               "Too Many Push Constants");
_Static_assert(sizeof(TbShadowConstants) <= PUSH_CONSTANT_BYTES,
               "Too Many Push Constants");
