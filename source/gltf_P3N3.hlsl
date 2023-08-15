#include "common.hlsli"
#include "gltf.hlsli"
#include "lighting.hlsli"

GLTF_MATERIAL_SET(space0)
GLTF_OBJECT_SET(space1);
GLTF_VIEW_SET(space2);

struct VertexIn {
  int3 local_pos : SV_POSITION;
  half3 normal : NORMAL0;
};

struct Interpolators {
  float4 clip_pos : SV_POSITION;
  float3 world_pos : POSITION0;
  float3 view_pos : POSITION1;
  float3 normal : NORMAL0;
  float4 clip : TEXCOORD0;
};

Interpolators vert(VertexIn i) {
  float3 world_pos = mul(object_data.m, float4(i.local_pos, 1)).xyz;
  float4 clip_pos = mul(camera_data.vp, float4(world_pos, 1.0));

  float3x3 orientation = (float3x3)object_data.m;

  Interpolators o;
  o.clip_pos = clip_pos;
  o.world_pos = world_pos;
  o.view_pos = mul(camera_data.v, float4(world_pos, 1.0)).xyz;
  o.normal = mul(orientation, i.normal); // convert to world-space normal
  o.clip = clip_pos;
  return o;
}

float4 frag(Interpolators i, bool front_face : SV_IsFrontFace) : SV_TARGET {
  // TODO: Get material base color color some other way
  float3 albedo = float3(0.5, 0.5, 0.5);

  float3 N = normalize(i.normal);
  N = front_face ? N : -N;
  float3 V = normalize(camera_data.view_pos - i.world_pos);
  float3 R = reflect(-V, N);
  float3 L = light_data.light_dir;
  float2 screen_uv = (i.clip.xy / i.clip.w) * 0.5 + 0.5;

  float3 out_color = float3(0.0, 0.0, 0.0);
  float alpha = 1.0f;

  if (consts.perm & GLTF_PERM_PBR_METALLIC_ROUGHNESS) {
    float metallic =
        material_data.pbr_metallic_roughness.metal_rough_factors[0];
    float roughness =
        material_data.pbr_metallic_roughness.metal_rough_factors[1];

    // TODO: Handle alpha masking
    {
      float4 pbr_base_color =
          material_data.pbr_metallic_roughness.base_color_factor;

      albedo = pbr_base_color.rgb;
      alpha = pbr_base_color.a;
      if (consts.perm & GLTF_PERM_ALPHA_CLIP) {
        if (alpha < ALPHA_CUTOFF(material_data)) {
          discard;
        }
      }
    }

    // Lighting
    {
      float2 brdf =
          brdf_lut
              .Sample(shadow_sampler, float2(max(dot(N, V), 0.0), roughness))
              .rg;
      float3 reflection = prefiltered_reflection(
          prefiltered_map, filtered_env_sampler, R, roughness);
      float3 irradiance =
          irradiance_map.SampleLevel(filtered_env_sampler, N, 0).rgb;

      float ao = ssao_map.Sample(shadow_sampler, screen_uv).r;
      out_color =
          pbr_lighting(ao, albedo, metallic, roughness, brdf, reflection,
                       irradiance, light_data.color, L, V, N);
    }
  } else {
    // Phong fallback
    float gloss = 0.5f;

    // for each light
    {
      float3 H = normalize(V + L);
      out_color += phong_light(albedo, light_data.color, gloss, N, L, V, H);
    }
  }

  // Shadow cascades
  {
    uint cascade_idx = 0;
    for (uint c = 0; c < (TB_CASCADE_COUNT - 1); ++c) {
      if (i.view_pos.z < light_data.cascade_splits[c]) {
        cascade_idx = c + 1;
      }
    }

    float4 shadow_coord =
        mul(light_data.cascade_vps[cascade_idx], float4(i.world_pos, 1.0));

    float NdotL = clamp(dot(N, L), 0.001, 1.0);
    float shadow =
        pcf_filter(shadow_coord, shadow_map, cascade_idx, shadow_sampler);
    out_color *= shadow;

    /*
    switch (cascade_idx) {
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

  // Add emissive
  float3 emissive_factor = material_data.emissives.rgb;
  float emissive_strength = material_data.emissives.w;
  out_color += emissive_factor * emissive_strength;

  return float4(out_color, alpha);
}