// Adapted heavily from https://catlikecoding.com/unity/tutorials/flow/waves/

#include "pbr.hlsli"
#include "lighting.hlsli"

// Contains the vertex stage
#include "oceancommon.hlsli"

float4 frag(Interpolators i) : SV_TARGET
{
  float3 base_color = float3(0.7, 0.7, 1);
  float3 light_dir = float3(0, 1, 0);

  // Calculate normal after interpolation
  float3 N = normalize(cross(normalize(i.binormal), normalize(i.tangent)));
  float3 V = normalize(camera_data.view_pos - i.world_pos);
  float NdotV = clamp(abs(dot(N, V)), 0.001, 1.0);

  float3 color = float3(0.0f, 0.0f, 0.0f);

  // PBR Lighting
  {
    float metallic = 0.0;
    float roughness = 0.5;

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
      float3 light_color = float3(0.8, 0.8, 0.8);
      float3 L = light_dir;

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
  }

  // Gamma correction
  //float gamma = 2.2f; // TODO: pass in as a parameter
  //color = pow(color, float3(1.0f / gamma, 1.0f / gamma, 1.0f / gamma));

  return float4(color, 0.9);
}
