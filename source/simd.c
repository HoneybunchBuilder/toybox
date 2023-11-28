#include "simd.h"

#include <SDL2/SDL_stdinc.h>

#define _USE_MATH_DEFINES
#include <SDL_assert.h>
#include <math.h>

#include <stdbool.h>

#include "pi.h"
#include "profiling.h"
#include "tbgltf.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-braces"

#define unroll_loop_3 _Pragma("clang loop unroll_count(3)")
#define unroll_loop_4 _Pragma("clang loop unroll_count(4)")

float3 tb_atof3(const float f[3]) { return (float3){f[0], f[1], f[2]}; }
float4 tb_atof4(const float f[4]) { return (float4){f[0], f[1], f[2], f[3]}; }

float2 tb_f2(float x, float y) { return (float2){x, y}; }
float3 tb_f3(float x, float y, float z) { return (float3){x, y, z}; }
float4 tb_f4(float x, float y, float z, float w) {
  return (float4){x, y, z, w};
}

float3 tb_f4tof3(float4 f) { return (float3){f.x, f.y, f.z}; }
float4 tb_f3tof4(float3 f, float w) { return (float4){f.x, f.y, f.z, w}; }
float2 tb_f3tof2(float3 f) { return (float2){f.x, f.y}; }

float3x4 tb_f44tof34(float4x4 m) {
  return (float3x4){
      .col0 = m.col0,
      .col1 = m.col1,
      .col2 = m.col2,
  };
}
float3x3 tb_f44tof33(float4x4 m) {
  return (float3x3){
      .col0 = tb_f4tof3(m.col0),
      .col1 = tb_f4tof3(m.col1),
      .col2 = tb_f4tof3(m.col2),
  };
}

float4x4 tb_f33tof44(float3x3 m) {
  return (float4x4){
      .col0 = tb_f3tof4(m.col0, 0.0f),
      .col1 = tb_f3tof4(m.col1, 0.0f),
      .col2 = tb_f3tof4(m.col2, 0.0f),
      .col3 = {0.0f, 0.0f, 0.0f, 1.0f},
  };
}

float tb_dotf2(float2 x, float2 y) { return (x.x * y.x) + (x.y * y.y); }

float tb_dotf3(float3 x, float3 y) {
  return (x.x * y.x) + (x.y * y.y) + (x.z * y.z);
}

float tb_dotf4(float4 x, float4 y) {
  return (x.x * y.x) + (x.y * y.y) + (x.z * y.z) + (x.w * y.w);
}

float3 tb_crossf3(float3 x, float3 y) {
  return (float3){
      (x.y * y.z) - (x.z * y.y),
      (x.z * y.x) - (x.x * y.z),
      (x.x * y.y) - (x.y * y.x),
  };
}

float tb_magf2(float2 v) { return sqrtf(tb_magsqf2(v)); }
float tb_magf3(float3 v) { return sqrtf(tb_magsqf3(v)); }
float tb_magf4(float4 v) { return sqrtf(tb_magsqf4(v)); }

float tb_magsqf2(float2 v) { return tb_dotf2(v, v); }
float tb_magsqf3(float3 v) { return tb_dotf3(v, v); }
float tb_magsqf4(float4 v) { return tb_dotf4(v, v); }

float tb_norm_angle(float a) {
  a = SDL_fmod(a, TB_TAU);
  if (a < 0.0f) {
    a += TB_TAU;
  }
  return a;
}

float2 tb_normf2(float2 v) {
  float inv_sum = 1 / tb_magf2(v);
  return (float2){
      v.x * inv_sum,
      v.y * inv_sum,
  };
}

float3 tb_normf3(float3 v) {
  float inv_sum = 1 / tb_magf3(v);
  return (float3){
      v.x * inv_sum,
      v.y * inv_sum,
      v.z * inv_sum,
  };
}

float4 tb_normf4(float4 v) {
  float inv_sum = 1 / tb_magf4(v);
  return (float4){v.x * inv_sum, v.y * inv_sum, v.z * inv_sum, v.w * inv_sum};
}

TbQuaternion tb_normq(TbQuaternion q) {
  return (TbQuaternion)tb_normf4((float4)q);
}

TbTransform tb_trans_identity(void) {
  return (TbTransform){
      .rotation = tb_f4(0, 0, 0, 1),
      .scale = tb_f3(1, 1, 1),
  };
}

float3x3 tb_f33_identity(void) {
  return (float3x3){
      (float3){1, 0, 0},
      (float3){0, 1, 0},
      (float3){0, 0, 1},
  };
}

float3x4 tb_f34_identity(void) {
  return (float3x4){
      (float4){1, 0, 0, 0},
      (float4){0, 1, 0, 0},
      (float4){0, 0, 1, 0},
  };
}

float4x4 tb_f44_identity(void) {
  return (float4x4){
      (float4){1, 0, 0, 0},
      (float4){0, 1, 0, 0},
      (float4){0, 0, 1, 0},
      (float4){0, 0, 0, 1},
  };
}

float4 tb_mulf44f4(float4x4 m, float4 v) {
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

float3 tb_mulf33f3(float3x3 m, float3 v) {
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

float4 tb_mulf4f44(float4 v, float4x4 m) {
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

float4x4 tb_mulf44f44(float4x4 x, float4x4 y) {
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

float4x4 tb_invf44(float4x4 m) {
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

float4x4 tb_transpose_f44(float4x4 m) {
  return (float4x4){
      .col0 = (float4){m.cols[0].x, m.cols[1].x, m.cols[2].x, m.cols[3].x},
      .col1 = (float4){m.cols[0].y, m.cols[1].y, m.cols[2].y, m.cols[3].y},
      .col2 = (float4){m.cols[0].z, m.cols[1].z, m.cols[2].z, m.cols[3].z},
      .col3 = (float4){m.cols[0].w, m.cols[1].w, m.cols[2].w, m.cols[3].w},
  };
}

float3x3 tb_f33_from_axes(float3 forward, float3 right, float3 up) {
  return (float3x3){
      .col0 = {forward.x, forward.y, forward.z},
      .col1 = {right.x, right.y, right.z},
      .col2 = {up.x, up.y, up.z},
  };
}

TbQuaternion tb_f33_to_quat(float3x3 mat) {
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
    return (TbQuaternion){
        (mat.cols[2].y - mat.cols[1].z) * mult,
        (mat.cols[0].z - mat.cols[2].x) * mult,
        (mat.cols[1].x - mat.cols[0].y) * mult,
        biggest_val,
    };
  case 1:
    return (TbQuaternion){
        biggest_val,
        (mat.cols[2].x + mat.cols[1].y) * mult,
        (mat.cols[0].z + mat.cols[2].x) * mult,
        (mat.cols[1].y - mat.cols[0].z) * mult,
    };
  case 2:
    return (TbQuaternion){
        (mat.cols[1].x + mat.cols[0].y) * mult,
        biggest_val,
        (mat.cols[2].y + mat.cols[1].z) * mult,
        (mat.cols[0].z - mat.cols[2].x) * mult,
    };
  case 3:
    return (TbQuaternion){
        (mat.cols[0].z + mat.cols[2].x) * mult,
        (mat.cols[2].y + mat.cols[1].z) * mult,
        biggest_val,
        (mat.cols[1].x - mat.cols[0].y) * mult,
    };
  default:
    SDL_assert(false);
    return (TbQuaternion){0, 0, 0, 1};
  }
}

TbQuaternion tb_quat_from_axes(float3 forward, float3 right, float3 up) {
  return tb_f33_to_quat(tb_f33_from_axes(forward, right, up));
}

TbQuaternion tb_angle_axis_to_quat(float4 angle_axis) {
  float s = SDL_sinf(angle_axis.w * 0.5f);
  return tb_normq((TbQuaternion){
      angle_axis.x * s,
      angle_axis.y * s,
      angle_axis.z * s,
      SDL_cosf(angle_axis.w * 0.5f),
  });
}

float3x3 tb_quat_to_f33(TbQuaternion q) {
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

float4x4 tb_quat_to_f44(TbQuaternion q) {
  return tb_f33tof44(tb_quat_to_f33(q));
}

TbQuaternion tb_mulq(TbQuaternion q, TbQuaternion p) {
  return (TbQuaternion){
      (p.w * q.x) + (p.x * q.w) + (p.y * q.z) - (p.z * q.y),
      (p.w * q.y) + (p.y * q.w) + (p.z * q.x) - (p.x * q.z),
      (p.w * q.z) + (p.z * q.w) + (p.x * q.y) - (p.y * q.x),
      (p.w * q.w) - (p.x * q.x) - (p.y * q.y) - (p.z * q.z),
  };
}

// https://gamedev.stackexchange.com/questions/28395/rotating-vector3-by-a-quaternion
float3 tb_qrotf3(TbQuaternion q, float3 v) {
  float3 u = q.xyz;
  float3 uv = tb_crossf3(u, v);
  float3 uuv = tb_crossf3(u, uv);

  return v + ((uv * q.w) + uuv) * 2.0f;
}

bool tb_f4eq(float4 x, float4 y) {
  return x.x == y.x && x.y == y.y && x.z == y.z && x.w == y.w;
}
bool tb_f3eq(float3 x, float3 y) {
  return x.x == y.x && x.y == y.y && x.z == y.z;
}

bool tb_f33_eq(const float3x3 *x, const float3x3 *y) {
  return tb_f3eq(x->col0, y->col0) && tb_f3eq(x->col1, y->col1) &&
         tb_f3eq(x->col2, y->col2);
}

bool tb_f44_eq(const float4x4 *x, const float4x4 *y) {
  return tb_f4eq(x->col0, y->col0) && tb_f4eq(x->col1, y->col1) &&
         tb_f4eq(x->col2, y->col2) && tb_f4eq(x->col3, y->col3);
}

bool tb_trans_eq(const TbTransform *x, const TbTransform *y) {
  return tb_f3eq(x->position, y->position) &&
         tb_f4eq(x->rotation, y->rotation) && tb_f3eq(x->scale, y->scale);
}

TbAABB tb_aabb_init(void) {
  return (TbAABB){
      .min = {FLT_MAX, FLT_MAX, FLT_MAX},
      .max = {-FLT_MAX, -FLT_MAX, -FLT_MAX},
  };
}

void tb_aabb_add_point(TbAABB *aabb, float3 point) {
  aabb->min.x = SDL_min(aabb->min.x, point.x);
  aabb->min.y = SDL_min(aabb->min.y, point.y);
  aabb->min.z = SDL_min(aabb->min.z, point.z);
  aabb->max.x = SDL_max(aabb->max.x, point.x);
  aabb->max.y = SDL_max(aabb->max.y, point.y);
  aabb->max.z = SDL_max(aabb->max.z, point.z);
}

float tb_aabb_get_width(TbAABB aabb) {
  return SDL_fabsf(aabb.max[TB_WIDTH_IDX] - aabb.min[TB_WIDTH_IDX]);
}

float tb_aabb_get_height(TbAABB aabb) {
  return SDL_fabsf(aabb.max[TB_HEIGHT_IDX] - aabb.min[TB_HEIGHT_IDX]);
}

float tb_aabb_get_depth(TbAABB aabb) {
  return SDL_fabsf(aabb.max[TB_DEPTH_IDX] - aabb.min[TB_DEPTH_IDX]);
}

TbAABB tb_aabb_rotate(TbQuaternion q, TbAABB aabb) {
  return (TbAABB){
      .min = tb_qrotf3(q, aabb.min),
      .max = tb_qrotf3(q, aabb.max),
  };
}

TbAABB tb_aabb_transform(float4x4 m, TbAABB aabb) {
  return (TbAABB){
      .min = tb_f4tof3(tb_mulf44f4(m, tb_f3tof4(aabb.min, 1.0f))),
      .max = tb_f4tof3(tb_mulf44f4(m, tb_f3tof4(aabb.max, 1.0f))),
  };
}

void tb_translate(TbTransform *t, float3 p) {
  SDL_assert(t);
  t->position += p;
}
void tb_scale(TbTransform *t, float3 s) {
  SDL_assert(t);
  t->scale *= s;
}
void tb_rotate(TbTransform *t, TbQuaternion r) {
  SDL_assert(t);
  t->rotation = tb_mulq(t->rotation, r);
}

float3 tb_safe_reciprocal(float3 v) {
  float3 r = 0;
  if (v.x != 0) {
    r.x = 1.0f / v.x;
  }
  if (v.y != 0) {
    r.y = 1.0f / v.y;
  }
  if (v.z != 0) {
    r.z = 1.0f / v.z;
  }
  return r;
}

TbQuaternion tb_inv_quat(TbQuaternion q) {
  return tb_f4(-1, -1, -1, 1) * tb_normq(q);
}

TbTransform tb_inv_trans(TbTransform t) {
  // The opposite of what we do in transform_combine
  float3 inv_scale = tb_safe_reciprocal(t.scale);
  TbQuaternion inv_rot = tb_inv_quat(t.rotation);
  float3 scaled_position = inv_scale * t.position;
  float3 inv_pos = -tb_qrotf3(inv_rot, scaled_position);

  return (TbTransform){
      .position = inv_pos,
      .rotation = inv_rot,
      .scale = inv_scale,
  };
}

float3 tb_transform_get_forward(const TbTransform *t) {
  return tb_normf3(tb_qrotf3(t->rotation, TB_FORWARD));
}

float3 tb_transform_get_right(const TbTransform *t) {
  return tb_normf3(tb_qrotf3(t->rotation, TB_RIGHT));
}

float3 tb_transform_get_up(const TbTransform *t) {
  return tb_normf3(tb_qrotf3(t->rotation, TB_UP));
}

TbTransform tb_transform_combine(const TbTransform *x, const TbTransform *y) {
  // TbTransform the x position by the y transform
  float3 scaled = y->scale * x->position;
  float3 pos_prime = tb_qrotf3(y->rotation, scaled) + y->position;

  return (TbTransform){
      .position = pos_prime,
      .rotation = x->rotation,
      .scale = x->scale,
  };
}

float4x4 tb_transform_to_matrix(const TbTransform *t) {
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
  float4x4 r = tb_quat_to_f44(t->rotation);

  // Scale matrix
  float4x4 s = {
      (float4){t->scale.x, 0, 0, 0},
      (float4){0, t->scale.y, 0, 0},
      (float4){0, 0, t->scale.z, 0},
      (float4){0, 0, 0, 1},
  };

  // Transformation matrix = p * r * s
  float4x4 m = tb_mulf44f44(tb_mulf44f44(p, r), s);
  TracyCZoneEnd(ctx);
  return m;
}

TbTransform tb_transform_from_node(const cgltf_node *node) {
  TbTransform transform = {.position = {0}};

  transform.position = (float3){node->translation[0], node->translation[1],
                                node->translation[2]};

  transform.rotation = tb_normq((TbQuaternion){
      node->rotation[0],
      node->rotation[1],
      node->rotation[2],
      node->rotation[3],
  });

  transform.scale = (float3){node->scale[0], node->scale[1], node->scale[2]};

  return transform;
}

// Right Handed
float4x4 tb_look_forward(float3 pos, float3 forward, float3 up) {
  forward = tb_normf3(forward);
  float3 right = tb_normf3(tb_crossf3(forward, tb_normf3(up)));
  up = tb_crossf3(right, forward);

  return (float4x4){
      (float4){right.x, up.x, -forward.x, 0},
      (float4){right.y, up.y, -forward.y, 0},
      (float4){right.z, up.z, -forward.z, 0},
      (float4){-tb_dotf3(right, pos), -tb_dotf3(up, pos),
               tb_dotf3(forward, pos), 1},
  };
}

float4x4 tb_look_at(float3 pos, float3 target, float3 up) {
  float3 forward = tb_normf3(target - pos);
  return tb_look_forward(pos, forward, up);
}

TbQuaternion tb_look_forward_quat(float3 forward, float3 up) {
  float4x4 look_mat = tb_look_forward(TB_ORIGIN, forward, up);
  return tb_f33_to_quat(tb_f44tof33(look_mat));
}

TbQuaternion tb_look_at_quat(float3 pos, float3 target, float3 up) {
  float3 forward = tb_normf3(target - pos);
  return tb_look_forward_quat(forward, up);
}

TbTransform tb_look_forward_transform(float3 pos, float3 forward, float3 up) {
  float4x4 look_mat = tb_look_forward(pos, forward, up);
  return (TbTransform){
      .position = pos,
      .rotation = tb_f33_to_quat(tb_f44tof33(look_mat)),
      .scale = (float3){1, 1, 1},
  };
}

TbTransform tb_look_at_transform(float3 pos, float3 target, float3 up) {
  float3 forward = tb_normf3(target - pos);
  return tb_look_forward_transform(pos, forward, up);
}

// Right Handed
float4x4 tb_perspective(float fovy, float aspect, float zn, float zf) {
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
float4x4 tb_orthographic(float r, float l, float t, float b, float zn,
                         float zf) {
  return (float4x4){
      (float4){2.0f / (r - l), 0, 0, 0},
      (float4){0, 2.0f / (t - b), 0, 0},
      (float4){0, 0, -1 / (zf - zn), 0},
      (float4){-(r + l) / (r - l), -(t + b) / (t - b), -zn / (zf - zn), 1},
  };
}

// See //
// https://www.braynzarsoft.net/viewtutorial/q16390-34-aabb-cpu-side-frustum-culling
TbFrustum tb_frustum_from_view_proj(const float4x4 *vp) {
  TbFrustum f = {
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
    TbPlane *p = &f.planes[i];
    const float norm_length =
        tb_magf3((float3){p->xyzw.x, p->xyzw.y, p->xyzw.z});
    p->xyzw.x /= norm_length;
    p->xyzw.y /= norm_length;
    p->xyzw.z /= norm_length;
    p->xyzw.w /= norm_length;
  }
  return f;
}

bool tb_frustum_test_aabb(const TbFrustum *frust, const TbAABB *aabb) {
  // See
  // https://www.braynzarsoft.net/viewtutorial/q16390-34-aabb-cpu-side-frustum-culling
  for (uint32_t i = 0; i < FrustumPlaneCount; ++i) {
    const TbPlane *plane = &frust->planes[i];
    const float3 normal = (float3){plane->xyzw.x, plane->xyzw.y, plane->xyzw.z};
    const float plane_const = plane->xyzw.w;

    float3 axis = {
        normal.x < 0.0f ? aabb->min.x : aabb->max.x,
        normal.y < 0.0f ? aabb->min.y : aabb->max.y,
        normal.z < 0.0f ? aabb->min.z : aabb->max.z,
    };

    if (tb_dotf3(normal, axis) + plane_const < 0.0f) {
      // The TbAABB was outside one of the planes and failed this test
      return false;
    }
  }
  // The TbAABB was inside each plane
  return true;
}

float tb_deg_to_rad(float d) { return d * (M_PI / 180.0f); }
float tb_rad_to_deg(float r) { return r * (180 / M_PI); }

// https://en.wikipedia.org/wiki/Linear_interpolation
float tb_lerpf(float f0, float f1, float a) { return (1 - a) * f0 + a * f1; }
float3 tb_lerpf3(float3 v0, float3 v1, float a) {
  return ((1 - a) * v0) + (a * v1);
}

// https://www.euclideanspace.com/maths/algebra/realNormedAlgebra/quaternions/slerp/index.htm
TbQuaternion tb_slerp(TbQuaternion q0, TbQuaternion q1, float a) {
  // Calc angle between quaternions
  float cos_half_theta = q0.w * q1.w + q0.x * q1.x + q0.y * q1.y + q0.z * q1.z;
  // If q1=q2 or q1=-q2 then alpha = 0 and we can return q1
  if (SDL_fabsf(cos_half_theta) >= 1.0f) {
    return q0;
  }

  float half_theta = SDL_acosf(cos_half_theta);
  float sin_half_theta = SDL_sqrtf(1.0f - cos_half_theta * cos_half_theta);
  // if alpha = 180 degrees then result is not fully defined
  // we could rotate around any axis normal to qa or qb
  if (SDL_fabsf(sin_half_theta) < 0.001f) { // fabs is floating point absolute
    return (TbQuaternion){
        q0.x * 0.5f + q1.x * 0.5f,
        q0.y * 0.5f + q1.y * 0.5f,
        q0.z * 0.5f + q1.z * 0.5f,
        q0.w * 0.5f + q1.w * 0.5f,
    };
  }

  float ratio_a = SDL_sinf((1.0f - a) * half_theta) / sin_half_theta;
  float ratio_b = SDL_sinf(a * half_theta) / sin_half_theta;

  return (TbQuaternion){
      q0.x * ratio_a + q1.x * ratio_b,
      q0.y * ratio_a + q1.y * ratio_b,
      q0.z * ratio_a + q1.z * ratio_b,
      q0.w * ratio_a + q1.w * ratio_b,
  };
}

TbTransform tb_trans_lerp(TbTransform t0, TbTransform t1, float a) {
  return (TbTransform){
      .position = tb_lerpf3(t0.position, t1.position, a),
      .rotation = tb_slerp(t0.rotation, t1.rotation, a),
      .scale = tb_lerpf3(t0.scale, t1.scale, a),
  };
}

float tb_clampf(float v, float min, float max) {
  if (v < min) {
    return min;
  }
  if (v > max) {
    return max;
  }
  return v;
}

float3 tb_clampf3(float3 v, float3 min, float3 max) {
  return (float3){
      tb_clampf(v.x, min.x, max.x),
      tb_clampf(v.y, min.y, max.y),
      tb_clampf(v.z, min.z, max.z),
  };
}

#pragma clang diagnostic pop
