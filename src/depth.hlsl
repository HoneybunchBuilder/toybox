#include "common.hlsli"

// Per-object data - Vertex Stage Only
ConstantBuffer<CommonObjectData> object_data: register(b0, space0);

// Per-view data - Fragment Stage Only
ConstantBuffer<CommonCameraData> camera_data: register(b0, space1);
ConstantBuffer<CommonLightData> light_data : register(b1, space1);

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

void frag(Interpolators i) 
{
}