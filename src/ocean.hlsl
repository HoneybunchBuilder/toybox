// Adapted heavily from https://catlikecoding.com/unity/tutorials/flow/waves/

#include "lighting.hlsli"
#include "pbr.hlsli"

// Contains the vertex stage
#include "oceancommon.hlsli"

TextureCube irradiance_map : register(t1, space1);  // Fragment Stage Only
TextureCube prefiltered_map : register(t2, space1); // Fragment Stage Only
Texture2D brdf_lut : register(t3, space1);          // Fragment Stage Only
ConstantBuffer<CommonLightData> light_data : register(b4, space1); // Frag Only

float4 frag(Interpolators i) : SV_TARGET {
  float3 L = light_data.light_dir;

  // Calculate normal after interpolation
  float3 N = normalize(cross(normalize(i.binormal), normalize(i.tangent)));
  float3 V = normalize(camera_data.view_pos - i.world_pos);
  float NdotV = clamp(abs(dot(N, V)), 0.001, 1.0);

  float3 base_color = float3(0, 0, 0);

  // Underwater fog
  {
    // HACK: Need to paramaterize these
    const float near = 0.1f;
    const float far = 1000.0f;
    const float fog_density = 0.15f;
    const float3 fog_color = float3(0.305, 0.513, 0.662);

    const float2 uv = i.screen_pos.xy / i.screen_pos.w;

    float background_depth =
        linear_depth(depth_map.Sample(static_sampler, uv).r, near, far);
    float surface_depth = depth_from_clip_z(i.screen_pos.z, near, far);

    float depth_diff = background_depth - surface_depth;

    float3 background_color = color_map.Sample(static_sampler, uv).rgb;
    float fog = exp2(fog_density * depth_diff);

    base_color = lerp(fog_color, background_color, fog);
  }

  // PBR Lighting
  float3 color = float3(0, 0, 0);
  {
    float metallic = 0.0;
    float roughness = 0.5;

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
      float3 light_color = float3(1, 1, 1);

      PBRLight light = {
          light_color,
          L,
          specular_environment_R0,
          specular_environment_R90,
          alpha_roughness,
          diffuse_color,
      };

      color += pbr_lighting(light, N, V, NdotV);
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

      color += ambient;
    }
  }

  return float4(color, 1);
}
