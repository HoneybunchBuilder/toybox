#include "common.hlsli"
#include "gltf.hlsli"
#include "lighting.hlsli"

GLTF_MATERIAL_SET(space0)
GLTF_INDIRECT_SET(space1);
GLTF_VIEW_SET(space2);
GLTF_OBJECT_SET(space3);
GLTF_GEOM_SET(space4);

struct VertexIn {
  int vert_idx : SV_VertexID;
  int inst : SV_InstanceID;
};

struct Interpolators {
  float4 clip_pos : SV_POSITION;
  float3 world_pos : POSITION0;
  float3 view_pos : POSITION1;
  float3 normal : NORMAL0;
  float3 tangent : TANGENT0;
  float3 binormal : BINORMAL0;
  float2 uv : TEXCOORD0;
};

Interpolators vert(VertexIn i) {
  int32_t obj_idx = trans_indices[i.inst];
  int32_t vert_perm = object_data[obj_idx].perm;

  // Gather vertex data from supplied descriptors
  // These functions will give us suitable defaults if this object
  // doesn't provide these vertex attributes
  int3 local_pos = tb_vert_get_local_pos(vert_perm, i.vert_idx, pos_buffer);
  half3 normal = tb_vert_get_normal(vert_perm, i.vert_idx, norm_buffer);
  half4 tangent = tb_vert_get_tangent(vert_perm, i.vert_idx, tan_buffer);
  int2 uv0 = tb_vert_get_uv0(vert_perm, i.vert_idx, uv0_buffer);

  float4x4 m = object_data[obj_idx].m; // Per object model matrix
  float3 world_pos = mul(m, float4(local_pos, 1)).xyz;
  float4 clip_pos = mul(camera_data.vp, float4(world_pos, 1.0));

  float3x3 orientation = (float3x3)m;

  Interpolators o;
  o.clip_pos = clip_pos;
  o.world_pos = world_pos.xyz;
  o.view_pos = mul(camera_data.v, float4(world_pos, 1.0)).xyz;
  o.normal = normalize(mul(orientation, normal)); // convert to world-space
  o.tangent = normalize(mul(orientation, tangent.xyz));
  o.binormal = normalize(cross(o.tangent, o.normal) * tangent.w);
  o.uv = uv_transform(uv0, material_data.tex_transform);
  return o;
}

float4 frag(Interpolators i, bool front_face: SV_IsFrontFace) : SV_TARGET {
  float4 base_color = 1;

  // World-space normal
  float3 N = normalize(i.normal);
  if (consts.perm & GLTF_PERM_NORMAL_MAP) {
    // Construct TBN
    float3x3 tbn = float3x3(normalize(i.binormal), normalize(i.tangent),
                            normalize(i.normal));

    // Convert from tangent space to world space
    float3 tangent_space_normal = normal_map.Sample(material_sampler, i.uv).xyz;
    tangent_space_normal = tangent_space_normal * 2 - 1; // Must unpack normal
    N = normalize(mul(tangent_space_normal, tbn));
  }
  N = front_face ? N : -N;

  float3 V = normalize(camera_data.view_pos - i.world_pos);
  float3 R = reflect(-V, N);
  float2 screen_uv = (i.clip_pos.xy / i.clip_pos.w) * 0.5 + 0.5;

  float4 out_color = 0;
  if (consts.perm & GLTF_PERM_PBR_METALLIC_ROUGHNESS) {
    float metallic =
        material_data.pbr_metallic_roughness.metal_rough_factors[0];
    float roughness =
        material_data.pbr_metallic_roughness.metal_rough_factors[1];

    // TODO: Handle alpha masking
    {
      base_color = material_data.pbr_metallic_roughness.base_color_factor;
      if (consts.perm & GLTF_PERM_BASE_COLOR_MAP) {
        base_color *= base_color_map.Sample(material_sampler, i.uv);
      }
      if (consts.perm & GLTF_PERM_ALPHA_CLIP) {
        if (base_color.a < ALPHA_CUTOFF(material_data)) {
          discard;
        }
      }
    }

    if (consts.perm & GLTF_PERM_PBR_METAL_ROUGH_TEX) {
      // The red channel of this texture *may* store occlusion.
      // TODO: Check the perm for occlusion
      float4 mr_sample = metal_rough_map.Sample(material_sampler, i.uv);
      roughness *= mr_sample.g;
      metallic *= mr_sample.b;
    }

    GLTF_OPAQUE_LIGHTING(out_color, base_color, N, V, R, screen_uv, metallic,
                         roughness);
  }

  return out_color;
}
