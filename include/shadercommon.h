#pragma once

#include "simd.h"

#include "common.hlsli"
#include "imgui.hlsli"
#include "shadow.hlsli"

_Static_assert(sizeof(SkyPushConstants) <= PUSH_CONSTANT_BYTES,
               "Too Many Push Constants");
_Static_assert(sizeof(ImGuiPushConstants) <= PUSH_CONSTANT_BYTES,
               "Too Many Push Constants");
_Static_assert(sizeof(ShadowConstants) <= PUSH_CONSTANT_BYTES,
               "Too Many Push Constants");
