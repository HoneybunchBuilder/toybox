#pragma once

#include "tb_simd.h"

#include "common.hlsli"
#include "imgui.hlsli"

_Static_assert(sizeof(TbSkyPushConstants) <= PUSH_CONSTANT_BYTES,
               "Too Many Push Constants");
_Static_assert(sizeof(TbImGuiPushConstants) <= PUSH_CONSTANT_BYTES,
               "Too Many Push Constants");
