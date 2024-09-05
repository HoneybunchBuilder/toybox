// Adapted from Nvidia's FXAA Whitepaper
// https://developer.download.nvidia.com/assets/gamedev/files/sdk/11/FXAA_WhitePaper.pdf
// And this version from choi303
// https://github.com/choi303/FXAA/blob/main/FXAA/PSFXAA.hlsl

#include "tb_fullscreenvert.slangh"
#include "tb_fxaa.slangh"

FXAA_SET(0)

// Luminance is estimated only from the red and green channels
float fxaa_luma(float3 rgb) { return rgb.g * (0.587 / 0.299) + rgb.r; }

// This is a macro because otherwise the offset will not properly be marked as
// constant by the slang spirv compiler
#define sample_offset(tex, uv, offset)                                         \
  tex.SampleLevel(in_sampler, uv, 0, offset).rgb

float3 sample_lod0(Texture2D tex, float2 uv) {
  return tex.SampleLevel(in_sampler, uv, 0).xyz;
}

float4 frag(Interpolators i) : SV_Target {
  if (consts.on == 0) {
    return float4(sample_lod0(input, i.uv0), 1.0f);
  }

  float2 input_size;
  input.GetDimensions(input_size.x, input_size.y);
  float2 texel_size = float2(1.0 / input_size.x, 1.0 / input_size.y);

  // Sample neighboring pixels
  float3 rgb_n = sample_offset(input, i.uv0, int2(0, -1));
  float3 rgb_w = sample_offset(input, i.uv0, int2(-1, 0));
  float3 rgb_m = sample_offset(input, i.uv0, int2(0, 0));
  float3 rgb_e = sample_offset(input, i.uv0, int2(1, 0));
  float3 rgb_s = sample_offset(input, i.uv0, int2(0, 1));
  // Calculate luma of each sample
  float luma_n = fxaa_luma(rgb_n);
  float luma_w = fxaa_luma(rgb_w);
  float luma_m = fxaa_luma(rgb_m);
  float luma_e = fxaa_luma(rgb_e);
  float luma_s = fxaa_luma(rgb_s);
  // Calculate range of luma values
  float range_min = min(luma_m, min(min(luma_n, luma_w), min(luma_s, luma_e)));
  float range_max = max(luma_m, max(max(luma_n, luma_w), max(luma_s, luma_e)));
  float range = range_max - range_min;
  // Early out from dark areas with no contrast
  if (range <
      max(consts.edge_threshold_min, range_max * consts.edge_threshold)) {
    return float4(rgb_m, 1.0f);
  }
  float3 rgb_l = rgb_n + rgb_w + rgb_m + rgb_e + rgb_s;

  // Subpixel aliasing test
  float blend_l = 0;
  if (consts.subpixel > TB_FXAA_SUBPIXEL_OFF) {
    float luma_l = (luma_n + luma_w + luma_e + luma_s) * 0.25;
    float range_l = abs(luma_l - luma_m);
    blend_l = max(0.0, (range_l / range) - consts.subpixel_trim) *
              consts.subpixel_trim_scale;
    blend_l = min(consts.subpixel_cap, blend_l);
  }

  // Lowpass filter requires a 3x3 grid of samples
  float3 rgb_nw = sample_offset(input, i.uv0, int2(-1, -1));
  float3 rgb_ne = sample_offset(input, i.uv0, int2(1, -1));
  float3 rgb_sw = sample_offset(input, i.uv0, int2(-1, 1));
  float3 rgb_se = sample_offset(input, i.uv0, int2(1, 1));

  if (consts.subpixel != TB_FXAA_SUBPIXEL_FASTER) {
    rgb_l += rgb_nw + rgb_ne + rgb_sw + rgb_se;
    rgb_l *= (1.0f / 9.0f);
  }

  // Determine vertical or horizontal search
  float luma_nw = fxaa_luma(rgb_nw);
  float luma_ne = fxaa_luma(rgb_ne);
  float luma_sw = fxaa_luma(rgb_sw);
  float luma_se = fxaa_luma(rgb_se);
  float edge_vert = abs((0.25 * luma_nw) + (-0.5 * luma_n) + (0.25 * luma_ne)) +
                    abs((0.50 * luma_w) + (-1.0 * luma_m) + (0.50 * luma_e)) +
                    abs((0.25 * luma_sw) + (-0.5 * luma_s) + (0.25 * luma_se));
  float edge_horz = abs((0.25 * luma_nw) + (-0.5 * luma_w) + (0.25 * luma_sw)) +
                    abs((0.50 * luma_n) + (-1.0 * luma_m) + (0.50 * luma_s)) +
                    abs((0.25 * luma_ne) + (-0.5 * luma_e) + (0.25 * luma_se));
  bool horz_span = edge_horz >= edge_vert;
  float len_sign = horz_span ? -texel_size.y : -texel_size.x;
  if (!horz_span) {
    luma_n = luma_w;
    luma_s = luma_e;
  }
  float grad_n = abs(luma_n - luma_m);
  float grad_s = abs(luma_s - luma_m);
  luma_n = (luma_n + luma_m) * 0.5f;
  luma_s = (luma_s + luma_m) * 0.5f;

  // Choose side of pixel where gradient is highest
  bool pair_n = grad_n >= grad_s;
  if (!pair_n) {
    luma_n = luma_s;
    grad_n = grad_s;
    len_sign *= -1.0f;
  }
  float2 pos_n = float2(i.uv0.x + (horz_span ? 0.0 : len_sign * 0.5),
                        i.uv0.y + (horz_span ? len_sign * 0.5 : 0.0));

  grad_n *= consts.search_threshold;

  // Search in both directions until we find a luma pair average that is out of
  // range
  float2 pos_p = pos_n;
  float2 off_np =
      horz_span ? float2(texel_size.x, 0.0) : float2(0.0, texel_size.y);
  float luma_end_n = luma_n;
  float luma_end_p = luma_n;
  bool done_n = false;
  bool done_p = false;
  if (consts.search_accel == 1) {
    pos_n += off_np * -1;
    pos_p += off_np;
  }
  for (int32_t i = 0; i < consts.search_steps; ++i) {
    if (consts.search_accel == 1) {
      if (!done_n) {
        luma_end_n = fxaa_luma(sample_lod0(input, pos_n));
      }
      if (!done_p) {
        luma_end_p = fxaa_luma(sample_lod0(input, pos_p));
      }
    }
    done_n = done_n || (abs(luma_end_n - luma_n) > grad_n);
    done_p = done_p || (abs(luma_end_p - luma_n) > grad_n);
    if (done_n && done_p) {
      break;
    }
    if (!done_n) {
      pos_n -= off_np;
    }
    if (!done_p) {
      pos_p += off_np;
    }
  }

  // Handle if center is on positive or negative side
  float dst_n = horz_span ? i.uv0.x - pos_n.x : i.uv0.y - pos_n.y;
  float dst_p = horz_span ? pos_p.x - i.uv0.x : pos_p.y - i.uv0.y;
  bool dir_n = dst_n < dst_p;

  // Check if pixel is in part of span that gets no filtering
  if (((luma_m - luma_n) < 0.0f) == ((luma_end_n - luma_n) < 0.0f)) {
    len_sign = 0.0f;
  }

  float span_len = dst_p + dst_n;
  dst_n = dir_n ? dst_n : dst_p;
  float sub_pixel_offset = (0.5f + (dst_n * (-1.0f / span_len))) * len_sign;
  float3 rgb_f = sample_lod0(
      input, float2(i.uv0.x + (horz_span ? 0.0 : sub_pixel_offset),
                    i.uv0.y + (horz_span ? sub_pixel_offset : 0.0)));
  return float4(lerp(rgb_l, rgb_f, blend_l), 1.0f);
}
