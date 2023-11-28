#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __HLSL_VERSION
#define TB_GPU_STRUCT
#else
#define TB_GPU_STRUCT __attribute__((aligned(16))) __attribute__((packed))
#endif

/*
  Notes on mathematic standards used
  * Coordinate system is right handed. GLTF is right handed
  * +Y is up, +X is right and -Z is forward
  * Matrices are column major. HLSL and SPIRV may consider them row major but
  they are not
*/

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-anonymous-struct"
#pragma clang diagnostic ignored "-Wnested-anon-types"

#ifndef TB_USE_INVERSE_DEPTH
// #define TB_USE_INVERSE_DEPTH 1
#endif

// Do nothing if this is a shader
#ifndef __HLSL_VERSION

#include <float.h>
#include <stdbool.h>
#include <stdint.h>

typedef float __attribute__((ext_vector_type(4))) float4;
typedef float __attribute__((ext_vector_type(3))) float3;
typedef float __attribute__((ext_vector_type(2))) float2;

typedef double __attribute__((ext_vector_type(4))) double4;
typedef double __attribute__((ext_vector_type(3))) double3;
typedef double __attribute__((ext_vector_type(2))) double2;

typedef int32_t __attribute__((ext_vector_type(4))) int4;
typedef int32_t __attribute__((ext_vector_type(3))) int3;
typedef int32_t __attribute__((ext_vector_type(2))) int2;

typedef uint32_t __attribute__((ext_vector_type(4))) uint4;
typedef uint32_t __attribute__((ext_vector_type(3))) uint3;
typedef uint32_t __attribute__((ext_vector_type(2))) uint2;
typedef uint32_t uint; // For hlsl compatibility

// All matrices are column major since HLSL expects that by default

typedef struct float4x4 {
  union {
    struct {
      float4 col0;
      float4 col1;
      float4 col2;
      float4 col3;
    };
    float4 cols[4];
  };
} float4x4;

typedef struct float3x4 {
  union {
    struct {
      float4 col0;
      float4 col1;
      float4 col2;
    };
    float4 cols[3];
  };
} float3x4;

typedef struct float3x3 {
  union {
    struct {
      float3 col0;
      float3 col1;
      float3 col2;
    };
    float3 cols[3];
  };
} float3x3;

typedef float4 TbQuaternion;

typedef struct TbTransform {
  float3 position;
  float3 scale;
  TbQuaternion rotation;
} TbTransform;

typedef struct TbPlane {
  float4 xyzw;
} TbPlane;

typedef struct TbSphere {
  float3 center;
  float radius;
} TbSphere;

typedef struct TbAABB {
  float3 min;
  float3 max;
} TbAABB;

// Must forward declare this for helper function;
typedef struct cgltf_node cgltf_node;

typedef enum TbFrustumPlane {
  TopPlane,
  BottomPlane,
  LeftPlane,
  RightPlane,
  NearPlane,
  FarPlane,
  FrustumPlaneCount
} TbFrustumPlane;

typedef struct TbFrustum {
  TbPlane planes[FrustumPlaneCount];
} TbFrustum;

#define TB_FRUSTUM_CORNER_COUNT 8
static const float3 tb_frustum_corners[TB_FRUSTUM_CORNER_COUNT] = {
#ifdef TB_USE_INVERSE_DEPTH
    {-1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, -1.0f}, // Near
    {1.0f, -1.0f, 1.0f}, {-1.0f, -1.0f, -1.0f},
    {-1.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, // Far
    {1.0f, -1.0f, 0.0f}, {-1.0f, -1.0f, 0.0f},
#else
    {-1.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, // Near
    {1.0f, -1.0f, 0.0f}, {-1.0f, -1.0f, 0.0f},
    {-1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, // Far
    {1.0f, -1.0f, 1.0f}, {-1.0f, -1.0f, 1.0f},
#endif
};

// Reminder: Left Handed coordinate space
#define TB_ORIGIN tb_f3(0, 0, 0)
#define TB_FORWARD tb_f3(0, 0, -1)
#define TB_BACKWARD tb_f3(0, 0, 1)
#define TB_LEFT tb_f3(-1, 0, 0)
#define TB_RIGHT tb_f3(1, 0, 0)
#define TB_UP tb_f3(0, 1, 0)
#define TB_DOWN tb_f3(0, -1, 0)

// X is left to right, Y is down to up and Z is front to back
#define TB_WIDTH_IDX 0
#define TB_HEIGHT_IDX 1
#define TB_DEPTH_IDX 2

float3 tb_atof3(const float f[3]);
float4 tb_atof4(const float f[4]);

float2 tb_f2(float x, float y);
float3 tb_f3(float x, float y, float z);
float4 tb_f4(float x, float y, float z, float w);
float3 tb_f4tof3(float4 f);
float4 tb_f3tof4(float3 f, float w);
float2 tb_f3tof2(float3 f);
float3x4 tb_f44tof34(float4x4 m);
float3x3 tb_f44tof33(float4x4 m);
float4x4 tb_f33tof44(float3x3 m);

float tb_dotf2(float2 x, float2 y);
float tb_dotf3(float3 x, float3 y);
float tb_dotf4(float4 x, float4 y);
float3 tb_crossf3(float3 x, float3 y);

float tb_magf2(float2 v);
float tb_magf3(float3 v);
float tb_magf4(float4 v);
float tb_magsqf2(float2 v);
float tb_magsqf3(float3 v);
float tb_magsqf4(float4 v);
float tb_norm_angle(float a);
float2 tb_normf2(float2 v);
float3 tb_normf3(float3 v);
float4 tb_normf4(float4 v);
TbQuaternion tb_normq(TbQuaternion q);

TbTransform tb_trans_identity(void);

float3x3 tb_f33_identity(void);
float3x4 tb_f34_identity(void);
float4x4 tb_f44_identity(void);

float4 tb_mulf44f4(float4x4 m, float4 v);
float4 tb_mulf4f44(float4 v, float4x4 m);

float3 tb_mulf33f3(float3x3 m, float3 v);

float4x4 tb_mulf44f44(float4x4 x, float4x4 y);
float4x4 tb_invf44(float4x4 m);
float4x4 tb_transpose_f44(float4x4 m);
float3x3 tb_f33_from_axes(float3 forward, float3 right, float3 up);

TbQuaternion tb_f33_to_quat(float3x3 m);
TbQuaternion tb_quat_from_axes(float3 forward, float3 right, float3 up);
TbQuaternion tb_angle_axis_to_quat(float4 angle_axis);
float3x3 tb_quat_to_f33(TbQuaternion quat);
float4x4 tb_quat_to_f44(TbQuaternion quat);

TbQuaternion tb_mulq(TbQuaternion q, TbQuaternion p);
float3 tb_qrotf3(TbQuaternion q, float3 v);

bool tb_f4eq(float4 x, float4 y);
bool tb_f3eq(float3 x, float3 y);

bool tb_f33_eq(const float3x3 *x, const float3x3 *y);
bool tb_f44_eq(const float4x4 *x, const float4x4 *y);

bool tb_trans_eq(const TbTransform *x, const TbTransform *y);

TbAABB tb_aabb_init(void);
void tb_aabb_add_point(TbAABB *aabb, float3 point);
float tb_aabb_get_width(TbAABB aabb);
float tb_aabb_get_height(TbAABB aabb);
float tb_aabb_get_depth(TbAABB aabb);
TbAABB tb_aabb_rotate(TbQuaternion q, TbAABB aabb);
TbAABB tb_aabb_transform(float4x4 m, TbAABB aabb);

void tb_translate(TbTransform *t, float3 p);
void tb_scale(TbTransform *t, float3 s);
void tb_rotate(TbTransform *t, TbQuaternion r);

float3 tb_safe_reciprocal(float3 v);
TbQuaternion tb_inv_quat(TbQuaternion q);

TbTransform tb_inv_trans(TbTransform t);

float3 tb_transform_get_forward(const TbTransform *t);
float3 tb_transform_get_right(const TbTransform *t);
float3 tb_transform_get_up(const TbTransform *t);

TbTransform tb_transform_combine(const TbTransform *x, const TbTransform *y);
float4x4 tb_transform_to_matrix(const TbTransform *t);

TbTransform tb_transform_from_node(const cgltf_node *node);

float4x4 tb_look_forward(float3 pos, float3 forward, float3 up);
float4x4 tb_look_at(float3 pos, float3 target, float3 up);

TbQuaternion tb_look_forward_quat(float3 forward, float3 up);
TbQuaternion tb_look_at_quat(float3 pos, float3 target, float3 up);

TbTransform tb_look_forward_transform(float3 pos, float3 forward, float3 up);
TbTransform tb_look_at_transform(float3 pos, float3 target, float3 up);

float4x4 tb_perspective(float fovy, float aspect, float zn, float zf);
float4x4 tb_orthographic(float r, float l, float t, float b, float zn,
                         float zf);

TbFrustum tb_frustum_from_view_proj(const float4x4 *vp);

bool tb_frustum_test_aabb(const TbFrustum *frust, const TbAABB *aabb);

float tb_deg_to_rad(float d);
float tb_rad_to_deg(float r);

float tb_lerpf(float f0, float f1, float a);
float3 tb_lerpf3(float3 v0, float3 v1, float a);

TbQuaternion tb_slerp(TbQuaternion q0, TbQuaternion q1, float a);

TbTransform tb_trans_lerp(TbTransform t0, TbTransform t1, float a);

float tb_clampf(float v, float min, float max);
float3 tb_clampf3(float3 v, float3 min, float3 max);

#endif

#pragma clang diagnostic pop

#ifdef __cplusplus
}
#endif
