#include "common.hlsli"
#include "gltf.hlsli"
#include "lighting.hlsli"

GLTF_MATERIAL_SET(space0)
GLTF_OBJECT_SET(space1);
GLTF_VIEW_SET(space2);

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
  o.binormal = normalize(cross(o.tangent, o.normal) * i.tangent.w);
  o.uv = uv_transform(i.uv, material_data.tex_transform);
  o.clip = clip_pos;
  return o;
}

float4 frag(Interpolators i, bool front_face : SV_IsFrontFace) : SV_TARGET {
  // Sample textures up-front
  float3 albedo = float3(0.5, 0.5, 0.5);

  // World-space normal
  float3 N = normalize(i.normal);
  if (consts.perm & GLTF_PERM_NORMAL_MAP) {
    // Construct TBN
    float3x3 tbn = float3x3(normalize(i.tangent), normalize(i.binormal),
                            normalize(i.normal));

    // Convert from tangent space to world space
    float3 tangent_space_normal = normal_map.Sample(material_sampler, i.uv).xyz;
    tangent_space_normal = tangent_space_normal * 2 - 1; // Must unpack normal
    N = normalize(mul(tangent_space_normal, tbn));
  }
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
      if (consts.perm & GLTF_PERM_BASE_COLOR_MAP) {
        pbr_base_color *= base_color_map.Sample(material_sampler, i.uv);
      }

      albedo = pbr_base_color.rgb;
      alpha = pbr_base_color.a;
      if (consts.perm & GLTF_PERM_ALPHA_CLIP) {
        if (alpha < ALPHA_CUTOFF(material_data)) {
          discard;
        }
      }
    }

    if (consts.perm & GLTF_PERM_PBR_METAL_ROUGH_TEX) {
      // The red channel of this texture *may* store occlusion.
      // TODO: Check the perm for occlusion
      float4 mr_sample = metal_rough_map.Sample(material_sampler, i.uv);
      roughness = mr_sample.g * roughness;
      metallic = mr_sample.b * metallic;
    }

    // Lighting
    {
      float2 brdf =
          brdf_lut.Sample(brdf_sampler, float2(max(dot(N, V), 0.0), roughness))
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
    for (uint c = 0; c < (TB_CASCADE_COUNT - 1); ++c) {
      if (i.view_pos.z < light_data.cascade_splits[c]) {
        cascade_idx = c + 1;
      }
    }

    float4 shadow_coord =
        mul(light_data.cascade_vps[cascade_idx], float4(i.world_pos, 1.0));

    float shadow =
        pcf_filter(shadow_coord, shadow_map, cascade_idx, shadow_sampler);
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

  // Add emissive
  float3 emissive_factor = material_data.emissives.rgb;
  float emissive_strength = material_data.emissives.w;
  out_color += emissive_factor * emissive_strength;

  return float4(out_color, alpha);
}