#pragma once
#include "common.hlsli"

struct Interpolators
{
    float4 pos : SV_POSITION;
    float2 uv0 : TEXCOORD0;
};

Interpolators vert(uint i : SV_VERTEXID)
{
    Interpolators o;
    o.uv0 = float2(uint2(i, i << 1) & 2);
    o.pos = float4(lerp(float2(-1, 1), float2(1, -1), o.uv0), 0.5f, 1.0f);
    return o;
}
