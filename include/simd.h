#pragma once

/*
  Notes on mathematic standards used
  * Coordinate system is right handed. GLTF is right handed
  * +Y is up, +X is right and -Z is forward
  * Matrices are column major. HLSL and SPIRV may consider them row major but
  they are not
*/

#ifndef TB_USE_INVERSE_DEPTH
// #define TB_USE_INVERSE_DEPTH 1
#endif

// Do nothing if this is a shader
#ifndef __HLSL_VERSION

#include <float.h>
#include <stdbool.h>
#include <stdint.h>

typedef float __attribute__((vector_size(16))) float4;
typedef float __attribute__((vector_size(16))) float3;
typedef float __attribute__((vector_size(8))) float2;

typedef double __attribute__((vector_size(32))) double4;
typedef double __attribute__((vector_size(32))) double3;
typedef double __attribute__((vector_size(16))) double2;

typedef int32_t __attribute__((vector_size(16))) int4;
typedef int32_t __attribute__((vector_size(16))) int3;
typedef int32_t __attribute__((vector_size(8))) int2;

typedef uint32_t __attribute__((vector_size(16))) uint4;
typedef uint32_t __attribute__((vector_size(16))) uint3;
typedef uint32_t __attribute__((vector_size(8))) uint2;
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

typedef float4 Quaternion;

typedef struct Transform {
  float3 position;
  float3 scale;
  Quaternion rotation;
} Transform;

typedef struct Plane {
  float4 xyzw;
} Plane;

typedef struct Sphere {
  float3 center;
  float radius;
} Sphere;

typedef struct AABB {
  float3 min;
  float3 max;
} AABB;

// Must forward declare this for helper function;
typedef struct cgltf_node cgltf_node;

typedef enum FrustumPlane {
  TopPlane,
  BottomPlane,
  LeftPlane,
  RightPlane,
  NearPlane,
  FarPlane,
  FrustumPlaneCount
} FrustumPlane;

typedef struct Frustum {
  Plane planes[FrustumPlaneCount];
} Frustum;

float3 atof3(const float f[3]);
float4 atof4(const float f[4]);

float2 f2(float x, float y);
float3 f3(float x, float y, float z);
float4 f4(float x, float y, float z, float w);
float3 f4tof3(float4 f);
float4 f3tof4(float3 f, float w);
float2 f3tof2(float3 f);
float3x4 m44tom34(float4x4 m);
float3x3 m44tom33(float4x4 m);
float4x4 m33tom44(float3x3 m);

float dotf2(float2 x, float2 y);
float dotf3(float3 x, float3 y);
float dotf4(float4 x, float4 y);
float3 crossf3(float3 x, float3 y);

float magf2(float2 v);
float magf3(float3 v);
float magf4(float4 v);
float magsqf3(float3 v);
float magsqf4(float4 v);
float norm_angle(float a);
float2 normf2(float2 v);
float3 normf3(float3 v);
float4 normf4(float4 v);
Quaternion normq(Quaternion q);

float lenf3(float3 v);

float3x3 mf33_identity(void);
float3x4 mf34_identity(void);
float4x4 mf44_identity(void);

float4 mulf44(float4x4 m, float4 v);
float4 mul4f44f(float4 v, float4x4 m);

float3 mulf33(float3x3 m, float3 v);

float4x4 mulmf44(float4x4 x, float4x4 y);
float4x4 inv_mf44(float4x4 m);
float4x4 transpose_mf44(float4x4 m);
float3x3 mf33_from_axes(float3 forward, float3 right, float3 up);

Quaternion mf33_to_quat(float3x3 m);
Quaternion quat_from_axes(float3 forward, float3 right, float3 up);
Quaternion angle_axis_to_quat(float4 angle_axis);
float3x3 quat_to_mf33(Quaternion quat);
float4x4 quat_to_trans(Quaternion quat);

Quaternion mulq(Quaternion p, Quaternion q);
float3 qrotf3(Quaternion q, float3 v);

AABB aabb_init(void);
void aabb_add_point(AABB *aabb, float3 point);
float aabb_get_width(AABB aabb);
float aabb_get_height(AABB aabb);
float aabb_get_depth(AABB aabb);
AABB aabb_transform(float4x4 m, AABB aabb);

void translate(Transform *t, float3 p);
void scale(Transform *t, float3 s);
void rotate(Transform *t, Quaternion r);

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
#define TB_ORIGIN                                                              \
  (float3) { 0 }
#define TB_FORWARD                                                             \
  (float3) { 0, 0, -1 }
#define TB_BACKWARD                                                            \
  (float3) { 0, 0, 1 }
#define TB_LEFT                                                                \
  (float3) { -1, 0, 0 }
#define TB_RIGHT                                                               \
  (float3) { 1, 0, 0 }
#define TB_UP                                                                  \
  (float3) { 0, 1, 0 }
#define TB_DOWN                                                                \
  (float3) { 0, -1, 0 }

// X is left to right, Y is down to up and Z is front to back
#define TB_WIDTH_IDX 0
#define TB_HEIGHT_IDX 1
#define TB_DEPTH_IDX 2

float3 transform_get_forward(const Transform *t);
float3 transform_get_right(const Transform *t);
float3 transform_get_up(const Transform *t);

float4x4 transform_to_matrix(const Transform *t);
Transform tb_transform_from_node(const cgltf_node *node);

float4x4 look_forward(float3 pos, float3 forward, float3 up);
float4x4 look_at(float3 pos, float3 target, float3 up);

float4x4 perspective(float fovy, float aspect, float zn, float zf);
float4x4 orthographic(float r, float l, float t, float b, float zn, float zf);

Frustum frustum_from_view_proj(const float4x4 *vp);

bool frustum_test_aabb(const Frustum *frust, const AABB *aabb);

float deg_to_rad(float d);
float rad_to_deg(float r);

float lerpf(float v0, float v1, float a);
float3 lerpf3(float3 v0, float3 v1, float a);

Quaternion slerp(Quaternion q1, Quaternion q2, float a);

float clampf(float v, float min, float max);
float3 clampf3(float3 v, float3 min, float3 max);

float tb_randf(float min, float max);

#endif
