#include "common.hlsli"
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

float distributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return a2 / denom;
}

float geometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;

    float denom = NdotV * (1.0 - k) + k;
    
    return NdotV / denom;
}

float geometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = geometrySchlickGGX(NdotV, roughness);
    float ggx1 = geometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

float3 fresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
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

    float roughness = 0.5;

    float3 V = normalize(camera_data.view_pos - i.world_pos);

    float3 color = float3(0.0, 0.0, 0.0);

    if(PermutationFlags & GLTF_PERM_PBR_METALLIC_ROUGHNESS)
    {
        float metallic = material_data.pbr_metallic_roughness.metallic_factor;

        // TODO: If has roughness map
        // roughness = roughness_map.Sample(static_sampler, i.uv).x;

        float3 F0 = float3(0.04, 0.04, 0.04);
        F0 = lerp(F0, albedo, metallic);

        // Reflectance
        float3 Lo = float3(0.0, 0.0, 0.0);
        //for each light
        {
            float3 light_color = float3(1, 1, 1);

            float3 L = normalize(light_data.light_dir);
            float3 H = normalize(V + L);
            // For point lights
            // float distance = length(light_data.light_pos - i.world_pos);
            float distance = 1.0;
            float attenuation = 1.0 / (distance * distance);
            float3 radiance = light_color * attenuation;

            // cook-torrance brdf
            float NDF = distributionGGX(N, H, roughness);
            float G = geometrySmith(N, V, L, roughness);
            float3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

            float3 kS = F;
            float3 kD = float3(1.0, 1.0, 1.0) - kS;
            kD *= 1.0 - metallic;

            float3 numerator = NDF * G * F;
            float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.00001;
            float3 specular = numerator / denominator;

            float NdotL = max(dot(N, L), 0.0);
            Lo += (kD * albedo / PI + specular) * radiance * NdotL;
        }

        float3 ambient = float3(0.03, 0.03, 0.03) * albedo;
        color = ambient + Lo;
        
        color = color / (color + float3(1.0, 1.0, 1.0));
        color = pow(color, float3(1.0/2.2, 1.0/2.2, 1.0/2.2));
    }
    else // Phong fallback
    {
        float gloss = 1 - roughness;

        // for each light
        {
            float3 light_color = float3(1, 1, 1);

            // Lighting calcs
            float3 L = normalize(light_data.light_dir);
            
            // Calc ambient light
            float3 ambient = float3(0.03, 0.03, 0.03) * albedo;

            // Calc diffuse Light
            float lambert = saturate(dot(N, L));
            float3 diffuse = light_color * lambert * albedo;

            // Calc specular light
            float3 H = normalize(L + V);

            float3 specular_exponent = exp2(gloss * 11) + 2;
            float3 specular = saturate(dot(H, N)) * (lambert > 0); // Blinn-Phong
            specular = pow(specular, specular_exponent) * gloss;
            specular *= light_color;

            color = ambient + diffuse + specular;
        }

    }

    return float4(color, 1.0);
}