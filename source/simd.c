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

#ifdef __clang__
#define unroll_loop_3 _Pragma("clang loop unroll_count(3)")
#define unroll_loop_4 _Pragma("clang loop unroll_count(4)")
#endif

float3 atof3(const float f[3]) { return (float3){f[0], f[1], f[2]}; }
float4 atof4(const float f[4]) { return (float4){f[0], f[1], f[2], f[3]}; }

float2 f2(float x, float y) { return (float2){x, y}; }
float3 f3(float x, float y, float z) { return (float3){x, y, z}; }
float4 f4(float x, float y, float z, float w) { return (float4){x, y, z, w}; }

float3 f4tof3(float4 f) { return (float3){f.x, f.y, f.z}; }
float4 f3tof4(float3 f, float w) { return (float4){f.x, f.y, f.z, w}; }
float2 f3tof2(float3 f) { return (float2){f.x, f.y}; }

float3x4 m44tom34(float4x4 m) {
  return (float3x4){
      .col0 = m.col0,
      .col1 = m.col1,
      .col2 = m.col2,
  };
}
float3x3 m44tom33(float4x4 m) {
  return (float3x3){
      .col0 = f4tof3(m.col0),
      .col1 = f4tof3(m.col1),
      .col2 = f4tof3(m.col2),
  };
}

float4x4 m33tom44(float3x3 m) {
  return (float4x4){
      .col0 = f3tof4(m.col0, 0.0f),
      .col1 = f3tof4(m.col1, 0.0f),
      .col2 = f3tof4(m.col2, 0.0f),
      .col3 = {0.0f, 0.0f, 0.0f, 1.0f},
  };
}

float dotf2(float2 x, float2 y) { return (x.x * y.x) + (x.y * y.y); }

float dotf3(float3 x, float3 y) {
  return (x.x * y.x) + (x.y * y.y) + (x.z * y.z);
}

float dotf4(float4 x, float4 y) {
  return (x.x * y.x) + (x.y * y.y) + (x.z * y.z) + (x.w * y.w);
}

float3 crossf3(float3 x, float3 y) {
  return (float3){
      (x.y * y.z) - (x.z * y.y),
      (x.z * y.x) - (x.x * y.z),
      (x.x * y.y) - (x.y * y.x),
  };
}

float magf2(float2 v) { return sqrtf((v.x * v.x) + (v.y * v.y)); }

float magf3(float3 v) { return sqrtf((v.x * v.x) + (v.y * v.y) + (v.z * v.z)); }

float magf4(float4 v) {
  return sqrtf((v.x * v.x) + (v.y * v.y) + (v.z * v.z) + (v.w * v.w));
}

float magsqf3(float3 v) { return (v.x * v.x) + (v.y * v.y) + (v.z * v.z); }

float magsqf4(float4 v) {
  return (v.x * v.x) + (v.y * v.y) + (v.z * v.z) + (v.w * v.w);
}

float norm_angle(float a) {
  a = SDL_fmod(a, TAU);
  if (a < 0.0f) {
    a += TAU;
  }
  return a;
}

float2 normf2(float2 v) {
  float inv_sum = 1 / magf2(v);
  return (float2){
      v.x * inv_sum,
      v.y * inv_sum,
  };
}

float3 normf3(float3 v) {
  float inv_sum = 1 / magf3(v);
  return (float3){
      v.x * inv_sum,
      v.y * inv_sum,
      v.z * inv_sum,
  };
}

float4 normf4(float4 v) {
  float inv_sum = 1 / magf4(v);
  return (float4){v.x * inv_sum, v.y * inv_sum, v.z * inv_sum, v.w * inv_sum};
}

Quaternion normq(Quaternion q) { return (Quaternion)normf4((float4)q); }

float lenf3(float3 v) { return sqrtf(dotf3(v, v)); }

float3x3 mf33_identity(void) {
  return (float3x3){
      (float3){1, 0, 0},
      (float3){0, 1, 0},
      (float3){0, 0, 1},
  };
}

float3x4 mf34_identity(void) {
  return (float3x4){
      (float4){1, 0, 0, 0},
      (float4){0, 1, 0, 0},
      (float4){0, 0, 1, 0},
  };
}

float4x4 mf44_identity(void) {
  return (float4x4){
      (float4){1, 0, 0, 0},
      (float4){0, 1, 0, 0},
      (float4){0, 0, 1, 0},
      (float4){0, 0, 0, 1},
  };
}

float4 mulf44(float4x4 m, float4 v) {
  float4 out = {0};

  unroll_loop_4 for (uint32_t i = 0; i < 4; ++i) {
    float sum = 0.0f;
    unroll_loop_4 for (uint32_t ii = 0; ii < 4; ++ii) {
      sum += m.cols[ii][i] * v[ii];
    }
    out[i] = sum;
  }

  return out;
}

float3 mulf33(float3x3 m, float3 v) {
  float3 out = {0};

  unroll_loop_3 for (uint32_t i = 0; i < 3; ++i) {
    float sum = 0.0f;
    unroll_loop_3 for (uint32_t ii = 0; ii < 3; ++ii) {
      sum += m.cols[ii][i] * v[ii];
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
      sum += m.cols[i][ii] * v[ii];
    }
    out[i] = sum;
  }

  return out;
}

float4x4 mulmf44(float4x4 x, float4x4 y) {
  float4x4 o = {.col0 = {0}};
  unroll_loop_4 for (uint32_t i = 0; i < 4; ++i) {
    unroll_loop_4 for (uint32_t ii = 0; ii < 4; ++ii) {
      float s = 0.0f;
      unroll_loop_4 for (uint32_t iii = 0; iii < 4; ++iii) {
        s += x.cols[iii][ii] * y.cols[i][iii];
      }
      o.cols[i][ii] = s;
    }
  }
  return o;
}

float4x4 inv_mf44(float4x4 m) {
  TracyCZoneN(ctx, "inv_mf44", true);
  TracyCZoneColor(ctx, TracyCategoryColorMath);

  float coef00 = m.col2.z * m.col3.w - m.col3.z * m.col2.w;
  float coef02 = m.col1.z * m.col3.w - m.col3.z * m.col1.w;
  float coef03 = m.col1.z * m.col2.w - m.col2.z * m.col1.w;
  float coef04 = m.col2.y * m.col3.w - m.col3.y * m.col2.w;
  float coef06 = m.col1.y * m.col3.w - m.col3.y * m.col1.w;
  float coef07 = m.col1.y * m.col2.w - m.col2.y * m.col1.w;
  float coef08 = m.col2.y * m.col3.z - m.col3.y * m.col2.z;
  float coef10 = m.col1.y * m.col3.z - m.col3.y * m.col1.z;
  float coef11 = m.col1.y * m.col2.z - m.col2.y * m.col1.z;
  float coef12 = m.col2.x * m.col3.w - m.col3.x * m.col2.w;
  float coef14 = m.col1.x * m.col3.w - m.col3.x * m.col1.w;
  float coef15 = m.col1.x * m.col2.w - m.col2.x * m.col1.w;
  float coef16 = m.col2.x * m.col3.z - m.col3.x * m.col2.z;
  float coef18 = m.col1.x * m.col3.z - m.col3.x * m.col1.z;
  float coef19 = m.col1.x * m.col2.z - m.col2.x * m.col1.z;
  float coef20 = m.col2.x * m.col3.y - m.col3.x * m.col2.y;
  float coef22 = m.col1.x * m.col3.y - m.col3.x * m.col1.y;
  float coef23 = m.col1.x * m.col2.y - m.col2.x * m.col1.y;

  float4 fac0 = {coef00, coef00, coef02, coef03};
  float4 fac1 = {coef04, coef04, coef06, coef07};
  float4 fac2 = {coef08, coef08, coef10, coef11};
  float4 fac3 = {coef12, coef12, coef14, coef15};
  float4 fac4 = {coef16, coef16, coef18, coef19};
  float4 fac5 = {coef20, coef20, coef22, coef23};

  float4 vec0 = {m.col1.x, m.col0.x, m.col0.x, m.col0.x};
  float4 vec1 = {m.col1.y, m.col0.y, m.col0.y, m.col0.y};
  float4 vec2 = {m.col1.z, m.col0.z, m.col0.z, m.col0.z};
  float4 vec3 = {m.col1.w, m.col0.w, m.col0.w, m.col0.w};

  float4 inv0 = vec1 * fac0 - vec2 * fac1 + vec3 * fac2;
  float4 inv1 = vec0 * fac0 - vec2 * fac3 + vec3 * fac4;
  float4 inv2 = vec0 * fac1 - vec1 * fac3 + vec3 * fac5;
  float4 inv3 = vec0 * fac2 - vec1 * fac4 + vec2 * fac5;

  float4 sign_a = {+1, -1, +1, -1};
  float4 sign_b = {-1, +1, -1, +1};
  float4x4 inv = {inv0 * sign_a, inv1 * sign_b, inv2 * sign_a, inv3 * sign_b};

  float4 col0 = {inv.col0.x, inv.col1.x, inv.col2.x, inv.col3.x};

  float4 dot0 = m.col0 * col0;
  float dot1 = (dot0.x + dot0.y) + (dot0.z + dot0.w);

  float OneOverDeterminant = 1.0f / dot1;

  float4x4 out = {
      inv.col0 * OneOverDeterminant,
      inv.col1 * OneOverDeterminant,
      inv.col2 * OneOverDeterminant,
      inv.col3 * OneOverDeterminant,
  };

  TracyCZoneEnd(ctx);

  return out;
}

float4x4 transpose_mf44(float4x4 m) {
  return (float4x4){
      .col0 = (float4){m.cols[0].x, m.cols[1].x, m.cols[2].x, m.cols[3].x},
      .col1 = (float4){m.cols[0].y, m.cols[1].y, m.cols[2].y, m.cols[3].y},
      .col2 = (float4){m.cols[0].z, m.cols[1].z, m.cols[2].z, m.cols[3].z},
      .col3 = (float4){m.cols[0].w, m.cols[1].w, m.cols[2].w, m.cols[3].w},
  };
}

float3x3 mf33_from_axes(float3 forward, float3 right, float3 up) {
  return (float3x3){
      .col0 = {forward.x, forward.y, forward.z},
      .col1 = {right.x, right.y, right.z},
      .col2 = {up.x, up.y, up.z},
  };
}

Quaternion mf33_to_quat(float3x3 mat) {
  float four_x_squared_minus_1 = mat.cols[0].x - mat.cols[1].y - mat.cols[2].z;
  float four_y_squared_minus_1 = mat.cols[1].y - mat.cols[0].x - mat.cols[2].z;
  float four_z_squared_minus_1 = mat.cols[2].z - mat.cols[0].x - mat.cols[1].y;
  float four_w_squared_minus_1 = mat.cols[0].x + mat.cols[1].y + mat.cols[2].z;

  int32_t biggest_index = 0;
  float four_biggest_squared_minus_1 = four_w_squared_minus_1;
  if (four_x_squared_minus_1 > four_biggest_squared_minus_1) {
    four_biggest_squared_minus_1 = four_x_squared_minus_1;
    biggest_index = 1;
  }
  if (four_y_squared_minus_1 > four_biggest_squared_minus_1) {
    four_biggest_squared_minus_1 = four_y_squared_minus_1;
    biggest_index = 2;
  }
  if (four_z_squared_minus_1 > four_biggest_squared_minus_1) {
    four_biggest_squared_minus_1 = four_z_squared_minus_1;
    biggest_index = 3;
  }

  float biggest_val = SDL_sqrtf(four_biggest_squared_minus_1 + 1.0f) * 0.5f;
  float mult = 0.25f / biggest_val;

  switch (biggest_index) {
  case 0:
    return (Quaternion){
        (mat.cols[2].y - mat.cols[1].z) * mult,
        (mat.cols[0].z - mat.cols[2].x) * mult,
        (mat.cols[1].x - mat.cols[0].y) * mult,
        biggest_val,
    };
  case 1:
    return (Quaternion){
        biggest_val,
        (mat.cols[2].x + mat.cols[1].y) * mult,
        (mat.cols[0].z + mat.cols[2].x) * mult,
        (mat.cols[1].y - mat.cols[0].z) * mult,
    };
  case 2:
    return (Quaternion){
        (mat.cols[1].x + mat.cols[0].y) * mult,
        biggest_val,
        (mat.cols[2].y + mat.cols[1].z) * mult,
        (mat.cols[0].z - mat.cols[2].x) * mult,
    };
  case 3:
    return (Quaternion){
        (mat.cols[0].z + mat.cols[2].x) * mult,
        (mat.cols[2].y + mat.cols[1].z) * mult,
        biggest_val,
        (mat.cols[1].x - mat.cols[0].y) * mult,
    };
  default:
    SDL_assert(false);
    return (Quaternion){0, 0, 0, 1};
  }
}

Quaternion quat_from_axes(float3 forward, float3 right, float3 up) {
  return mf33_to_quat(mf33_from_axes(forward, right, up));
}

Quaternion angle_axis_to_quat(float4 angle_axis) {
  float s = SDL_sinf(angle_axis.w * 0.5f);
  return normq((Quaternion){
      angle_axis.x * s,
      angle_axis.y * s,
      angle_axis.z * s,
      SDL_cosf(angle_axis.w * 0.5f),
  });
}

float3x3 quat_to_mf33(Quaternion q) {
  float qxx = q.x * q.x;
  float qyy = q.y * q.y;
  float qzz = q.z * q.z;
  float qxz = q.x * q.z;
  float qxy = q.x * q.y;
  float qyz = q.y * q.z;
  float qwx = q.w * q.x;
  float qwy = q.w * q.y;
  float qwz = q.w * q.z;

  float m00 = 1.0f - 2.0f * (qyy + qzz);
  float m01 = 2.0f * (qxy + qwz);
  float m02 = 2.0f * (qxz - qwy);

  float m10 = 2.0f * (qxy - qwz);
  float m11 = 1.0f - 2.0f * (qxx + qzz);
  float m12 = 2.0f * (qyz + qwx);

  float m20 = 2.0f * (qxz + qwy);
  float m21 = 2.0f * (qyz - qwx);
  float m22 = 1.0f - 2.0f * (qxx + qyy);

  return (float3x3){
      .col0 = {m00, m01, m02},
      .col1 = {m10, m11, m12},
      .col2 = {m20, m21, m22},
  };
}

float4x4 quat_to_trans(Quaternion q) { return m33tom44(quat_to_mf33(q)); }

Quaternion mulq(Quaternion q, Quaternion p) {
  return (Quaternion){
      (p.w * q.x) + (p.x * q.w) + (p.y * q.z) - (p.z * q.y),
      (p.w * q.y) + (p.y * q.w) + (p.z * q.x) - (p.x * q.z),
      (p.w * q.z) + (p.z * q.w) + (p.x * q.y) - (p.y * q.x),
      (p.w * q.w) - (p.x * q.x) - (p.y * q.y) - (p.z * q.z),
  };
}

// https://gamedev.stackexchange.com/questions/28395/rotating-vector3-by-a-quaternion
float3 qrotf3(Quaternion q, float3 v) {
  float3 u = f3(q.x, q.y, q.z);
  float3 uv = crossf3(u, v);
  float3 uuv = crossf3(u, uv);

  return v + ((uv * q.w) + uuv) * 2.0f;
}

bool tb_f4eq(float4 x, float4 y) {
  return x.x == y.x && x.y == y.y && x.z == y.z && x.w == y.w;
}
bool tb_f3eq(float3 x, float3 y) {
  return x.x == y.x && x.y == y.y && x.z == y.z;
}

bool tb_transeq(const Transform *x, const Transform *y) {
  return tb_f3eq(x->position, y->position) &&
         tb_f4eq(x->rotation, y->rotation) && tb_f3eq(x->scale, y->scale);
}

AABB aabb_init(void) {
  return (AABB){
      .min = {FLT_MAX, FLT_MAX, FLT_MAX},
      .max = {-FLT_MAX, -FLT_MAX, -FLT_MAX},
  };
}

void aabb_add_point(AABB *aabb, float3 point) {
  aabb->min.x = SDL_min(aabb->min.x, point.x);
  aabb->min.y = SDL_min(aabb->min.y, point.y);
  aabb->min.z = SDL_min(aabb->min.z, point.z);
  aabb->max.x = SDL_max(aabb->max.x, point.x);
  aabb->max.y = SDL_max(aabb->max.y, point.y);
  aabb->max.z = SDL_max(aabb->max.z, point.z);
}

float aabb_get_width(AABB aabb) {
  return SDL_fabsf(aabb.max[TB_WIDTH_IDX] - aabb.min[TB_WIDTH_IDX]);
}

float aabb_get_height(AABB aabb) {
  return SDL_fabsf(aabb.max[TB_HEIGHT_IDX] - aabb.min[TB_HEIGHT_IDX]);
}

float aabb_get_depth(AABB aabb) {
  return SDL_fabsf(aabb.max[TB_DEPTH_IDX] - aabb.min[TB_DEPTH_IDX]);
}

AABB aabb_rotate(Quaternion q, AABB aabb) {
  return (AABB){
      .min = qrotf3(q, aabb.min),
      .max = qrotf3(q, aabb.max),
  };
}

AABB aabb_transform(float4x4 m, AABB aabb) {
  return (AABB){
      .min = f4tof3(mulf44(m, f3tof4(aabb.min, 1.0f))),
      .max = f4tof3(mulf44(m, f3tof4(aabb.max, 1.0f))),
  };
}

void translate(Transform *t, float3 p) {
  SDL_assert(t);
  t->position += p;
}
void scale(Transform *t, float3 s) {
  SDL_assert(t);
  t->scale *= s;
}
void rotate(Transform *t, Quaternion r) {
  SDL_assert(t);
  t->rotation = mulq(t->rotation, r);
}

float3 transform_get_forward(const Transform *t) {
  return normf3(qrotf3(t->rotation, TB_FORWARD));
}

float3 transform_get_right(const Transform *t) {
  return normf3(qrotf3(t->rotation, TB_RIGHT));
}

float3 transform_get_up(const Transform *t) {
  return normf3(qrotf3(t->rotation, TB_UP));
}

float4x4 transform_to_matrix(const Transform *t) {
  TracyCZoneN(ctx, "transform_to_matrix", true);
  TracyCZoneColor(ctx, TracyCategoryColorMath);
  SDL_assert(t);

  // Position matrix
  float4x4 p = {
      (float4){1, 0, 0, 0},
      (float4){0, 1, 0, 0},
      (float4){0, 0, 1, 0},
      (float4){t->position.x, t->position.y, t->position.z, 1},
  };

  // Rotation matrix from quaternion
  float4x4 r = quat_to_trans(t->rotation);

  // Scale matrix
  float4x4 s = {
      (float4){t->scale.x, 0, 0, 0},
      (float4){0, t->scale.y, 0, 0},
      (float4){0, 0, t->scale.z, 0},
      (float4){0, 0, 0, 1},
  };

  // Transformation matrix = p * r * s
  float4x4 m = mulmf44(mulmf44(p, r), s);
  TracyCZoneEnd(ctx);
  return m;
}

Transform tb_transform_from_node(const cgltf_node *node) {
  Transform transform = {.position = {0}};

  transform.position = (float3){node->translation[0], node->translation[1],
                                node->translation[2]};

  transform.rotation = normq((Quaternion){
      node->rotation[0],
      node->rotation[1],
      node->rotation[2],
      node->rotation[3],
  });

  transform.scale = (float3){node->scale[0], node->scale[1], node->scale[2]};

  return transform;
}

// Right Handed
float4x4 look_forward(float3 pos, float3 forward, float3 up) {
  forward = normf3(forward);
  float3 right = normf3(crossf3(forward, normf3(up)));
  up = crossf3(right, forward);

  return (float4x4){
      (float4){right.x, up.x, -forward.x, 0},
      (float4){right.y, up.y, -forward.y, 0},
      (float4){right.z, up.z, -forward.z, 0},
      (float4){-dotf3(right, pos), -dotf3(up, pos), dotf3(forward, pos), 1},
  };
}

float4x4 look_at(float3 pos, float3 target, float3 up) {
  float3 forward = normf3(target - pos);
  return look_forward(pos, forward, up);
}

// Right Handed
float4x4 perspective(float fovy, float aspect, float zn, float zf) {
  float focal_length = 1.0f / tanf(fovy * 0.5f);
  float m00 = focal_length / aspect;
  float m11 = focal_length;

#ifdef TB_USE_INVERSE_DEPTH
  float m22 = zn / (zn - zf);
  float m32 = -(zn * zf) / (zn - zf);

  return (float4x4){
      (float4){m00, 0, 0, 0},
      (float4){0, m11, 0, 0},
      (float4){0, 0, m22, 1},
      (float4){0, 0, m32, 0},
  };
#else
  float m22 = zf / (zn - zf);
  float m32 = -(zf * zn) / (zf - zn);

  return (float4x4){
      (float4){m00, 0, 0, 0},
      (float4){0, m11, 0, 0},
      (float4){0, 0, m22, -1},
      (float4){0, 0, m32, 0},
  };
#endif
}

// Right Handed
float4x4 orthographic(float r, float l, float t, float b, float zn, float zf) {
  return (float4x4){
      (float4){2.0f / (r - l), 0, 0, 0},
      (float4){0, 2.0f / (t - b), 0, 0},
      (float4){0, 0, -1 / (zf - zn), 0},
      (float4){-(r + l) / (r - l), -(t + b) / (t - b), -zn / (zf - zn), 1},
  };
}

// See //
// https://www.braynzarsoft.net/viewtutorial/q16390-34-aabb-cpu-side-frustum-culling
Frustum frustum_from_view_proj(const float4x4 *vp) {
  Frustum f = {
      .planes[LeftPlane] =
          (float4){
              vp->cols[0].w + vp->cols[0].x,
              vp->cols[1].w + vp->cols[1].x,
              vp->cols[2].w + vp->cols[2].x,
              vp->cols[3].w + vp->cols[3].x,
          },
      .planes[RightPlane] =
          (float4){
              vp->cols[0].w - vp->cols[0].x,
              vp->cols[1].w - vp->cols[1].x,
              vp->cols[2].w - vp->cols[2].x,
              vp->cols[3].w - vp->cols[3].x,
          },
      .planes[TopPlane] =
          (float4){
              vp->cols[0].w - vp->cols[0].y,
              vp->cols[1].w - vp->cols[1].y,
              vp->cols[2].w - vp->cols[2].y,
              vp->cols[3].w - vp->cols[3].y,
          },
      .planes[BottomPlane] =
          (float4){
              vp->cols[0].w + vp->cols[0].y,
              vp->cols[1].w + vp->cols[1].y,
              vp->cols[2].w + vp->cols[2].y,
              vp->cols[3].w + vp->cols[3].y,
          },
      .planes[NearPlane] =
          (float4){
              vp->cols[0].z,
              vp->cols[1].z,
              vp->cols[2].z,
              vp->cols[3].z,
          },
      .planes[FarPlane] =
          (float4){
              vp->cols[0].w - vp->cols[0].z,
              vp->cols[1].w - vp->cols[1].z,
              vp->cols[2].w - vp->cols[2].z,
              vp->cols[3].w - vp->cols[3].z,
          },
  };
  // Must normalize planes
  for (uint32_t i = 0; i < FrustumPlaneCount; ++i) {
    Plane *p = &f.planes[i];
    const float norm_length = lenf3((float3){p->xyzw.x, p->xyzw.y, p->xyzw.z});
    p->xyzw.x /= norm_length;
    p->xyzw.y /= norm_length;
    p->xyzw.z /= norm_length;
    p->xyzw.w /= norm_length;
  }
  return f;
}

bool frustum_test_aabb(const Frustum *frust, const AABB *aabb) {
  // See
  // https://www.braynzarsoft.net/viewtutorial/q16390-34-aabb-cpu-side-frustum-culling
  for (uint32_t i = 0; i < FrustumPlaneCount; ++i) {
    const Plane *plane = &frust->planes[i];
    const float3 normal = (float3){plane->xyzw.x, plane->xyzw.y, plane->xyzw.z};
    const float plane_const = plane->xyzw.w;

    float3 axis = {
        normal.x < 0.0f ? aabb->min.x : aabb->max.x,
        normal.y < 0.0f ? aabb->min.y : aabb->max.y,
        normal.z < 0.0f ? aabb->min.z : aabb->max.z,
    };

    if (dotf3(normal, axis) + plane_const < 0.0f) {
      // The AABB was outside one of the planes and failed this test
      return false;
    }
  }
  // The AABB was inside each plane
  return true;
}

float deg_to_rad(float d) { return d * (M_PI / 180.0f); }
float rad_to_deg(float r) { return r * (180 / M_PI); }

// https://en.wikipedia.org/wiki/Linear_interpolation
float lerpf(float f0, float f1, float a) { return (1 - a) * f0 + a * f1; }
float3 lerpf3(float3 v0, float3 v1, float a) {
  return ((1 - a) * v0) + (a * v1);
}

// https://www.euclideanspace.com/maths/algebra/realNormedAlgebra/quaternions/slerp/index.htm
Quaternion slerp(Quaternion q1, Quaternion q2, float a) {
  // Calc angle between quaternions
  float cos_half_theta = q1.w * q2.w + q1.x * q2.x + q1.y * q2.y + q1.z * q2.z;
  // If q1=q2 or q1=-q2 then alpha = 0 and we can return q1
  if (SDL_fabsf(cos_half_theta) >= 1.0f) {
    return q1;
  }

  float half_theta = SDL_acosf(cos_half_theta);
  float sin_half_theta = SDL_sqrtf(1.0f - cos_half_theta * cos_half_theta);
  // if alpha = 180 degrees then result is not fully defined
  // we could rotate around any axis normal to qa or qb
  if (SDL_fabsf(sin_half_theta) < 0.001f) { // fabs is floating point absolute
    return (Quaternion){
        q1.x * 0.5f + q2.x * 0.5f,
        q1.y * 0.5f + q2.y * 0.5f,
        q1.z * 0.5f + q2.z * 0.5f,
        q1.w * 0.5f + q2.w * 0.5f,
    };
  }

  float ratio_a = SDL_sinf((1.0f - a) * half_theta) / sin_half_theta;
  float ratio_b = SDL_sinf(a * half_theta) / sin_half_theta;

  return (Quaternion){
      q1.x * ratio_a + q2.x * ratio_b,
      q1.y * ratio_a + q2.y * ratio_b,
      q1.z * ratio_a + q2.z * ratio_b,
      q1.w * ratio_a + q2.w * ratio_b,
  };
}

float clampf(float v, float min, float max) {
  if (v < min) {
    return min;
  }
  if (v > max) {
    return max;
  }
  return v;
}

float3 clampf3(float3 v, float3 min, float3 max) {
  return (float3){
      clampf(v.x, min.x, max.x),
      clampf(v.y, min.y, max.y),
      clampf(v.z, min.z, max.z),
  };
}

float tb_randf(float min, float max) {
  float r = (float)rand() / (float)RAND_MAX;
  return min + r * (max - min);
}

#ifdef __clang__
#pragma clang diagnostic pop
#endif
