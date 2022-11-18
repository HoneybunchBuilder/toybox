#include "gltf.hlsli"
#include "common.hlsli"
#include "lighting.hlsli"

// Heavily based off
// https://microsoft.github.io/DirectX-Specs/d3d/MeshShader.html
// and
// https://github.com/microsoft/DirectX-Graphics-Samples/blob/master/Samples/Desktop/D3D12MeshShaders/src/MeshletRender/MeshletMS.hlsl

// matches meshopt_Meshlet
struct Meshlet {
  uint vert_count;
  uint vert_offset;
  uint prim_count;
  uint prim_offset;
};

// Per-material data - Fragment & Mesh Stages
ConstantBuffer<GLTFMaterialData> material_data : register(b0, space0);
// Fragment Stage Only
Texture2D base_color_map : register(t1, space0);
Texture2D normal_map : register(t2, space0);
Texture2D metal_rough_map : register(t3, space0);
// Texture2D emissive_map                       : register(t4, space0);
// Immutable sampler
sampler static_sampler : register(s4, space0);

// Per-object data - Mesh Stage Only
ConstantBuffer<CommonObjectData> object_data : register(b0, space1);
StructuredBuffer<int3> positions : register(t1, space1);
StructuredBuffer<half3> normals : register(t2, space1);
StructuredBuffer<half4> tangents : register(t3, space1);
StructuredBuffer<int2> uvs : register(t3, space1);
StructuredBuffer<Meshlet> meshlets : register(t4, space1);
ByteAddressBuffer unique_vert_indices : register(t5, space1);
StructuredBuffer<uint> primitive_indices : register(t6, space1);

// Per-view data - Fragment Stage Only
ConstantBuffer<CommonViewData> camera_data : register(b0, space2);
TextureCube irradiance_map : register(t1, space2);
// ConstantBuffer<CommonLightData> light_data : register(b1, space2);
// Texture2D shadow_map                       : register(t2, space2);
// SamplerState shadow_sampler                : register(s2, space2);

[[vk::constant_id(0)]] const uint PermutationFlags = 0;
[[vk::constant_id(1)]] const uint MeshFlags = 0;

#define THREADS_X 96
#define THREADS_Y 1
#define THREADS_Z 1

#define MAX_VERTS 252
#define MAX_PRIMS (MAX_VERTS / 3)

// Members of this struct will be interpolated
struct VertexAttributes {
  float4 clip_pos : SV_POSITION;
  float3 world_pos : POSITION0;
  float3 normal : NORMAL0;
  float3 tangent : TANGENT0;
  float3 binormal : BINORMAL0;
  float2 uv : TEXCOORD0;
  // float4 shadowcoord : TEXCOORD1;
};
// An optional PrimitiveAttributes struct can contain attributes that
// are never interpolated

// Use a separate fragment input struct
// Members that belong to the optional primitive attributes
// must be decorated with [[vk::perprimitive]]
struct FragmentInput {
  float4 clip_pos : SV_POSITION;
  float3 world_pos : POSITION0;
  float3 normal : NORMAL0;
  float3 tangent : TANGENT0;
  float3 binormal : BINORMAL0;
  float2 uv : TEXCOORD0;
};

uint3 get_primitive(Meshlet m, uint index) {
  uint primitive = primitive_indices[m.prim_offset + index];
  // Unpacks a 10 bits per index triangle from a 32-bit uint.
  return uint3(primitive & 0x3FF, (primitive >> 10) & 0x3FF,
               (primitive >> 20) & 0x3FF);
}

uint get_vert_index(Meshlet m, uint local_index) {
  local_index += m.vert_offset;

  if (object_data.index_bytes == 4) {
    return unique_vert_indices.Load(local_index * 4);
  } else {
    // Byte address must be 4-byte aligned.
    uint word_offset = (local_index & 0x1);
    uint byte_offset = (local_index / 2) * 4;

    // Grab the pair of 16-bit indices, shift & mask off proper 16-bits.
    uint index_pair = unique_vert_indices.Load(byte_offset);
    uint index = (index_pair >> (word_offset * 16)) & 0xffff;

    return index;
  }
}

VertexAttributes index_vert(uint32_t meshlet_index, uint vertex_index) {
  VertexAttributes o;

  int3 local_pos = positions[vertex_index];
  float3 world_pos = mul(float4(local_pos, 1), object_data.m).xyz;
  float4 clip_pos = mul(float4(world_pos, 1.0), camera_data.vp);
  o.clip_pos = clip_pos;
  o.world_pos = world_pos.xyz;

  if (MeshFlags & VA_INPUT_PERM_NORMAL || MeshFlags & VA_INPUT_PERM_TANGENT) {
    float3x3 orientation = (float3x3)object_data.m;
    if (MeshFlags & VA_INPUT_PERM_NORMAL) {
      half3 normal = normals[vertex_index];
      o.normal = normalize(mul(normal, orientation)); // convert to world-space
    }
    if (MeshFlags & VA_INPUT_PERM_TANGENT) {
      half4 tangent = tangents[vertex_index];
      o.tangent = normalize(mul(orientation, tangent.xyz));
      o.binormal = cross(o.tangent, o.normal) * tangent.w;
    }
  }
  if (MeshFlags & VA_INPUT_PERM_TEXCOORD0) {
    int2 uv = uvs[vertex_index];
    o.uv = uv_transform(uv, material_data.tex_transform);
  }

  return o;
}

groupshared uint indices[MAX_VERTS];

[numthreads(THREADS_X, THREADS_Y, THREADS_Z)]
[outputtopology("triangle")]
void mesh(in uint group_thread_id: SV_GroupThreadID,
          in uint group_id: SV_GroupID,
          out indices uint3 triangles[MAX_PRIMS],
          out vertices VertexAttributes verts[MAX_VERTS]){
  Meshlet m = meshlets[object_data.meshlet_offset + group_id];

  SetMeshOutputCounts(m.vert_count, m.prim_count);

  if (group_thread_id < m.prim_count) {
    triangles[group_thread_id] = get_primitive(m, group_thread_id);
  }

  if (group_thread_id < m.vert_count) {
    uint vert_index = get_vert_index(m, group_thread_id);
    verts[group_thread_id] = index_vert(group_id, vert_index);
  }
}

float4 frag(FragmentInput i) : SV_TARGET {
  float3 base_color = float3(0.5, 0.5, 0.5);

  float3 out_color = float3(0.0, 0.0, 0.0);

  // Technically we shouldn't need a normal channel to be able to 
  // do some unlit vertex colors but ignore that for now
  if(MeshFlags & VA_INPUT_PERM_NORMAL)
  {
    // World-space normal
    float3 N = normalize(i.normal);
    if (PermutationFlags & GLTF_PERM_NORMAL_MAP &&
        MeshFlags & VA_INPUT_PERM_TANGENT &&
        MeshFlags & VA_INPUT_PERM_TEXCOORD0) {
      // Construct TBN
      float3x3 tbn = float3x3(normalize(i.tangent), normalize(i.binormal),
                              normalize(i.normal));

      // Convert from tangent space to world space
      float3 tangentSpaceNormal = normal_map.Sample(static_sampler, i.uv).xyz;
      tangentSpaceNormal =
          normalize(tangentSpaceNormal * 2 - 1); // Must unpack normal
      N = normalize(mul(tangentSpaceNormal, tbn));
    }

    // Per view calcs
    float3 V = normalize(camera_data.view_pos - i.world_pos);
    float NdotV = clamp(abs(dot(N, V)), 0.001, 1.0);
    float3 reflection = -normalize(reflect(V, N));
    reflection.y *= -1.0;

    float3 light_dir = normalize(float3(0.707, 0.707, 0));

    if (PermutationFlags & GLTF_PERM_PBR_METALLIC_ROUGHNESS) {
      float metallic = material_data.pbr_metallic_roughness.metallic_factor;
      float roughness = material_data.pbr_metallic_roughness.roughness_factor;

      // TODO: Handle alpha masking
      {
        float4 pbr_base_color =
            material_data.pbr_metallic_roughness.base_color_factor;
        if (PermutationFlags & GLTF_PERM_BASE_COLOR_MAP && 
            MeshFlags & VA_INPUT_PERM_TEXCOORD0) {
          pbr_base_color *= base_color_map.Sample(static_sampler, i.uv);
        }

        base_color = pbr_base_color.rgb;
      }

      if (PermutationFlags & GLTF_PERM_PBR_METAL_ROUGH_TEX && 
          MeshFlags & VA_INPUT_PERM_TEXCOORD0) {
        // The red channel of this texture *may* store occlusion.
        // TODO: Check the perm for occlusion
        float4 mr_sample = metal_rough_map.Sample(static_sampler, i.uv);
        roughness = mr_sample.g * roughness;
        metallic = mr_sample.b * metallic;
      }

      float alpha_roughness = roughness * roughness;

      float3 f0 = float3(0.4, 0.4, 0.4);

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
        float3 L = light_dir;

        PBRLight light = {
            light_color,
            L,
            specular_environment_R0,
            specular_environment_R90,
            alpha_roughness,
            diffuse_color,
        };

        out_color += pbr_lighting(light, N, V, NdotV);
      }

      // Ambient IBL
      {
        const float ao = 1.0f;
        float3 kS = fresnel_schlick_roughness(NdotV, f0, roughness);
        float3 kD = 1.0 - kS;
        float3 irradiance = irradiance_map.Sample(static_sampler, N).rgb;
        float exposure = 4.5f; // TODO: pass in as a parameter
        irradiance = tonemap(irradiance * exposure);
        irradiance *= 1.0f / tonemap(float3(11.2f, 11.2f, 11.2f));
        float3 diffuse = irradiance * base_color;
        float3 ambient = (kD * diffuse) * ao;

        out_color += ambient;
      }

      // TODO: Ambient Occlusion

      // TODO: Emissive Texture
    } else // Phong fallback
    {
      float gloss = 0.5;

      // for each light
      {
        float3 L = light_dir;
        float3 H = normalize(V + L);

        float3 light_color = float3(1, 1, 1);
        out_color += phong_light(base_color, light_color, gloss, N, L, V, H);
      }
    }
  }

  // Shadow hack
  /*
  float3 L = normalize(light_data.light_dir);
  float NdotL = clamp(dot(N, L), 0.001, 1.0);

  float shadow = pcf_filter(i.shadowcoord, AMBIENT, shadow_map, shadow_sampler,
  NdotL); out_color *= shadow;
  */

  return float4(out_color, 1);
}