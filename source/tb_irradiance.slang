#include "tb_cube_view_lut.slangh"
#include "tb_common.slangh"
#include "tb_pbr.slangh"

[[vk::binding(0, 0)]]
TextureCube env_map;
[[vk::binding(1, 0)]]
SamplerState material_sampler;

struct VertexIn {
  float3 local_pos : POSITION0;
  uint view_idx : SV_ViewID;
};

struct Interpolators {
  float4 clip_pos : SV_POSITION;
  float3 view_pos : TEXCOORD0;
};

Interpolators vert(VertexIn i) {
  float4x4 vp = view_proj_lut[i.view_idx];

  Interpolators o;
  o.view_pos = i.local_pos;
  o.clip_pos = mul(vp, float4(i.local_pos, 1.0));
  return o;
}

// See https://learnopengl.com/PBR/IBL/Diffuse-irradiance
float4 frag(Interpolators i) : SV_TARGET {
  float3 normal = normalize(i.view_pos);
  float3 up = float3(0, 1, 0);
  float3 right = normalize(cross(up, normal));
  up = normalize(cross(normal, right));

  const float phi_delta = TB_TAU / 32.0f;
  const float theta_delta = TB_PI_2 / 8.0f;
  uint sample_count = 0u;

  float3 irradiance = float3(0, 0, 0);
  for (float phi = 0.0f; phi < TB_TAU; phi += phi_delta) {
    for (float theta = 0.0f; theta < TB_PI_2; theta += theta_delta) {
      float3 tan_sample =
          float3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
      float3 sample_vec =
          tan_sample.x * right + tan_sample.y * up + tan_sample.z * normal;

      // Clamp each sample to 1 to avoid blowout
      irradiance += min(env_map.Sample(material_sampler, sample_vec).rgb, 1) *
                    cos(theta) * sin(theta);
      sample_count++;
    }
  }
  irradiance = TB_PI * irradiance / float(sample_count);

  return float4(irradiance, 1);
}
