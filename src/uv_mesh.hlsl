#include "common.hlsli"

// Per-object data - Vertex Stage Only
ConstantBuffer<CommonObjectData> object_data: register(b0, space0);

// Per-view data - Fragment Stage Only
ConstantBuffer<CommonViewData> camera_data: register(b0, space1);
ConstantBuffer<CommonLightData> light_data : register(b1, space1);

Texture2D albedo_map : register(t0, space0); // Fragment Stage Only
Texture2D displacement_map : register(t1, space0); // Vertex Stage Only
Texture2D normal_map : register(t2, space0); // Fragment Stage Only
Texture2D roughness_map : register(t3, space0); // Fragment Stage Only

// Immutable sampler
sampler static_sampler : register(s4, space0);

struct VertexIn
{
    float3 local_pos : SV_POSITION;
    float3 normal : NORMAL0;
    float2 uv: TEXCOORD0;
};

struct Interpolators
{
    float4 clip_pos : SV_POSITION;
    float3 world_pos: POSITION0;
    float3 normal : NORMAL0;
    float2 uv: TEXCOORD0;
};

Interpolators vert(VertexIn i)
{
    float displacement_strength = 0.1;

    // Apply displacement map
    float height = displacement_map.SampleLevel(static_sampler, i.uv, 0).x * 2 - 1;
    float3 pos = i.local_pos + (i.normal * (height * displacement_strength));

    float3x3 orientation = (float3x3)object_data.m;

    Interpolators o;
    o.clip_pos = mul(float4(pos, 1.0), object_data.mvp);
    o.world_pos = mul(float4(pos, 1.0), object_data.m).xyz;
    o.normal = mul(i.normal, orientation); // convert to world-space normal
    o.uv = i.uv;
    return o;
}

float4 frag(Interpolators i) : SV_TARGET
{
    // Sample textures up-front
    float3 albedo = albedo_map.Sample(static_sampler, i.uv).rgb;
    float3 normal = normal_map.Sample(static_sampler, i.uv).xyz;
    float roughness = roughness_map.Sample(static_sampler, i.uv).x;
    float gloss = 1 - roughness;

    float3 lightColor = float3(1, 1, 1);

    // Lighting calcs
    float3 L = normalize(light_data.light_dir);
    // TODO: Use tangents and bitangents to create and apply
    // tangent space to world space transformation matrix.
    // Technically this only works because this is a plane
    float3 N = normalize(normal * 2 - 1); // Must unpack normal

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
    float3 color = ambient + (albedo * diffuse) + specular;

    return float4(color, 1);
}