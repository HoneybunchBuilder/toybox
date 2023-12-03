#include "common.hlsli"
#include "gltf.hlsli"
#include "shadow.hlsli"

GLTF_INDIRECT_SET(space0);
GLTF_VIEW_SET(space1);
GLTF_OBJECT_SET(space2);
GLTF_MESH_SET(space3);

struct VertexIn {
  int32_t vert_idx : SV_VertexID;
  int32_t inst : SV_InstanceID;
};

struct Interpolators {
  float4 clip_pos : SV_POSITION;
};

Interpolators vert(VertexIn i) {
  TbCommonObjectData obj_data = object_data[trans_indices[i.inst]];

  int3 local_pos = tb_vert_get_local_pos(obj_data.perm, i.vert_idx, pos_buffer);

  float3 world_pos = mul(obj_data.m, float4(local_pos, 1)).xyz;
  float4 clip_pos = mul(camera_data.vp, float4(world_pos, 1.0));

  Interpolators o;
  o.clip_pos = clip_pos;
  return o;
}

void frag(Interpolators i) {}
