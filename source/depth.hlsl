#include "common.hlsli"
#include "gltf.hlsli"
#include "shadow.hlsli"

GLTF_INDIRECT_SET(space0);
GLTF_VIEW_SET(space1);
GLTF_OBJECT_SET(space2);

struct VertexIn {
  int3 local_pos : SV_POSITION;
  int inst : SV_InstanceID;
};

struct Interpolators {
  float4 clip_pos : SV_POSITION;
};

Interpolators vert(VertexIn i) {
  int32_t trans_idx = trans_indices[i.inst];
  float4x4 m = object_data[trans_idx].m;
  float3 world_pos = mul(m, float4(i.local_pos, 1)).xyz;
  float4 clip_pos = mul(camera_data.vp, float4(world_pos, 1.0));

  Interpolators o;
  o.clip_pos = clip_pos;
  return o;
}

void frag(Interpolators i) {}