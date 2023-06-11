#include "common.hlsli"
#include "gltf.hlsli"
#include "lighting.hlsli"

ConstantBuffer<GLTFMaterialData> material_data : register(b0, space0);
Texture2D base_color_map : register(t1, space0);
Texture2D normal_map : register(t2, space0);
Texture2D metal_rough_map : register(t3, space0);
// Texture2D emissive_map : register(t4, space0);
sampler static_sampler : register(s4, space0);

ConstantBuffer<CommonObjectData> object_data : register(b0, space1);

ConstantBuffer<CommonViewData> camera_data : register(b0, space2);
TextureCube irradiance_map : register(t1, space2);
TextureCube prefiltered_map : register(t2, space2);
Texture2D brdf_lut : register(t3, space2);
ConstantBuffer<CommonLightData> light_data : register(b4, space2);
Texture2D shadow_maps[CASCADE_COUNT] : register(t5, space2);
Texture2D ssao_map : register(s6, space2);

[[vk::constant_id(0)]] const uint PermutationFlags = 0;

struct VertexIn {
  int3 local_pos : SV_POSITION;
  half3 normal : NORMAL0;
  half4 tangent : TANGENT0;
  int2 uv : TEXCOORD0;
};

struct Interpolators {
  float4 clip_pos : SV_POSITION;
  float3 world_pos : POSITION0;
  float3 view_pos : POSITION1;
  float3 normal : NORMAL0;
  float3 tangent : TANGENT0;
  float3 binormal : BINORMAL0;
  float2 uv : TEXCOORD0;
  float4 clip : TEXCOORD1;
};

Interpolators vert(VertexIn i) {
  float3 world_pos = mul(object_data.m, float4(i.local_pos, 1)).xyz;
  float4 clip_pos = mul(camera_data.vp, float4(world_pos, 1.0));

  float3x3 orientation = (float3x3)object_data.m;

  Interpolators o;
  o.clip_pos = clip_pos;
  o.world_pos = world_pos.xyz;
  o.view_pos = mul(camera_data.v, float4(world_pos, 1.0)).xyz;
  o.normal = normalize(mul(orientation, i.normal)); // convert to world-space
  o.tangent = normalize(mul(orientation, i.tangent.xyz));
  o.binormal = normalize(cross(o.normal, o.tangent));
  o.uv = uv_transform(i.uv, material_data.tex_transform);
  o.clip = clip_pos;
  return o;
}

float4 frag(Interpolators i) : SV_TARGET {
  // Sample textures up-front
  float3 albedo = float3(0.5, 0.5, 0.5);

  // World-space normal
  float3 N = normalize(i.normal);
  if (PermutationFlags & GLTF_PERM_NORMAL_MAP) {
    // Construct TBN
    float3x3 tbn = float3x3(normalize(i.tangent), normalize(i.binormal),
                            normalize(i.normal));

    // Convert from tangent space to world space
    float3 tangentSpaceNormal = normal_map.Sample(static_sampler, i.uv).xyz;
    tangentSpaceNormal =
        normalize(tangentSpaceNormal * 2 - 1); // Must unpack normal
    N = normalize(mul(tbn, tangentSpaceNormal));
  }

  float3 V = normalize(camera_data.view_pos - i.world_pos);
  float3 R = reflect(-V, N);
  float3 L = light_data.light_dir;
  float2 screen_uv = (i.clip.xy / i.clip.w) * 0.5 + 0.5;

  float3 out_color = float3(0.0, 0.0, 0.0);

  if (PermutationFlags & GLTF_PERM_PBR_METALLIC_ROUGHNESS) {
    float metallic = material_data.pbr_metallic_roughness.metallic_factor;
    float roughness = material_data.pbr_metallic_roughness.roughness_factor;

    // TODO: Handle alpha masking
    {
      float4 pbr_base_color =
          material_data.pbr_metallic_roughness.base_color_factor;
      if (PermutationFlags & GLTF_PERM_BASE_COLOR_MAP) {
        pbr_base_color *= base_color_map.Sample(static_sampler, i.uv);
      }

      albedo = pbr_base_color.rgb;
    }

    if (PermutationFlags & GLTF_PERM_PBR_METAL_ROUGH_TEX) {
      // The red channel of this texture *may* store occlusion.
      // TODO: Check the perm for occlusion
      float4 mr_sample = metal_rough_map.Sample(static_sampler, i.uv);
      roughness = mr_sample.g * roughness;
      metallic = mr_sample.b * metallic;
    }

    // Lighting
    {
      float2 brdf =
          brdf_lut
              .Sample(static_sampler, float2(max(dot(N, V), 0.0), roughness))
              .rg;
      float3 reflection =
          prefiltered_reflection(prefiltered_map, static_sampler, R, roughness);
      float3 irradiance = irradiance_map.Sample(static_sampler, N).rgb;
      float ao = ssao_map.Sample(static_sampler, screen_uv).r;
      out_color =
          pbr_lighting(ao, albedo, metallic, roughness, brdf, reflection,
                       irradiance, light_data.color, L, V, N);
    }
  } else {
    // Phong fallback
    float gloss = 0.5;

    // for each light
    {
      float3 H = normalize(V + L);

      float3 light_color = light_data.color;
      out_color += phong_light(albedo, light_color, gloss, N, L, V, H);
    }
  }

  // Shadow cascades
  {
    uint cascade_idx = 0;
    for (uint c = 0; c < (CASCADE_COUNT - 1); ++c) {
      if (i.view_pos.z < light_data.cascade_splits[c]) {
        cascade_idx = c + 1;
      }
    }

    float4 shadow_coord =
        mul(light_data.cascade_vps[cascade_idx], float4(i.world_pos, 1.0));

    float shadow = pcf_filter(shadow_coord, AMBIENT, shadow_maps[cascade_idx],
                              static_sampler);
    out_color *= shadow;
  }

  // Fog
  {
    float b = 0.0001f;

    float distance = length(camera_data.view_pos - i.world_pos);
    float fog_amount = 1.0 - exp(-distance * b);
    float sun_amount = max(dot(V, -L), 0.0);
    float3 fog_color = lerp(float3(0.5, 0.6, 0.7), float3(1.0, 0.9, 0.7),
                            pow(sun_amount, 8.0));
    out_color = lerp(out_color, fog_color, saturate(fog_amount));
  }

  return float4(out_color, 1);
}