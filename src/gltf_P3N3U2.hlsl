#include "common.hlsli"
#include "lighting.hlsli"
#include "gltf.hlsli"

// Per-material data - Fragment Stage Only (Maybe vertex stage too later?)
ConstantBuffer<GLTFMaterialData> material_data : register(b0, space0);
Texture2D albedo_map : register(t1, space0); // Fragment Stage Only
Texture2D normal_map : register(t2, space0); // Fragment Stage Only
Texture2D roughness_map : register(t3, space0); // Fragment Stage Only
sampler static_sampler : register(s4, space0); // Immutable sampler

// Per-object data - Vertex Stage Only
ConstantBuffer<CommonObjectData> object_data: register(b0, space1);

// Per-view data - Fragment Stage Only
ConstantBuffer<CommonCameraData> camera_data: register(b0, space2);
ConstantBuffer<CommonLightData> light_data : register(b1, space2);

[[vk::constant_id(0)]] const uint PermutationFlags = 0;

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
    // Apply displacement map
    float3 pos = i.local_pos;

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

    float3 N = normalize(i.normal);
    if(PermutationFlags & GLTF_PERM_NORMAL_MAP)
    {
        N = normal_map.Sample(static_sampler, i.uv).xyz;
        N = normalize(N * 2 - 1); // Must unpack normal
    }

    float3 L = normalize(light_data.light_dir);
    float3 V = normalize(camera_data.view_pos - i.world_pos);
    float3 H = normalize(V + L);

    float3 color = float3(0.0, 0.0, 0.0);

    if(PermutationFlags & GLTF_PERM_PBR_METALLIC_ROUGHNESS)
    {
        float metallic = material_data.pbr_metallic_roughness.metallic_factor;
        float roughness = material_data.pbr_metallic_roughness.roughness_factor;

        // TODO: If has roughness map
        // roughness = roughness_map.Sample(static_sampler, i.uv).x;

        float3 F0 = float3(0.04, 0.04, 0.04);
        F0 = lerp(F0, albedo, metallic);

        // Reflectance
        float3 Lo = float3(0.0, 0.0, 0.0);
        //for each light
        {
            float3 light_color = float3(1, 1, 1);

            Lo += pbr_metal_rough_light(F0, albedo, light_color, metallic, roughness, N, L, V, H);
        }

        float3 ambient = float3(0.03, 0.03, 0.03) * albedo;
        color = ambient + Lo;
        
        color = color / (color + float3(1.0, 1.0, 1.0));
        color = pow(color, float3(1.0/2.2, 1.0/2.2, 1.0/2.2));
    }
    else // Phong fallback
    {
        float gloss = 0.5;

        // for each light
        {
            float3 light_color = float3(1, 1, 1);
            color += phong_light(albedo, light_color, gloss, N, L, V, H);
        }
    }

    return float4(color, 1.0);
}