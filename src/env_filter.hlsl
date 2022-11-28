#include "common.hlsli"

#include "cube_view_lut.hlsli"

TextureCube env_texture : register(t0, space0);  // Fragment Stage Only
SamplerState env_sampler : register(s0, space0); // Fragment Stage Only

[[vk::push_constant]] ConstantBuffer<EnvFilterConstants> consts
    : register(b1, space0); // Fragment Stage Only

struct VertexIn {
  float3 local_pos : SV_POSITION;
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
  o.view_pos.xy *= -1.0;
  o.clip_pos = mul(float4(i.local_pos, 1.0), vp);
  return o;
}

// Based on
// http://byteblacksmith.com/improvements-to-the-canonical-one-liner-glsl-rand-for-opengl-es-2-0/
float random(float2 co) {
  float a = 12.9898;
  float b = 78.233;
  float c = 43758.5453;
  float dt = dot(co.xy, float2(a, b));
  float sn = fmod(dt, 3.14);
  return frac(sin(sn) * c);
}

float2 hammersley_2d(uint i, uint N) {
  // Radical inverse based on
  // http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html
  uint bits = (i << 16u) | (i >> 16u);
  bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
  bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
  bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
  bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
  float rdi = float(bits) * 2.3283064365386963e-10;
  return float2(float(i) / float(N), rdi);
}

// Based on
// http://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_slides.pdf
float3 importance_sample_ggx(float2 Xi, float roughness, float3 normal) {
  // Maps a 2D point to a hemisphere with spread based on roughness
  float alpha = roughness * roughness;
  float phi = 2.0 * PI * Xi.x + random(normal.xz) * 0.1;
  float cos_theta = sqrt((1.0 - Xi.y) / (1.0 + (alpha * alpha - 1.0) * Xi.y));
  float sin_theta = sqrt(1.0 - cos_theta * cos_theta);
  float3 H = float3(sin_theta * cos(phi), sin_theta * sin(phi), cos_theta);

  // Tangent space
  float3 up =
      abs(normal.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
  float3 tan_x = normalize(cross(up, normal));
  float3 tan_y = normalize(cross(normal, tan_x));

  // Convert to world Space
  return normalize(tan_x * H.x + tan_y * H.y + tan_y * H.z);
}

// Normal Distribution function
float d_ggx(float dot_NH, float roughness) {
  float alpha = roughness * roughness;
  float alpha2 = alpha * alpha;
  float denom = dot_NH * dot_NH * (alpha2 - 1.0) + 1.0;
  return (alpha2) / (PI * denom * denom);
}

float3 prefilter_env_map(float3 R, float roughness) {
  float3 N = R;
  float3 V = R;
  float3 color = float3(0.0, 0.0, 0.0);
  float total_weight = 0.0;
  int2 env_map_dims;
  env_texture.GetDimensions(env_map_dims.x, env_map_dims.y);
  float env_map_dim = float(env_map_dims.x);
  for (uint i = 0u; i < consts.sample_count; i++) {
    float2 Xi = hammersley_2d(i, consts.sample_count);
    float3 H = importance_sample_ggx(Xi, roughness, N);
    float3 L = 2.0 * dot(V, H) * H - V;
    float dot_NL = clamp(dot(N, L), 0.0, 1.0);
    if (dot_NL > 0.0) {
      // Filtering based on
      // https://placeholderart.wordpress.com/2015/07/28/implementation-notes-runtime-environment-map-filtering-for-image-based-lighting/

      float dot_NH = clamp(dot(N, H), 0.0, 1.0);
      float dot_VH = clamp(dot(V, H), 0.0, 1.0);

      // Probability Distribution Function
      float pdf = d_ggx(dot_NH, roughness) * dot_NH / (4.0 * dot_VH) + 0.0001;
      // Slid angle of current smple
      float omega_s = 1.0 / (float(consts.sample_count) * pdf);
      // Solid angle of 1 pixel across all cube faces
      float omega_p = 4.0 * PI / (6.0 * env_map_dim * env_map_dim);
      // Biased (+1.0) mip level for better result
      float mip_level = roughness == 0.0
                            ? 0.0
                            : max(0.5 * log2(omega_s / omega_p) + 1.0, 0.0f);
      color += env_texture.SampleLevel(env_sampler, L, mip_level).rgb * dot_NL;
      total_weight += dot_NL;
    }
  }
  return (color / total_weight);
}

float4 frag(Interpolators i) : SV_TARGET {
  float3 N = normalize(i.view_pos);
  return float4(prefilter_env_map(N, consts.roughness), 1.0);
}