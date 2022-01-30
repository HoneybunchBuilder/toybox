#include "common.hlsli"
#include "lighting.hlsli"
#include "gltf.hlsli"

// Per-material data - Fragment Stage Only (Maybe vertex stage too later?)
ConstantBuffer<GLTFMaterialData> material_data : register(b0, space0);

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
};

struct Interpolators
{
    float4 clip_pos : SV_POSITION;
    float3 world_pos: POSITION0;
    float3 normal : NORMAL0;
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
    return o;
}

float4 frag(Interpolators i) : SV_TARGET
{
    // TODO: Get material base color color some other way
    float3 base_color = float3(0.5, 0.5, 0.5);

    float3 N = normalize(i.normal);

    float3 V = normalize(camera_data.view_pos - i.world_pos);
    float NdotV = clamp(abs(dot(N, V)), 0.001, 1.0);

    float3 out_color = float3(0.0, 0.0, 0.0);

    if(PermutationFlags & GLTF_PERM_PBR_METALLIC_ROUGHNESS)
    {
        float metallic = material_data.pbr_metallic_roughness.metallic_factor;
        float roughness = material_data.pbr_metallic_roughness.roughness_factor;

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
            float3 L = normalize(light_data.light_dir);

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
    }
    else // Phong fallback
    {
        float gloss = 0.5f;

        // for each light
        {
            float3 L = normalize(light_data.light_dir);
            float3 H = normalize(V + L);

            float3 light_color = float3(1, 1, 1);
            out_color += phong_light(base_color, light_color, gloss, N, L, V, H);
        }
    }

    // Gamma correct
    out_color = pow(out_color, float3(0.4545, 0.4545, 0.4545));

    // Ambient
    float3 ambient = float3(AMBIENT, AMBIENT, AMBIENT) * base_color;
    out_color += ambient;

    return float4(out_color, 1.0);
}