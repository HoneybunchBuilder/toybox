#include "simd.h"

#define _USE_MATH_DEFINES
#include <assert.h>
#include <math.h>

#include <stdbool.h>

#include "profiling.h"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-braces"
#endif

#ifdef __clang__
#define unroll_loop_3 _Pragma("clang loop unroll_count(3)")
#define unroll_loop_4 _Pragma("clang loop unroll_count(4)")
#endif

#ifdef __GNUC__
#define unroll_loop_3 _Pragma("GCC unroll 3")
#define unroll_loop_4 _Pragma("GCC unroll 4")
#endif

float3 f4tof3(float4 f) { return (float3){f[0], f[1], f[2]}; }
float4 f3tof4(float3 f, float w) { return (float4){f[0], f[1], f[2], w}; }

float3x4 m44tom34(float4x4 m) {
  return (float3x4){
      .row0 = m.row0,
      .row1 = m.row1,
      .row2 = m.row2,
  };
}

float dotf3(float3 x, float3 y) {
  return (x[0] * y[0]) + (x[1] * y[1]) + (x[2] * y[2]);
}

float dotf4(float4 x, float4 y) {
  return (x[0] * y[0]) + (x[1] * y[1]) + (x[2] * y[2]) + (x[3] * y[3]);
}

float3 crossf3(float3 x, float3 y) {
  return (float3){
      (x[1] * y[2]) - (x[2] * y[1]),
      (x[2] * y[0]) - (x[0] * y[2]),
      (x[0] * y[1]) - (x[1] * y[0]),
  };
}

float magf3(float3 v) {
  return sqrtf((v[0] * v[0]) + (v[1] * v[1]) + (v[2] * v[2]));
}

float magf4(float4 v) {
  return sqrtf((v[0] * v[0]) + (v[1] * v[1]) + (v[2] * v[2]) + (v[3] * v[3]));
}

float magsqf3(float3 v) {
  return (v[0] * v[0]) + (v[1] * v[1]) + (v[2] * v[2]);
}

float magsqf4(float3 v) {
  return (v[0] * v[0]) + (v[1] * v[1]) + (v[2] * v[2]) + (v[3] * v[3]);
}

float3 normf3(float3 v) {
  float invSum = 1 / magf3(v);
  return (float3){v[0] * invSum, v[1] * invSum, v[2] * invSum};
}

void mf33_identity(float3x3 *m) {
  assert(m);
  *m = (float3x3){
      (float3){1, 0, 0},
      (float3){0, 1, 0},
      (float3){0, 0, 1},
  };
}

void mf34_identity(float3x4 *m) {
  assert(m);
  *m = (float3x4){
      (float4){1, 0, 0, 0},
      (float4){0, 1, 0, 0},
      (float4){0, 0, 1, 0},
  };
}

void mf44_identity(float4x4 *m) {
  assert(m);
  *m = (float4x4){
      (float4){1, 0, 0, 0},
      (float4){0, 1, 0, 0},
      (float4){0, 0, 1, 0},
      (float4){0, 0, 0, 1},
  };
}

void mulf33(float3x3 *m, float3 v) {
  assert(m);
  unroll_loop_3 for (uint32_t i = 0; i < 3; ++i) {
    float s = v[i];
    m->row0[i] *= s;
    m->row1[i] *= s;
    m->row2[i] *= s;
  }
}

void mulf34(float3x4 *m, float4 v) {
  assert(m);
  unroll_loop_4 for (uint32_t i = 0; i < 4; ++i) {
    float s = v[i];
    m->row0[i] *= s;
    m->row1[i] *= s;
    m->row2[i] *= s;
  }
}

void mulf44(float4x4 *m, float4 v) {
  assert(m);
  unroll_loop_4 for (uint32_t i = 0; i < 4; ++i) {
    float s = v[i];
    m->row0[i] *= s;
    m->row1[i] *= s;
    m->row2[i] *= s;
    m->row3[i] *= s;
  }
}

void mulmf34(const float3x4 *x, const float3x4 *y, float3x4 *o) {
  assert(x);
  assert(y);
  assert(o);
  unroll_loop_3 for (uint32_t i = 0; i < 3; ++i) {
    unroll_loop_4 for (uint32_t ii = 0; ii < 4; ++ii) {
      float s = 0.0f;
      unroll_loop_3 for (uint32_t iii = 0; iii < 3; ++iii) {
        s += x->rows[i][iii] * y->rows[iii][ii];
      }
      if (ii == 3) {
        s += x->rows[i][3];
      }
      o->rows[i][ii] = s;
    }
  }
}

void mulmf44(const float4x4 *x, const float4x4 *y, float4x4 *o) {
  TracyCZoneN(ctx, "mulmf44", true);
  TracyCZoneColor(ctx, TracyCategoryColorMath);
  assert(x);
  assert(y);
  assert(o);
  unroll_loop_4 for (uint32_t i = 0; i < 4; ++i) {
    unroll_loop_4 for (uint32_t ii = 0; ii < 4; ++ii) {
      float s = 0.0f;
      unroll_loop_4 for (uint32_t iii = 0; iii < 4; ++iii) {
        s += x->rows[i][iii] * y->rows[iii][ii];
      }
      o->rows[i][ii] = s;
    }
  }
  TracyCZoneEnd(ctx);
}

void translate(Transform *t, float3 p) {
  assert(t);
  t->position += p;
}
void scale(Transform *t, float3 s) {
  assert(t);
  t->scale += s;
}
void rotate(Transform *t, float3 r) {
  assert(t);
  t->rotation += r;
}

void transform_to_matrix(float4x4 *m, const Transform *t) {
  TracyCZoneN(ctx, "transform_to_matrix", true);
  TracyCZoneColor(ctx, TracyCategoryColorMath);
  assert(m);
  assert(t);

  // Position matrix
  float4x4 p = {
      (float4){1, 0, 0, t->position[0]},
      (float4){0, 1, 0, t->position[1]},
      (float4){0, 0, 1, t->position[2]},
      (float4){0, 0, 0, 1},
  };

  // Rotation matrix from euler angles
  float4x4 r = {0};
  {
    float x_angle = t->rotation[0];
    float y_angle = t->rotation[1];
    float z_angle = t->rotation[2];

    float4x4 rx = {
        (float4){1, 0, 0, 0},
        (float4){0, cosf(x_angle), -sinf(x_angle), 0},
        (float4){0, sinf(x_angle), cosf(x_angle), 0},
        (float4){0, 0, 0, 1},
    };
    float4x4 ry = {
        (float4){cosf(y_angle), 0, sinf(y_angle), 0},
        (float4){0, 1, 0, 0},
        (float4){-sinf(y_angle), 0, cosf(y_angle), 0},
        (float4){0, 0, 0, 1},

    };
    float4x4 rz = {
        (float4){cosf(z_angle), -sinf(z_angle), 0, 0},
        (float4){sinf(z_angle), cosf(z_angle), 0, 0},
        (float4){0, 0, 1, 0},
        (float4){0, 0, 0, 1},
    };
    float4x4 temp = {0};
    mulmf44(&rx, &ry, &temp);
    mulmf44(&temp, &rz, &r);
  }

  // Scale matrix
  float4x4 s = {
      (float4){t->scale[0], 0, 0, 0},
      (float4){0, t->scale[1], 0, 0},
      (float4){0, 0, t->scale[2], 0},
      (float4){0, 0, 0, 1},
  };

  // Transformation matrix = r * p * s;
  float4x4 temp = {0};
  mulmf44(&p, &r, &temp);
  mulmf44(&s, &temp, m);

  TracyCZoneEnd(ctx);
}

void look_forward(float4x4 *m, float3 pos, float3 forward, float3 up) {
  TracyCZoneN(ctx, "look_forward", true);
  TracyCZoneColor(ctx, TracyCategoryColorMath);
  assert(m);

  forward = normf3(forward);
  float3 right = crossf3(normf3(up), forward);
  up = crossf3(forward, right);

  *m = (float4x4){
      (float4){right[0], right[1], right[2], -dotf3(right, pos)},
      (float4){up[0], up[1], up[2], -dotf3(up, pos)},
      (float4){forward[0], forward[1], forward[2], -dotf3(forward, pos)},
      (float4){0, 0, 0, 1},
  };
  TracyCZoneEnd(ctx);
}

// Left-Handed
void look_at(float4x4 *m, float3 pos, float3 target, float3 up) {
  assert(m);

  float3 forward = pos - target;
  look_forward(m, pos, forward, up);
}

// Left Handed
void perspective(float4x4 *m, float fovy, float aspect, float zn, float zf) {
  assert(m);
  float focal_length = 1.0f / tanf(fovy * 0.5f);

  float m00 = focal_length / aspect;
  float m11 = -focal_length;
  float m22 = zn / (zf - zn);
  float m23 = zf * m22;

  *m = (float4x4){
      (float4){m00, 0, 0, 0},
      (float4){0, m11, 0, 0},
      (float4){0, 0, m22, m23},
      (float4){0, 0, -1, 0},
  };
}

#ifdef __clang__
#pragma clang diagnostic pop
#endif
