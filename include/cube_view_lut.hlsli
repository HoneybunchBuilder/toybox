#pragma once

#include "simd.h"

// Per view matrix look up table
// So that each view is pointing at the right face of the cubemap
// Generated manually by doing the math on the CPU and writing the values here
/*
    float4x4 proj = perspective(PI_2, 1.0, 0.1, 1000.0f);
    float4x4 view = look_forward((float3){0}, (float3){0, -1, 0}, (float3){1, 0, 0});
    float4x4 vp = mulmf44(proj, view);

    Note that HLSL defaults matrices like this to being row major
    but the above code is column major. So we manually transpose the output
    of the above code when recording it here
*/
static const column_major float4x4 view_proj_lut[6] = {
    // X+
    {0, 0, 1, 0,
     0, 1, 0, 0,
     1.00010001, 0, 0, -0.00100100099,
     1, 0, 0, 0},
    // X-
    {0, 0, -1, 0,
     0, 1, 0, 0,
     -1.001001, 0, 0, -0.00100100099,
     -1, 0, 0, 0},
    // Y+
    {0, 0, -1, 0,
     1, 0, 0, 0,
     0, 1.001001, 0, -0.00100100099,
     0, 1, 0, 0},
    // Y-
    {0, 0, 1, 0,
     1, 0, 0, 0,
     0, -1.001001, 0, -0.00100100099,
     0, -1, 0, 0},
    // Z+
    {-1, 0, 0, 0,
     0, 1, 0, 0,
     0, 0, 1.001001, -0.00100100099,
     0, 0, 1, 0},
    // Z-
    {1, 0, 0, 0,
     0, 1, 0, 0,
     0, 0, -1.001001, -0.00100100099,
     0, 0, -1, 0},
};
