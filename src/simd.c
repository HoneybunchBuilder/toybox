#include "simd.h"

#include <SDL2/SDL_stdinc.h>

#define _USE_MATH_DEFINES
#include <SDL_assert.h>
#include <math.h>

#include <stdbool.h>

#include "pi.h"
#include "profiling.h"
#include "tbgltf.h"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-braces"
#endif

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-braces"
#endif

#ifdef __clang__
#define unroll_loop_3 _Pragma("clang loop unroll_count(3)")
#define unroll_loop_4 _Pragma("clang loop unroll_count(4)")
#endif

#if defined(__GNUC__) && !defined(__clang__)
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

float dotf2(float2 x, float2 y) { return (x[0] * y[0]) + (x[1] * y[1]); }

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

float magf2(float2 v) { return sqrtf((v[0] * v[0]) + (v[1] * v[1])); }

float magf3(float3 v) {
  return sqrtf((v[0] * v[0]) + (v[1] * v[1]) + (v[2] * v[2]));
}

float magf4(float4 v) {
  return sqrtf((v[0] * v[0]) + (v[1] * v[1]) + (v[2] * v[2]) + (v[3] * v[3]));
}

float magsqf3(float3 v) {
  return (v[0] * v[0]) + (v[1] * v[1]) + (v[2] * v[2]);
}

float magsqf4(float4 v) {
  return (v[0] * v[0]) + (v[1] * v[1]) + (v[2] * v[2]) + (v[3] * v[3]);
}

float2 normf2(float2 v) {
  float inv_sum = 1 / magf2(v);
  return (float2){
      v[0] * inv_sum,
      v[1] * inv_sum,
  };
}

float3 normf3(float3 v) {
  float inv_sum = 1 / magf3(v);
  return (float3){
      v[0] * inv_sum,
      v[1] * inv_sum,
      v[2] * inv_sum,
  };
}

float4 normf4(float3 v) {
  float inv_sum = 1 / magf4(v);
  return (float3){
      v[0] * inv_sum,
      v[1] * inv_sum,
      v[2] * inv_sum,
      v[3] * inv_sum,
  };
}

float lenf3(float3 v) { return sqrtf(dotf3(v, v)); }

void mf33_identity(float3x3 *m) {
  SDL_assert(m);
  *m = (float3x3){
      (float3){1, 0, 0},
      (float3){0, 1, 0},
      (float3){0, 0, 1},
  };
}

void mf34_identity(float3x4 *m) {
  SDL_assert(m);
  *m = (float3x4){
      (float4){1, 0, 0, 0},
      (float4){0, 1, 0, 0},
      (float4){0, 0, 1, 0},
  };
}

void mf44_identity(float4x4 *m) {
  SDL_assert(m);
  *m = (float4x4){
      (float4){1, 0, 0, 0},
      (float4){0, 1, 0, 0},
      (float4){0, 0, 1, 0},
      (float4){0, 0, 0, 1},
  };
}

void mulf33(float3x3 *m, float3 v) {
  SDL_assert(m);
  unroll_loop_3 for (uint32_t i = 0; i < 3; ++i) {
    float s = v[i];
    m->row0[i] *= s;
    m->row1[i] *= s;
    m->row2[i] *= s;
  }
}

void mulf34(float3x4 *m, float4 v) {
  SDL_assert(m);
  unroll_loop_4 for (uint32_t i = 0; i < 4; ++i) {
    float s = v[i];
    m->row0[i] *= s;
    m->row1[i] *= s;
    m->row2[i] *= s;
  }
}

float4 mulf44(float4x4 m, float4 v) {
  float4 out = {0};

  unroll_loop_4 for (uint32_t i = 0; i < 4; ++i) {
    float sum = 0.0f;
    unroll_loop_4 for (uint32_t ii = 0; ii < 4; ++ii) {
      sum += m.rows[ii][i] * v[ii];
    }
    out[i] = sum;
  }

  return out;
}

float4 mul4f44f(float4 v, float4x4 m) {
  float4 out = {0};

  unroll_loop_4 for (uint32_t i = 0; i < 4; ++i) {
    float sum = 0.0f;
    unroll_loop_4 for (uint32_t ii = 0; ii < 4; ++ii) {
      sum += m.rows[i][ii] * v[ii];
    }
    out[i] = sum;
  }

  return out;
}

void mulmf34(const float3x4 *x, const float3x4 *y, float3x4 *o) {
  SDL_assert(x);
  SDL_assert(y);
  SDL_assert(o);
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
  SDL_assert(x);
  SDL_assert(y);
  SDL_assert(o);
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

float4x4 inv_mf44(float4x4 m) {
  TracyCZoneN(ctx, "mulmf44", true);
  TracyCZoneColor(ctx, TracyCategoryColorMath);

  float coef00 = m.row2[2] * m.row3[3] - m.row3[2] * m.row2[3];
  float coef02 = m.row1[2] * m.row3[3] - m.row3[2] * m.row1[3];
  float coef03 = m.row1[2] * m.row2[3] - m.row2[2] * m.row1[3];
  float coef04 = m.row2[1] * m.row3[3] - m.row3[1] * m.row2[3];
  float coef06 = m.row1[1] * m.row3[3] - m.row3[1] * m.row1[3];
  float coef07 = m.row1[1] * m.row2[3] - m.row2[1] * m.row1[3];
  float coef08 = m.row2[1] * m.row3[2] - m.row3[1] * m.row2[2];
  float coef10 = m.row1[1] * m.row3[2] - m.row3[1] * m.row1[2];
  float coef11 = m.row1[1] * m.row2[2] - m.row2[1] * m.row1[2];
  float coef12 = m.row2[0] * m.row3[3] - m.row3[0] * m.row2[3];
  float coef14 = m.row1[0] * m.row3[3] - m.row3[0] * m.row1[3];
  float coef15 = m.row1[0] * m.row2[3] - m.row2[0] * m.row1[3];
  float coef16 = m.row2[0] * m.row3[2] - m.row3[0] * m.row2[2];
  float coef18 = m.row1[0] * m.row3[2] - m.row3[0] * m.row1[2];
  float coef19 = m.row1[0] * m.row2[2] - m.row2[0] * m.row1[2];
  float coef20 = m.row2[0] * m.row3[1] - m.row3[0] * m.row2[1];
  float coef22 = m.row1[0] * m.row3[1] - m.row3[0] * m.row1[1];
  float coef23 = m.row1[0] * m.row2[1] - m.row2[0] * m.row1[1];

  float4 fac0 = {coef00, coef00, coef02, coef03};
  float4 fac1 = {coef04, coef04, coef06, coef07};
  float4 fac2 = {coef08, coef08, coef10, coef11};
  float4 fac3 = {coef12, coef12, coef14, coef15};
  float4 fac4 = {coef16, coef16, coef18, coef19};
  float4 fac5 = {coef20, coef20, coef22, coef23};

  float4 vec0 = {m.row1[0], m.row0[0], m.row0[0], m.row0[0]};
  float4 vec1 = {m.row1[1], m.row0[1], m.row0[1], m.row0[1]};
  float4 vec2 = {m.row1[2], m.row0[2], m.row0[2], m.row0[2]};
  float4 vec3 = {m.row1[3], m.row0[3], m.row0[3], m.row0[3]};

  float4 inv0 = vec1 * fac0 - vec2 * fac1 + vec3 * fac2;
  float4 inv1 = vec0 * fac0 - vec2 * fac3 + vec3 * fac4;
  float4 inv2 = vec0 * fac1 - vec1 * fac3 + vec3 * fac5;
  float4 inv3 = vec0 * fac2 - vec1 * fac4 + vec2 * fac5;

  float4 sign_a = {+1, -1, +1, -1};
  float4 sign_b = {-1, +1, -1, +1};
  float4x4 inv = {inv0 * sign_a, inv1 * sign_b, inv2 * sign_a, inv3 * sign_b};

  float4 Row0 = {inv.row0[0], inv.row1[0], inv.row2[0], inv.row3[0]};

  float4 dot0 = m.row0 * Row0;
  float dot1 = (dot0[0] + dot0[1]) + (dot0[2] + dot0[3]);

  float OneOverDeterminant = 1.0f / dot1;

  float4x4 out = {
      inv.row0 * OneOverDeterminant,
      inv.row1 * OneOverDeterminant,
      inv.row2 * OneOverDeterminant,
      inv.row3 * OneOverDeterminant,
  };

  TracyCZoneEnd(ctx);

  return out;
}

EulerAngles quat_to_euler(Quaternion quat) {
  EulerAngles xyz;

  const float sinr_cosp = 2.0f * (quat[3] * quat[0] + quat[1] * quat[2]);
  const float cosr_cosp = 1.0f - 2.0f * (quat[0] * quat[0] + quat[1] * quat[1]);
  xyz[0] = -atan2f(sinr_cosp, cosr_cosp);

  const float sinp = 2.0f * (quat[3] * quat[1] - quat[2] * quat[0]);
  if (fabsf(sinp) >= 1.0f) {
    const float sign = sinp > 0 ? 1.0f : -1.0f;
    xyz[1] = (PI / 2.0f) * (sinp == 0 ? 0.0f : sign);
  } else {
    // use 90 degrees if out of range
    xyz[1] = asinf(sinp);
  }
  xyz[1] = -xyz[1]; // Why??

  const float siny_cosp = 2.0f * (quat[3] * quat[2] + quat[0] * quat[1]);
  const float cosy_cosp = 1.0f - 2.0f * (quat[1] * quat[1] + quat[2] * quat[2]);
  xyz[2] = atan2f(siny_cosp, cosy_cosp);

  return xyz;
}

Quaternion euler_to_quat(EulerAngles xyz) {
  const float x = xyz[0];
  const float y = xyz[1];
  const float z = xyz[2];

  // See
  // https://en.wikipedia.org/wiki/Conversion_between_quaternions_and_Euler_angles
  const float cy = cosf(z * 0.5);
  const float sy = sinf(z * 0.5);
  const float cp = cosf(y * 0.5);
  const float sp = sinf(y * 0.5);
  const float cr = cosf(x * 0.5);
  const float sr = sinf(x * 0.5);

  // Note: in the above conversion quat_to_euler we have to negate y axis and
  // not sure why... possibly may need to do some additional work here as this
  // method is untested.

  Quaternion q = {
      sr * cp * cy - cr * sp * sy, // X
      cr * sp * cy + sr * cp * sy, // Y
      cr * cp * sy - sr * sp * cy, // Z
      cr * cp * cy + sr * sp * sy, // W
  };
  return q;
}

float4x4 euler_to_trans(EulerAngles euler) {
  const float x_angle = euler[0];
  const float y_angle = euler[1];
  const float z_angle = euler[2];

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

  float4x4 temp = {.row0 = {0}};
  float4x4 r = {.row0 = {0}};
  mulmf44(&rx, &ry, &temp);
  mulmf44(&temp, &rz, &r);

  return r;
}

float4x4 quat_to_trans(Quaternion quat) {
  (void)quat;
  return (float4x4){.row0 = {0}};
}

AABB aabb_init(void) {
  return (AABB){
      .min = {FLT_MAX, FLT_MAX, FLT_MAX},
      .max = {-FLT_MAX, -FLT_MAX, -FLT_MAX},
  };
}

void aabb_add_point(AABB *aabb, float3 point) {
  aabb->min[0] = SDL_min(aabb->min[0], point[0]);
  aabb->min[1] = SDL_min(aabb->min[1], point[1]);
  aabb->min[2] = SDL_min(aabb->min[2], point[2]);
  aabb->max[0] = SDL_max(aabb->max[0], point[0]);
  aabb->max[1] = SDL_max(aabb->max[1], point[1]);
  aabb->max[2] = SDL_max(aabb->max[2], point[2]);
}

void translate(Transform *t, float3 p) {
  SDL_assert(t);
  t->position += p;
}
void scale(Transform *t, float3 s) {
  SDL_assert(t);
  t->scale *= s;
}
void rotate(Transform *t, float3 r) {
  SDL_assert(t);
  t->rotation += r;
}

void transform_to_matrix(float4x4 *m, const Transform *t) {
  TracyCZoneN(ctx, "transform_to_matrix", true);
  TracyCZoneColor(ctx, TracyCategoryColorMath);
  SDL_assert(m);
  SDL_assert(t);

  // Position matrix
  float4x4 p = {
      (float4){1, 0, 0, t->position[0]},
      (float4){0, 1, 0, t->position[1]},
      (float4){0, 0, 1, t->position[2]},
      (float4){0, 0, 0, 1},
  };

  // Rotation matrix from euler angles
  float4x4 r = euler_to_trans(t->rotation);

  // Scale matrix
  float4x4 s = {
      (float4){t->scale[0], 0, 0, 0},
      (float4){0, t->scale[1], 0, 0},
      (float4){0, 0, t->scale[2], 0},
      (float4){0, 0, 0, 1},
  };

  // Transformation matrix = r * p * s;
  float4x4 temp = {.row0 = {0}};
  mulmf44(&p, &r, &temp);
  mulmf44(&temp, &s, m);

  TracyCZoneEnd(ctx);
}

Transform tb_transform_from_node(const cgltf_node *node) {
  Transform transform = {.position = {0}};

  transform.position = (float3){node->translation[0], node->translation[1],
                                node->translation[2]};

  Quaternion quat = {
      node->rotation[0],
      node->rotation[1],
      node->rotation[2],
      node->rotation[3],
  };
  transform.rotation = quat_to_euler(quat);

  transform.scale = (float3){node->scale[0], node->scale[1], node->scale[2]};

  return transform;
}

void look_forward(float4x4 *m, float3 pos, float3 forward, float3 up) {
  TracyCZoneN(ctx, "look_forward", true);
  TracyCZoneColor(ctx, TracyCategoryColorMath);
  SDL_assert(m);

  forward = normf3(forward);
  float3 right = normf3(crossf3(normf3(up), forward));
  up = normf3(crossf3(forward, right));

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
  SDL_assert(m);

  float3 forward = target - pos;
  look_forward(m, pos, forward, up);
}

// Left Handed
void perspective(float4x4 *m, float fovy, float aspect, float zn, float zf) {
  SDL_assert(m);
  float focal_length = 1.0f / SDL_tanf(fovy * 0.5f);

  float m00 = focal_length / aspect;
  float m11 = focal_length;
  float m22 = zf / (zf - zn);
  float m23 = -(zn * zf) / (zf - zn);

  *m = (float4x4){
      (float4){m00, 0, 0, 0},
      (float4){0, m11, 0, 0},
      (float4){0, 0, m22, m23},
      (float4){0, 0, 1, 0},
  };
}

// Left Handed
void reverse_perspective(float4x4 *m, float fovy, float aspect, float zn,
                         float zf) {
  SDL_assert(m);
  float focal_length = 1.0f / SDL_tanf(fovy * 0.5f);

  float m00 = focal_length / aspect;
  float m11 = focal_length;
  float m22 = zn / (zf - zn);
  float m23 = zf * zn / (zf - zn);

  *m = (float4x4){
      (float4){m00, 0, 0, 0},
      (float4){0, m11, 0, 0},
      (float4){0, 0, m22, m23},
      (float4){0, 0, -1, 0},
  };
}

// Left Handed
float4x4 orthographic(float r, float l, float t, float b, float zn, float zf) {
  return (float4x4){
      (float4){2.0f / (r - l), 0, 0, 0},
      (float4){0, 2.0f / (t - b), 0, 0},
      (float4){0, 0, 1 / (zf - zn), 0},
      (float4){(l + r) / (l - r), (t + b) / (b - t), zn / (zn - zf), 1},
  };
}

// See //
// https://www.braynzarsoft.net/viewtutorial/q16390-34-aabb-cpu-side-frustum-culling
Frustum frustum_from_view_proj(const float4x4 *vp) {
  Frustum f = {
      .planes[LeftPlane] =
          (float4){
              vp->row3[0] + vp->row0[0],
              vp->row3[1] + vp->row0[1],
              vp->row3[2] + vp->row0[2],
              vp->row3[3] + vp->row0[3],
          },
      .planes[RightPlane] =
          (float4){
              vp->row3[0] - vp->row0[0],
              vp->row3[1] - vp->row0[1],
              vp->row3[2] - vp->row0[2],
              vp->row3[3] - vp->row0[3],
          },
      .planes[TopPlane] =
          (float4){
              vp->row3[0] - vp->row1[0],
              vp->row3[1] - vp->row1[1],
              vp->row3[2] - vp->row1[2],
              vp->row3[3] - vp->row1[3],
          },
      .planes[BottomPlane] =
          (float4){
              vp->row3[0] + vp->row1[0],
              vp->row3[1] + vp->row1[1],
              vp->row3[2] + vp->row1[2],
              vp->row3[3] + vp->row1[3],
          },
      .planes[NearPlane] =
          (float4){
              vp->row2[0],
              vp->row2[1],
              vp->row2[2],
              vp->row2[3],
          },
      .planes[FarPlane] =
          (float4){
              vp->row3[0] - vp->row2[0],
              vp->row3[1] - vp->row2[1],
              vp->row3[2] - vp->row2[2],
              vp->row3[3] - vp->row2[3],
          },
  };
  // Must normalize planes
  for (uint32_t i = 0; i < FrustumPlaneCount; ++i) {
    Plane *p = &f.planes[i];
    const float norm_length =
        lenf3((float3){p->xyzw[0], p->xyzw[1], p->xyzw[2]});
    p->xyzw[0] /= norm_length;
    p->xyzw[1] /= norm_length;
    p->xyzw[2] /= norm_length;
    p->xyzw[3] /= norm_length;
  }
  return f;
}

bool frustum_test_aabb(const Frustum *frust, const AABB *aabb) {
  // See
  // https://www.braynzarsoft.net/viewtutorial/q16390-34-aabb-cpu-side-frustum-culling
  for (uint32_t i = 0; i < FrustumPlaneCount; ++i) {
    const Plane *plane = &frust->planes[i];
    const float3 normal =
        (float3){plane->xyzw[0], plane->xyzw[1], plane->xyzw[2]};
    const float plane_const = plane->xyzw[3];

    float3 axis = {0};
    axis[0] = normal[0] < 0.0f ? aabb->min[0] : aabb->max[0];
    axis[1] = normal[1] < 0.0f ? aabb->min[1] : aabb->max[1];
    axis[2] = normal[2] < 0.0f ? aabb->min[2] : aabb->max[2];

    if (dotf3(normal, axis) + plane_const < 0.0f) {
      // The AABB was outside one of the planes and failed this test
      return false;
    }
  }
  // The AABB was inside each plane
  return true;
}

float tb_deg_to_rad(float d) { return d * (M_PI / 180.0f); }
float tb_rad_to_deg(float r) { return r * (180 / M_PI); }

// https://en.wikipedia.org/wiki/Linear_interpolation
float tb_lerpf(float v0, float v1, float a) { return (1 - a) * v0 + a * v1; }

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
