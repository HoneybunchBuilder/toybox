#include "common.hlsli"
#include "lighting.hlsli"
#include "gltf.hlsli"

// Per-material data - Fragment Stage Only (Maybe vertex stage too later?)
ConstantBuffer<GLTFMaterialData> material_data : register(b0, space0);
Texture2D albedo_map : register(t1, space0); // Fragment Stage Only
Texture2D normal_map : register(t2, space0); // Fragment Stage Only
Texture2D metal_rough_map : register(t3, space0); // Fragment Stage Only
//Texture2D emissive_map : register(t4, space0); // Fragment Stage Only
sampler static_sampler : register(s4, space0); // Immutable sampler

// Per-object data - Vertex Stage Only
ConstantBuffer<CommonObjectData> object_data: register(b0, space1);

// Per-view data
ConstantBuffer<CommonCameraData> camera_data: register(b0, space2); // Frag Only
ConstantBuffer<CommonLightData> light_data : register(b1, space2); // Vert & Frag
Texture2D shadow_map : register(t2, space2); // Frag Only
SamplerState shadow_sampler : register(s2, space2);

[[vk::constant_id(0)]] const uint PermutationFlags = 0;

struct VertexIn
{
    float3 local_pos : SV_POSITION;
    float3 normal : NORMAL0;
    float4 tangent: TANGENT0;
    float2 uv: TEXCOORD0;
};

struct Interpolators
{
    float4 clip_pos : SV_POSITION;
    float3 world_pos: POSITION0;
    float3 normal : NORMAL0;
    float3 tangent : TANGENT0;
    float3 binormal : BINORMAL0;
    float2 uv: TEXCOORD0;
    float4 shadowcoord : TEXCOORD1;
};

#define AMBIENT 0.1

Interpolators vert(VertexIn i)
{
    // Apply displacement map
    float3 pos = i.local_pos;
    float4 clip_pos = mul(float4(pos, 1.0), object_data.mvp);
    float4 world_pos = mul(float4(pos, 1.0), object_data.m);

    float3x3 orientation = (float3x3)object_data.m;

    Interpolators o;
    o.clip_pos = clip_pos;
    o.world_pos = world_pos.xyz;
    o.normal = normalize(mul(i.normal, orientation)); // convert to world-space normal
    o.tangent = normalize(mul(orientation, i.tangent.xyz));
    o.binormal = cross(o.tangent, o.normal) * i.tangent.w;
    o.uv = i.uv;
    o.shadowcoord = mul(world_pos, light_data.light_vp);

    return o;
}

float4 frag(Interpolators i) : SV_TARGET
{
    // Sample textures up-front
    float3 albedo = albedo_map.Sample(static_sampler, i.uv).rgb;

    // World-space normal
    float3 N = normalize(i.normal);
    if(PermutationFlags & GLTF_PERM_NORMAL_MAP)
    {
        // Construct TBN
        float3x3 tbn = float3x3(
            normalize(i.tangent),
            normalize(i.binormal),
            normalize(i.normal)
        );

        // Convert from tangent space to world space
        float3 tangentSpaceNormal = normal_map.Sample(static_sampler, i.uv).xyz;
        tangentSpaceNormal = normalize(tangentSpaceNormal * 2 - 1); // Must unpack normal
        N = normalize(mul(tangentSpaceNormal, tbn));
    }

    float3 L = normalize(light_data.light_dir);
    float3 V = normalize(camera_data.view_pos - i.world_pos);
    float3 H = normalize(V + L);

    float3 color = float3(0.0, 0.0, 0.0);

    if(PermutationFlags & GLTF_PERM_PBR_METALLIC_ROUGHNESS)
    { 
        float metallic = material_data.pbr_metallic_roughness.metallic_factor;
        float roughness = material_data.pbr_metallic_roughness.roughness_factor;

        if(PermutationFlags & GLTF_PERM_PBR_METAL_ROUGH_TEX)
        {
            // The red channel of this texture *may* store occlusion.
            // TODO: Check the perm for occlusion
            metallic = metal_rough_map.Sample(static_sampler, i.uv).b;
            roughness = metal_rough_map.Sample(static_sampler, i.uv).g;
        }

        // Angle between surface normal and outgoing light direction.
        float cosLo = max(0.0, dot(N, V));

        // Specular reflection vector
        float3 Lr = 2.0 * cosLo * N - V; // Used later for ambient lighting

        // Fresnel reflectance at normal incidence (for metals use albedo color).
        float3 F0 = lerp(Fdielectric, albedo, metallic);

        //for each light
        {
            float3 light_color = float3(1, 1, 1);

            color += pbr_light(F0, light_color, albedo, metallic, roughness, N, L, V, cosLo);
        }

        // TODO: Ambient IBL
        float3 ambient = float3(AMBIENT, AMBIENT, AMBIENT) * albedo;
        color += ambient;
    }
    else // Phong fallback
    {
        float gloss = 0.5;

        // for each light
        {
            float3 light_color = float3(1, 1, 1);
            color += phong_light(albedo, light_color, gloss, N, L, V, H);
        }

        float3 ambient = float3(AMBIENT, AMBIENT, AMBIENT) * albedo;
        color += ambient;
    }

    // Gamma correct
    color = pow(color, float3(0.4545, 0.4545, 0.4545));

    //float shadow = pcf_filter(i.shadowcoord, AMBIENT, shadow_map, shadow_sampler);
    //color *= shadow;

    return float4(color, 1);
}