#include "common.hlsli"
#include "gltf.hlsli"
#include "lighting.hlsli"

// Per-material data - Fragment & Vertex Stages
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
Texture2D shadow_maps[CASCADE_COUNT] : register(t5, space2);       // Frag Only
Texture2D ssao_map : register(s6, space2);

[[vk::constant_id(0)]] const uint PermutationFlags = 0;

struct VertexIn {
  int3 local_pos : SV_POSITION;
  half3 normal : NORMAL0;
  int2 uv : TEXCOORD0;
};

struct Interpolators {
  float4 clip_pos : SV_POSITION;
  float3 world_pos : POSITION0;
  float3 view_pos : POSITION1;
  float3 normal : NORMAL0;
  float2 uv : TEXCOORD0;
  float4 clip : TEXCOORD1;
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
  o.uv = uv_transform(i.uv, material_data.tex_transform);
  o.clip = clip_pos;
  return o;
}

float4 frag(Interpolators i) : SV_TARGET {
  // Sample textures up-front
  float3 albedo = base_color_map.Sample(static_sampler, i.uv).rgb;

  float3 N = normalize(i.normal);
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
      float4 pbr_color_factor =
          material_data.pbr_metallic_roughness.base_color_factor;
      float4 pbr_base_color =
          base_color_map.Sample(static_sampler, i.uv) * pbr_color_factor;
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
        mul(float4(i.world_pos, 1.0), light_data.cascade_vps[cascade_idx]);

    float shadow = pcf_filter(shadow_coord, AMBIENT, shadow_maps[cascade_idx],
                              static_sampler);
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

  return float4(out_color, 1.0);
}