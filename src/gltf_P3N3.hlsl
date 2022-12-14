#include "common.hlsli"
#include "gltf.hlsli"
#include "lighting.hlsli"

// Per-material data - Fragment Stage Only (Maybe vertex stage too later?)
ConstantBuffer<GLTFMaterialData> material_data : register(b0, space0);
Texture2D base_color_map : register(t1, space0); // Fragment Stage Only
Texture2D normal_map
    : register(
          t2,
          space0); // (unusable without per-vertex tangents) Fragment Stage Only
Texture2D metal_rough_map : register(t3, space0); // Fragment Stage Only
// Texture2D emissive_map : register(t4, space0); // Fragment Stage Only
sampler static_sampler : register(s4, space0); // Immutable sampler

// Per-object data - Vertex Stage Only
ConstantBuffer<CommonObjectData> object_data : register(b0, space1);

// Per-view data - Fragment Stage Only
ConstantBuffer<CommonViewData> camera_data : register(b0, space2);
TextureCube irradiance_map : register(t1, space2);  // Fragment Stage Only
TextureCube prefiltered_map : register(t2, space2); // Fragment Stage Only
Texture2D brdf_lut : register(t3, space2);          // Fragment Stage Only
ConstantBuffer<CommonLightData> light_data : register(b4, space2); // Frag Only
Texture2D shadow_maps[4] : register(t5, space2);                   // Frag Only

[[vk::constant_id(0)]] const uint PermutationFlags = 0;

struct VertexIn {
  int3 local_pos : SV_POSITION;
  half3 normal : NORMAL0;
};

struct Interpolators {
  float4 clip_pos : SV_POSITION;
  float3 world_pos : POSITION0;
  float3 view_pos : POSITION1;
  float3 normal : NORMAL0;
};

Interpolators vert(VertexIn i) {
  float3 world_pos = mul(float4(i.local_pos, 1), object_data.m).xyz;
  float4 clip_pos = mul(float4(world_pos, 1.0), camera_data.vp);

  float3x3 orientation = (float3x3)object_data.m;

  Interpolators o;
  o.clip_pos = clip_pos;
  o.world_pos = world_pos;
  o.view_pos = mul(float4(world_pos, 1.0), camera_data.v).xyz;
  o.normal = mul(i.normal, orientation); // convert to world-space normal
  return o;
}

float4 frag(Interpolators i) : SV_TARGET {
  // TODO: Get material base color color some other way
  float3 base_color = float3(0.5, 0.5, 0.5);

  float3 N = normalize(i.normal);

  float3 V = normalize(camera_data.view_pos - i.world_pos);
  float NdotV = clamp(abs(dot(N, V)), 0.001, 1.0);

  float3 out_color = float3(0.0, 0.0, 0.0);

  float3 L = light_data.light_dir;

  if (PermutationFlags & GLTF_PERM_PBR_METALLIC_ROUGHNESS) {
    float metallic = material_data.pbr_metallic_roughness.metallic_factor;
    float roughness = material_data.pbr_metallic_roughness.roughness_factor;

    float alpha_roughness = roughness * roughness;

    float3 f0 = float3(0.04, 0.04, 0.04);

    float3 diffuse_color = base_color * (float3(1.0, 1.0, 1.0) - f0);
    diffuse_color *= 1.0 - metallic;

    float3 specular_color = lerp(f0, base_color, metallic);
    float reflectance =
        max(max(specular_color.r, specular_color.g), specular_color.b);

    // For typical incident reflectance range (between 4% to 100%) set the
    // grazing reflectance to 100% for typical fresnel effect. For very low
    // reflectance range on highly diffuse objects (below 4%), incrementally
    // reduce grazing reflecance to 0%.
    float reflectance_90 = clamp(reflectance * 25.0, 0.0, 1.0);
    float3 specular_environment_R0 = specular_color;
    float3 specular_environment_R90 = float3(1.0, 1.0, 1.0) * reflectance_90;

    // for each light
    {
      float3 light_color = light_data.color;

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

    // Ambient IBL
    {
      float3 R = reflect(-V, N);

      float3 reflection =
          prefiltered_reflection(prefiltered_map, static_sampler, R, roughness);
      float3 irradiance = irradiance_map.Sample(static_sampler, N).rgb;
      float3 diffuse = irradiance * base_color;

      float3 kS =
          fresnel_schlick_roughness(max(dot(N, V), 0.0f), f0, roughness);

      float2 brdf =
          brdf_lut
              .Sample(static_sampler, float2(max(dot(N, V), 0.0), roughness))
              .rg;
      float3 specular = reflection * (kS * brdf.x + brdf.y);

      float3 kD = (1.0 - kS);
      kD *= 1.0f - metallic;
      float3 ambient = (kD * diffuse) + specular;

      out_color += ambient;
    }

    // Shadow cascades
    {
      uint cascade_idx = 0;
      for (uint c = 0; c < 3; ++c) {
        if (i.view_pos.z < light_data.cascade_splits[c]) {
          cascade_idx = c + 1;
        }
      }

      float4 shadow_coord =
          mul(float4(i.world_pos, 1.0), light_data.cascade_vps[cascade_idx]);

      float NdotL = clamp(dot(N, L), 0.001, 1.0);
      float shadow = pcf_filter(shadow_coord, AMBIENT, shadow_maps,
                                static_sampler, NdotL, cascade_idx);
      out_color *= shadow;

      /*
      switch(cascade_idx)
      {
        case 0:
          out_color.rgb *= float3(1.0f, 0.25f, 0.25f);
        break;
        case 1:
          out_color.rgb *= float3(0.25f, 1.0f, 0.25f);
        break;
        case 2:
          out_color.rgb *= float3(0.25f, 0.25f, 1.0f);
        break;
        case 3:
          out_color.rgb *= float3(1.0f, 1.0f, 0.25f);
        break;
      }
      */
    }
  } else // Phong fallback
  {
    float gloss = 0.5f;

    // for each light
    {
      float3 H = normalize(V + L);

      float3 light_color = light_data.color;
      out_color += phong_light(base_color, light_color, gloss, N, L, V, H);
    }
  }

  return float4(out_color, 1.0);
}