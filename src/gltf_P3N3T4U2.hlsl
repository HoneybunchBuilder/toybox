#include "common.hlsli"
#include "lighting.hlsli"
#include "gltf.hlsli"

// Per-material data - Fragment Stage Only (Maybe vertex stage too later?)
ConstantBuffer<GLTFMaterialData> material_data : register(b0, space0);
Texture2D base_color_map : register(t1, space0); // Fragment Stage Only
Texture2D normal_map : register(t2, space0); // Fragment Stage Only
Texture2D metal_rough_map : register(t3, space0); // Fragment Stage Only
//Texture2D emissive_map : register(t4, space0); // Fragment Stage Only
sampler static_sampler : register(s4, space0); // Immutable sampler

// Per-object data - Vertex Stage Only
ConstantBuffer<CommonObjectData> object_data: register(b0, space1);

// Per-view data
ConstantBuffer<CommonViewData> camera_data: register(b0, space2); // Frag Only
//ConstantBuffer<CommonLightData> light_data : register(b1, space2); // Vert & Frag
//Texture2D shadow_map : register(t2, space2); // Frag Only
//SamplerState shadow_sampler : register(s2, space2);

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

Interpolators vert(VertexIn i)
{
    float4x4 mvp = mul(object_data.m, camera_data.vp);

    // Apply displacement map
    float3 pos = i.local_pos;
    float4 clip_pos = mul(float4(pos, 1.0), mvp);
    float4 world_pos = mul(float4(pos, 1.0), object_data.m);

    float3x3 orientation = (float3x3)object_data.m;

    Interpolators o;
    o.clip_pos = clip_pos;
    o.world_pos = world_pos.xyz;
    o.normal = normalize(mul(i.normal, orientation)); // convert to world-space normal
    o.tangent = normalize(mul(orientation, i.tangent.xyz));
    o.binormal = cross(o.tangent, o.normal) * i.tangent.w;
    o.uv = i.uv;
    //o.shadowcoord = mul(world_pos, light_data.light_vp);

    return o;
}

float4 frag(Interpolators i) : SV_TARGET
{
    // Sample textures up-front
    float3 base_color = float3(0.5, 0.5, 0.5);

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

    // Per view calcs
    float3 V = normalize(camera_data.view_pos - i.world_pos);
    float NdotV = clamp(abs(dot(N, V)), 0.001, 1.0);
    float3 reflection = -normalize(reflect(V, N));
    reflection.y *= -1.0;

    float3 out_color = float3(0.0, 0.0, 0.0);

    float3 light_dir = float3(0, 1, 0);

    if(PermutationFlags & GLTF_PERM_PBR_METALLIC_ROUGHNESS)
    { 
        float metallic = material_data.pbr_metallic_roughness.metallic_factor;
        float roughness = material_data.pbr_metallic_roughness.roughness_factor;

        // TODO: Handle alpha masking
        {
            float4 pbr_base_color = material_data.pbr_metallic_roughness.base_color_factor;
            if(PermutationFlags & GLTF_PERM_BASE_COLOR_MAP)
            {
                pbr_base_color *= base_color_map.Sample(static_sampler, i.uv);
            }

            base_color = pbr_base_color.rgb;
        }

        if(PermutationFlags & GLTF_PERM_PBR_METAL_ROUGH_TEX)
        {
            // The red channel of this texture *may* store occlusion.
            // TODO: Check the perm for occlusion
            float4 mr_sample = metal_rough_map.Sample(static_sampler, i.uv);
            roughness = mr_sample.g * roughness;
            metallic = mr_sample.b * metallic;
        }

        float alpha_roughness = roughness * roughness;

        float3 f0 = float3(0.4, 0.4, 0.4);

        float3 diffuse_color = base_color * (float3(1.0, 1.0, 1.0) - f0);
        diffuse_color *= 1.0 - metallic;

        float3 specular_color = lerp(f0, base_color, metallic);
        float reflectance = max(max(specular_color.r, specular_color.g), specular_color.b);

        // For typical incident reflectance range (between 4% to 100%) set the grazing reflectance to 100% for typical fresnel effect.
        // For very low reflectance range on highly diffuse objects (below 4%), incrementally reduce grazing reflecance to 0%.
        float reflectance_90 = clamp(reflectance * 25.0, 0.0, 1.0);
        float3 specular_environment_R0 = specular_color;
        float3 specular_environment_R90 = float3(1.0, 1.0, 1.0) * reflectance_90;

        //for each light
        {
            float3 light_color = float3(1, 1, 1);
            float3 L = light_dir;

            PBRLight light = {
                light_color,
                L,
                specular_environment_R0,
                specular_environment_R90,
                alpha_roughness,
                diffuse_color,
            };

            out_color += pbr_lighting(light, N, V, NdotV);
        }

        // TODO: Ambient IBL

        // TODO: Ambient Occlusion

        // TODO: Emissive Texture
    }
    else // Phong fallback
    {
        float gloss = 0.5;

        // for each light
        {
            float3 L = light_dir;
            float3 H = normalize(V + L);
            
            float3 light_color = float3(1, 1, 1);
            out_color += phong_light(base_color, light_color, gloss, N, L, V, H);
        }
    }

    // Tonemap
    /*
    float exposure = 4.5f; // TODO: pass in as a parameter
    out_color = tonemap(out_color * exposure);
    out_color *= 1.0f / tonemap(float3(11.2f, 11.2f, 11.2f));
    */

    // Gamma correction
    float gamma = 2.2f; // TODO: pass in as a parameter
    out_color = pow(out_color, float3(1.0f / gamma, 1.0f / gamma, 1.0f / gamma));

    // Shadow hack
    /*
    float3 L = normalize(light_data.light_dir);
    float NdotL = clamp(dot(N, L), 0.001, 1.0);

    float shadow = pcf_filter(i.shadowcoord, AMBIENT, shadow_map, shadow_sampler, NdotL);
    out_color *= shadow;
    */

    // Want to add ambient term after shadowing
    float3 ambient = float3(AMBIENT, AMBIENT, AMBIENT) * base_color;
    out_color += ambient;

    return float4(out_color, 1);
}