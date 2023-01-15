#pragma once

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

typedef struct float4x4 {
  union {
    struct {
      float4 row0;
      float4 row1;
      float4 row2;
      float4 row3;
    };
    float4 rows[4];
  };
} float4x4;

typedef struct float3x4 {
  union {
    struct {
      float4 row0;
      float4 row1;
      float4 row2;
    };
    float4 rows[3];
  };
} float3x4;

typedef struct float3x3 {
  union {
    struct {
      float3 row0;
      float3 row1;
      float3 row2;
    };
    float3 rows[3];
  };
} float3x3;

typedef float4 Quaternion;
typedef float3 EulerAngles;

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

static const AABB InvalidAABB = {
    .min = {FLT_MAX, FLT_MAX, FLT_MAX},
    .max = {FLT_MIN, FLT_MIN, FLT_MIN},
};

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

float3 f4tof3(float4 f);
float4 f3tof4(float3 f, float w);
float3x4 m44tom34(float4x4 m);

float dotf2(float2 x, float2 y);
float dotf3(float3 x, float3 y);
float dotf4(float4 x, float4 y);
float3 crossf3(float3 x, float3 y);

float magf2(float2 v);
float magf3(float3 v);
float magf4(float4 v);
float magsqf3(float3 v);
float magsqf4(float4 v);
float2 normf2(float2 v);
float3 normf3(float3 v);
float4 normf4(float4 v);

float lenf3(float3 v);

void mf33_identity(float3x3 *m);
void mf34_identity(float3x4 *m);
void mf44_identity(float4x4 *m);

void mulf33(float3x3 *m, float3 v);
void mulf34(float3x4 *m, float4 v);

float4 mulf44(float4x4 m, float4 v);
float4 mul4f44f(float4 v, float4x4 m);

void mulmf34(const float3x4 *x, const float3x4 *y, float3x4 *o);
void mulmf44(const float4x4 *x, const float4x4 *y, float4x4 *o);

float4x4 inv_mf44(float4x4 m);

EulerAngles quat_to_euler(Quaternion quat);
Quaternion euler_to_quat(EulerAngles xyz);

float4x4 euler_to_trans(EulerAngles euler);
float4x4 quat_to_trans(Quaternion quat);

Quaternion mulq(Quaternion p, Quaternion q);

AABB aabb_init(void);
void aabb_add_point(AABB *aabb, float3 point);

void translate(Transform *t, float3 p);
void scale(Transform *t, float3 s);
void rotate(Transform *t, Quaternion r);

void transform_to_matrix(float4x4 *m, const Transform *t);
Transform tb_transform_from_node(const cgltf_node *node);

void look_forward(float4x4 *m, float3 pos, float3 forward, float3 up);
void look_at(float4x4 *m, float3 pos, float3 target, float3 up);
void perspective(float4x4 *m, float fovy, float aspect, float zn, float zf);
void reverse_perspective(float4x4 *m, float fovy, float aspect, float zn,
                         float zf);
float4x4 orthographic(float r, float l, float t, float b, float zn, float zf);

Frustum frustum_from_view_proj(const float4x4 *vp);

bool frustum_test_aabb(const Frustum *frust, const AABB *aabb);

float tb_deg_to_rad(float d);
float tb_rad_to_deg(float r);

float tb_lerpf(float v0, float v1, float a);
