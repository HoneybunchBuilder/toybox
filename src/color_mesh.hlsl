#include "common.hlsli"

// Per-object data - Vertex Stage Only
ConstantBuffer<CommonObjectData> object_data: register(b0, space0);

// Per-view data - Fragment Stage Only
ConstantBuffer<CommonCameraData> camera_data: register(b0, space1);
ConstantBuffer<CommonLightData> light_data : register(b1, space1);

struct VertexIn
{
    float3 local_pos : SV_POSITION;
    float3 color : COLOR0;
    float3 normal : NORMAL0;
};

struct Interpolators
{
    float4 clip_pos : SV_POSITION;
    float3 world_pos : POSITION0;
    float3 color : COLOR0;
    float3 normal : NORMAL0;
};

Interpolators vert(VertexIn i)
{
    float4 pos = float4(i.local_pos, 1.0);
    float3x3 orientation = (float3x3)object_data.m;

    Interpolators o;
    o.clip_pos = mul(pos, object_data.mvp);
    o.world_pos = mul(pos, object_data.m).xyz;
    o.color = i.color;
    o.normal = normalize(mul(i.normal, orientation)); // convert to world-space normal
    return o;
}

float4 frag(Interpolators i) : SV_TARGET
{
    float gloss = 0.7; // Should be a material parameter...

    float3 lightColor = float3(1, 1, 1);

    // Lighting calcs
    float3 L = normalize(light_data.light_dir);
    float3 N = normalize(i.normal);

    // Calc ambient light
    float3 ambient = float3(0.01, 0.01, 0.01);

    // Calc diffuse Light
    float lambert = saturate(dot(N, L));
    float3 diffuse = lightColor * lambert;

    // Calc specular light
    float3 V = normalize(camera_data.view_pos - i.world_pos);
    float3 H = normalize(L + V);

    float3 specular_exponent = exp2(gloss * 11) + 2;
    float3 specular = saturate(dot(H, N)) * (lambert > 0); // Blinn-Phong
    specular = pow(specular, specular_exponent) * gloss;
    specular *= lightColor;

    // Compose final lighting color
    float3 color = ambient + (i.color * diffuse) + specular;
    return float4(color, 1);
}