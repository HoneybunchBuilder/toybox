#include "common.hlsli"
#include "gltf.hlsli"
#include "lighting.hlsli"

GLTF_INDIRECT_SET(space0);
GLTF_VIEW_SET(space1);
GLTF_OBJECT_SET(space2);
GLTF_MESH_SET(space3);
GLTF_TEXTURE_SET(space4);
GLTF_MATERIAL_SET(space5);

[[vk::push_constant]]
ConstantBuffer<GLTFPushConstants> consts : register(b0, space6);

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
  int32_t obj_idx : TEXCOORD1;
};

Interpolators vert(VertexIn i) {
  int32_t obj_idx = trans_indices[i.inst];
  TbCommonObjectData obj_data = object_data[NonUniformResourceIndex(obj_idx)];
  int32_t vert_perm = obj_data.perm;
  int32_t mat_idx = consts.mat_idx;
  GLTFMaterialData gltf = gltf_data[NonUniformResourceIndex(mat_idx)][0];
  int32_t mat_perm = gltf.perm;

  // Gather vertex data from supplied descriptors
  // These functions will give us suitable defaults if this object
  // doesn't provide these vertex attributes
  int3 local_pos = tb_vert_get_local_pos(vert_perm, i.vert_idx, pos_buffer);
  float3 normal = tb_vert_get_normal(vert_perm, i.vert_idx, norm_buffer);
  float4 tangent = tb_vert_get_tangent(vert_perm, i.vert_idx, tan_buffer);
  int2 uv0 = tb_vert_get_uv0(vert_perm, i.vert_idx, uv0_buffer);

  float4x4 m = object_data[NonUniformResourceIndex(obj_idx)]
                   .m; // Per object model matrix
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
  o.uv = uv_transform(uv0, gltf.tex_transform);
  o.obj_idx = obj_idx;
  return o;
}

float4 frag(Interpolators i, bool front_face: SV_IsFrontFace) : SV_TARGET {
  TbCommonObjectData obj_data = object_data[NonUniformResourceIndex(i.obj_idx)];
  int32_t mat_idx = consts.mat_idx;
  GLTFMaterialData gltf = gltf_data[NonUniformResourceIndex(mat_idx)][0];
  int32_t mat_perm = gltf.perm;

  float4 base_color = 1;

  // World-space normal
  float3 N = normalize(i.normal);
  if ((mat_perm & GLTF_PERM_NORMAL_MAP) > 0) {
    // Construct TBN
    float3x3 tbn = float3x3(normalize(i.binormal), normalize(i.tangent),
                            normalize(i.normal));

    // Convert from tangent space to world space
    float3 tangent_space_normal =
        gltf_textures[NonUniformResourceIndex(gltf.normal_idx)]
            .Sample(material_sampler, i.uv)
            .xyz;
    tangent_space_normal = tangent_space_normal * 2 - 1; // Must unpack normal
    N = normalize(mul(tangent_space_normal, tbn));
  }
  N = front_face ? N : -N;

  float3 V = normalize(camera_data.view_pos - i.world_pos);
  float3 R = reflect(-V, N);
  float2 screen_uv = (i.clip_pos.xy / i.clip_pos.w) * 0.5 + 0.5;

  float4 out_color = 0;
  if ((mat_perm & GLTF_PERM_PBR_METALLIC_ROUGHNESS) > 0) {
    PBRMetallicRoughness mr = gltf.pbr_metallic_roughness;
    float metallic = mr.metal_rough_factors[0];
    float roughness = mr.metal_rough_factors[1];

    // TODO: Handle alpha masking
    {
      base_color = mr.base_color_factor;
      if ((mat_perm & GLTF_PERM_BASE_COLOR_MAP) > 0) {
        base_color *=
            gltf_textures[NonUniformResourceIndex(gltf.color_idx)].Sample(
                material_sampler, i.uv);
      }
      if (mat_perm & GLTF_PERM_ALPHA_CLIP) {
        if (base_color.a < ALPHA_CUTOFF(gltf)) {
          discard;
        }
      }
    }

    if ((mat_perm & GLTF_PERM_PBR_METAL_ROUGH_TEX) > 0) {
      // The red channel of this texture *may* store occlusion.
      // TODO: Check the perm for occlusion
      float4 mr_sample =
          gltf_textures[NonUniformResourceIndex(gltf.pbr_idx)].Sample(
              material_sampler, i.uv);
      roughness *= mr_sample.g;
      metallic *= mr_sample.b;
    }

    GLTF_OPAQUE_LIGHTING(out_color, base_color, N, V, R, screen_uv, metallic,
                         roughness);
  }

  return out_color;
}
