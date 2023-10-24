#include "common.hlsli"
#include "gltf.hlsli"
#include "lighting.hlsli"

GLTF_MATERIAL_SET(space0)
GLTF_INDIRECT_SET(space1);
GLTF_VIEW_SET(space2);
GLTF_OBJECT_SET(space3);

struct VertexIn {
  int3 local_pos : SV_POSITION;
  int inst : SV_InstanceID;
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
  int32_t trans_idx = trans_indices[i.inst];
  float4x4 m = object_data[trans_idx].m;
  float3 world_pos = mul(m, float4(i.local_pos, 1)).xyz;
  float4 clip_pos = mul(camera_data.vp, float4(world_pos, 1.0));

  float3x3 orientation = (float3x3)m;

  Interpolators o;
  o.clip_pos = clip_pos;
  o.world_pos = world_pos;
  o.view_pos = mul(camera_data.v, float4(world_pos, 1.0)).xyz;
  o.normal = mul(orientation, i.normal); // convert to world-space normal
  o.clip = clip_pos;
  return o;
}

float4 frag(Interpolators i, bool front_face : SV_IsFrontFace) : SV_TARGET {
  float4 base_color = 1;

  float3 N = normalize(i.normal);
  N = front_face ? N : -N;

  float3 V = normalize(camera_data.view_pos - i.world_pos);
  float3 R = reflect(-V, N);
  float2 screen_uv = (i.clip.xy / i.clip.w) * 0.5 + 0.5;

  float4 out_color = 0;
  if (consts.perm & GLTF_PERM_PBR_METALLIC_ROUGHNESS) {
    float metallic =
        material_data.pbr_metallic_roughness.metal_rough_factors[0];
    float roughness =
        material_data.pbr_metallic_roughness.metal_rough_factors[1];
    {
      base_color = material_data.pbr_metallic_roughness.base_color_factor;
      if (consts.perm & GLTF_PERM_ALPHA_CLIP) {
        if (base_color.a < ALPHA_CUTOFF(material_data)) {
          discard;
        }
      }
    }

    GLTF_OPAQUE_LIGHTING(out_color, base_color, N, V, R, screen_uv, metallic,
                         roughness);
  }

  return out_color;
}