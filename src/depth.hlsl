#include "common.hlsli"
#include "shadow.hlsli"

[[vk::push_constant]]
ConstantBuffer<ShadowPushConstants> consts : register(b0);

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
    o.clip_pos = mul(float4(i.local_pos, 1.0), consts.mvp);
    return o;
}

void frag(Interpolators i)
{
}