// Adapted from: http://www.madebyevan.com/shaders/grid/

#include "common.hlsli"

// Per-object data - Vertex Stage Only
ConstantBuffer<CommonObjectData> object_data: register(b0, space0);

struct VertexIn
{
    float3 local_pos : SV_POSITION;
};

struct Interpolators
{
    float4 clip_pos : SV_POSITION;
};

Interpolators vert(VertexIn i)
{
    Interpolators o;
    o.clip_pos = mul(float4(i.local_pos, 1.0), object_data.mvp);
    return o;
}

float4 frag(Interpolators i) : SV_TARGET
{
    float2 coord = i.clip_pos.xz;
    
    float2 grid = abs(frac(coord - 0.5) - 0.5) / fwidth(coord);
    float l = min(grid.x, grid.y); // line is a reserved name
    l = 1.0 - min(l, 1.0);

    return  float4(float3(l, l, l), 1.0);
}