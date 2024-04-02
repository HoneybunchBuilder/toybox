#include "common.hlsli"
#include "gltf.hlsli"
#include "lighting.hlsli"

GLTF_VIEW_SET(space0);
GLTF_MATERIAL_SET(space1);
GLTF_DRAW_SET(space2);
TB_OBJECT_SET(space3);
TB_TEXTURE_SET(space4);
TB_IDX_SET(space5)
TB_POS_SET(space6);
TB_NORM_SET(space7);
TB_TAN_SET(space8);
TB_UV0_SET(space9);
TB_JOINT_SET(space10);
TB_WEIGHT_SET(space11);

struct VertexIn {
  int32_t vert_idx : SV_VertexID;
  [[vk::builtin("DrawIndex")]]
  uint32_t draw_idx : POSITION0;
};

struct Interpolators {
  float4 clip_pos : SV_POSITION;
  float3 world_pos : POSITION0;
  float3 view_pos : POSITION1;
  float3 normal : NORMAL0;
  float3 tangent : TANGENT0;
  float3 binormal : BINORMAL0;
  float2 uv : TEXCOORD0;
  uint32_t mat_idx : TEXCOORD1;
};

Interpolators vert(VertexIn i) {
  TbGLTFDrawData draw = tb_get_gltf_draw_data(i.draw_idx, draw_data);
  int32_t vert_perm = draw.perm;
  uint32_t obj_idx = draw.obj_idx;
  uint32_t mesh_idx = draw.mesh_idx;
  uint32_t mat_idx = draw.mat_idx;

  TbCommonObjectData obj_data = tb_get_obj_data(obj_idx, object_data);
  TbGLTFMaterialData gltf = tb_get_gltf_mat_data(mat_idx, gltf_data);
  int32_t mat_perm = gltf.perm;

  // Gather vertex data from supplied descriptors
  // These functions will give us suitable defaults if this object
  // doesn't provide these vertex attributes
  int32_t idx =
      tb_get_idx(i.vert_idx + draw.index_offset, mesh_idx, idx_buffers) +
      draw.vertex_offset;
  int3 local_pos = tb_vert_get_local_pos(vert_perm, idx, mesh_idx, pos_buffers);
  float3 normal = tb_vert_get_normal(vert_perm, idx, mesh_idx, norm_buffers);
  float4 tangent = tb_vert_get_tangent(vert_perm, idx, mesh_idx, tan_buffers);
  int2 uv0 = tb_vert_get_uv0(vert_perm, idx, mesh_idx, uv0_buffers);

  float3 world_pos = mul(obj_data.m, float4(local_pos, 1)).xyz;
  float4 clip_pos = mul(camera_data.vp, float4(world_pos, 1.0));

  float3x3 orientation = (float3x3)obj_data.m;

  Interpolators o;
  o.clip_pos = clip_pos;
  o.world_pos = world_pos.xyz;
  o.view_pos = mul(camera_data.v, float4(world_pos, 1.0)).xyz;
  if ((vert_perm & TB_INPUT_PERM_NORMAL) > 0) {
    o.normal = normalize(mul(orientation, normal)); // convert to world-space
  }
  if ((vert_perm & TB_INPUT_PERM_TANGENT) > 0) {
    o.tangent = normalize(mul(orientation, tangent.xyz));
    o.binormal = normalize(cross(o.tangent, o.normal) * tangent.w);
  }
  o.uv = uv_transform(uv0, gltf.tex_transform);
  o.mat_idx = mat_idx;
  return o;
}

float4 frag(Interpolators i, bool front_face: SV_IsFrontFace) : SV_TARGET {
  TbGLTFMaterialData gltf = tb_get_gltf_mat_data(i.mat_idx, gltf_data);

  float4 base_color = 1;

  // World-space normal
  float3 N = normalize(i.normal);
  if ((gltf.perm & GLTF_PERM_NORMAL_MAP) > 0) {
    // Construct TBN
    float3x3 tbn = float3x3(normalize(i.binormal), normalize(i.tangent),
                            normalize(i.normal));

    // Convert from tangent space to world space
    float3 tangent_space_normal = tb_get_texture(gltf.normal_idx, gltf_textures)
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
  if ((gltf.perm & GLTF_PERM_PBR_METALLIC_ROUGHNESS) > 0) {
    PBRMetallicRoughness mr = gltf.pbr_metallic_roughness;
    float metallic = mr.metal_rough_factors[0];
    float roughness = mr.metal_rough_factors[1];

    // TODO: Handle alpha masking
    {
      base_color = mr.base_color_factor;
      if ((gltf.perm & GLTF_PERM_BASE_COLOR_MAP) > 0) {
        base_color *= tb_get_texture(gltf.color_idx, gltf_textures)
                          .Sample(material_sampler, i.uv);
      }
      if ((gltf.perm & GLTF_PERM_ALPHA_CLIP) > 0) {
        if (base_color.a < ALPHA_CUTOFF(gltf)) {
          discard;
        }
      }
    }

    if ((gltf.perm & GLTF_PERM_PBR_METAL_ROUGH_TEX) > 0) {
      // The red channel of this texture *may* store occlusion.
      // TODO: Check the perm for occlusion
      float4 mr_sample = tb_get_texture(gltf.pbr_idx, gltf_textures)
                             .Sample(material_sampler, i.uv);
      roughness *= mr_sample.g;
      metallic *= mr_sample.b;
    }

    GLTF_OPAQUE_LIGHTING(out_color, base_color, N, V, R, screen_uv, metallic,
                         roughness);
  }

  return out_color;
}
