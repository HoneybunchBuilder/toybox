#include "imgui.hlsli"

[[vk::push_constant]]
ConstantBuffer<ImGuiPushConstants> consts : register(b0);

Texture2D atlas : register(t0, space0);

sampler static_sampler : register(s1, space0);

struct VertexIn
{
    float2 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

struct Interpolators
{
    float4 clip_pos : SV_POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

Interpolators vert(VertexIn i)
{
    Interpolators o;
    o.clip_pos = float4(i.position * consts.scale + consts.translation, 0.5, 1);
    o.color = i.color;
    o.uv = i.uv;
    return o;
}

float4 frag(Interpolators i) : SV_TARGET
{
    return i.color * atlas.Sample(static_sampler, i.uv);
}