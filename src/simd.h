#pragma once

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

typedef struct Transform {
  float3 position;
  float3 scale;
  float3 rotation;
} Transform;

float3 f4tof3(float4 f);
float4 f3tof4(float3 f, float w);
float3x4 m44tom34(float4x4 m);

float dotf3(float3 x, float3 y);
float dotf4(float4 x, float4 y);
float3 crossf3(float3 x, float3 y);

float magf3(float3 v);
float magsqf3(float3 v);
float3 normf3(float3 v);

float lenf3(float3 v);

void mf33_identity(float3x3 *m);
void mf34_identity(float3x4 *m);
void mf44_identity(float4x4 *m);

float4 mulf44(float4x4 m, float4 v);

void mulmf34(const float3x4 *x, const float3x4 *y, float3x4 *o);
void mulmf44(const float4x4 *x, const float4x4 *y, float4x4 *o);

float4x4 inv_mf44(float4x4 m);

void translate(Transform *t, float3 p);
void scale(Transform *t, float3 s);
void rotate(Transform *t, float3 r);

void transform_to_matrix(float4x4 *m, const Transform *t);

void look_forward(float4x4 *m, float3 pos, float3 forward, float3 up);
void look_at(float4x4 *m, float3 pos, float3 target, float3 up);
void perspective(float4x4 *m, float fovy, float aspect, float zn, float zf);
void orthographic(float4x4 *m, float width, float height, float zn, float zf);
