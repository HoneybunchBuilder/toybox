#include "common.hlsli"

struct Interpolators
{
    float4 pos : SV_POSITION;
    float2 uv0 : TEXCOORD0;
};

Interpolators vert(uint i : SV_VERTEXID)
{
    Interpolators o;
    o.uv0 = float2((i << 1) & 2, i & 2);
    o.pos = float4(o.uv0 * 2.0f + -1.0f, 0.5f, 1.0f);
    return o;
}