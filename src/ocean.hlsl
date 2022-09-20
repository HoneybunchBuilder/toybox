#include "common.hlsli"
#include "pbr.hlsli"
#include "ocean.hlsli"

// Wave data - Vertex & Fragment Stages
ConstantBuffer<OceanData> ocean_data : register(b0, space0);
[[vk::push_constant]] // Vertex stage only
ConstantBuffer<OceanPushConstants> consts : register(b1, space0);

// Per-object data - Vertex Stage Only
ConstantBuffer<CommonObjectData> object_data: register(b0, space1);

// Per-view data - Fragment Stage Only
ConstantBuffer<CommonViewData> camera_data: register(b0, space2);

struct VertexIn
{
    float3 local_pos : SV_POSITION;
};

struct Interpolators
{
    float4 clip_pos : SV_POSITION;
    float3 world_pos: POSITION0;
    float3 normal : NORMAL0;
    float3 tangent : TANGENT0;
    float3 binormal : BINORMAL0;
    //float4 shadowcoord : TEXCOORD1;
};

Interpolators vert(VertexIn i)
{
    float3 pos = i.local_pos;
    float4 clip_pos = mul(float4(pos, 1.0), object_data.mvp);
    float4 world_pos = mul(float4(pos, 1.0), object_data.m);

    Interpolators o;
    o.clip_pos = clip_pos;
    o.world_pos = world_pos.xyz;
    // TODO: Calculate wave offset

    return o;
}

float4 frag(Interpolators i) : SV_TARGET
{
    return float4(1, 0, 0, 1);
}
