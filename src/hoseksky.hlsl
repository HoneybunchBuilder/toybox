// Adapted from: https://www.shadertoy.com/view/wslfD7
#include "common.hlsli"

#include "hosek.hlsli"

[[vk::push_constant]]
ConstantBuffer<SkyPushConstants> consts : register(b0, space0);

#define CIE_X 0
#define CIE_Y 1
#define CIE_Z 2
#define M_PI 3.1415926535897932384626433832795

struct SkyData {
  uint32_t turbidity;
  uint32_t albedo;
  float3 sun_dir;
};
ConstantBuffer<SkyData> sky_data : register(b1, space0); // Fragment Stage Only
TextureBuffer<SkyHosekData> hosek_data : register(t0, space1); // Fragment Stage Only

struct VertexIn {
  float3 local_pos : SV_POSITION;
};

struct Interpolators {
  float4 clip_pos : SV_POSITION;
  float3 view_pos : POSITION0;
};

struct FragmentOut {
  float4 color : SV_TARGET;
  float depth : SV_DEPTH;
};

Interpolators vert(VertexIn i) {
  Interpolators o;
  o.view_pos = i.local_pos;
  o.view_pos.xy *= -1.0;
  o.clip_pos = mul(float4(i.local_pos, 1.0), consts.vp);
  return o;
}

float sample_coeff(int channel, int albedo, int turbidity, int quintic_coeff, int coeff) {
    int index =  540 * albedo + 54 * turbidity + 9 * quintic_coeff + coeff;
  if (channel == CIE_X) return hosek_data.coeffsX[index];
  if (channel == CIE_Y) return hosek_data.coeffsY[index];
  if (channel == CIE_Z) return hosek_data.coeffsZ[index];
  return 0;
}

float sample_radiance(int channel, int albedo, int turbidity, int quintic_coeff) {
    int index = 60 * albedo + 6 * turbidity + quintic_coeff;
  if (channel == CIE_X) return hosek_data.radX[index];
  if (channel == CIE_Y) return hosek_data.radY[index];
  if (channel == CIE_Z) return hosek_data.radZ[index];
  return 0;
}

float eval_quintic_bezier(float control_points[6], float t) {
  float t2 = t * t;
  float t3 = t2 * t;
  float t4 = t3 * t;
  float t5 = t4 * t;
  
  float t_inv = 1.0 - t;
  float t_inv2 = t_inv * t_inv;
  float t_inv3 = t_inv2 * t_inv;
  float t_inv4 = t_inv3 * t_inv;
  float t_inv5 = t_inv4 * t_inv;
    
  return (
    control_points[0] *             t_inv5 +
    control_points[1] *  5.0 * t  * t_inv4 +
    control_points[2] * 10.0 * t2 * t_inv3 +
    control_points[3] * 10.0 * t3 * t_inv2 +
    control_points[4] *  5.0 * t4 * t_inv  +
    control_points[5] *        t5
  );
}

float transform_sun_zenith(float sun_zenith) {
  float elevation = M_PI / 2.0 - sun_zenith;
    return pow(elevation / (M_PI / 2.0), 0.333333);
}

void get_control_points(int channel, int albedo, int turbidity, int coeff, out float control_points[6]) {
  for (int i = 0; i < 6; ++i) control_points[i] = sample_coeff(channel, albedo, turbidity, i, coeff);
}

void get_control_points_radiance(int channel, int albedo, int turbidity, out float control_points[6]) {
  for (int i = 0; i < 6; ++i) control_points[i] = sample_radiance(channel, albedo, turbidity, i);
}

void get_coeffs(int channel, int albedo, int turbidity, float sun_zenith, out float coeffs[9]) {
  float t = transform_sun_zenith(sun_zenith);
  for (int i = 0; i < 9; ++i) {
    float control_points[6]; 
    get_control_points(channel, albedo, turbidity, i, control_points);
    coeffs[i] = eval_quintic_bezier(control_points, t);
  }
}

float3 mean_spectral_radiance(int albedo, int turbidity, float sun_zenith) {
  float3 spectral_radiance;
  for (int i = 0; i < 3; ++i) {
    float control_points[6];
        get_control_points_radiance(i, albedo, turbidity, control_points);
    float t = transform_sun_zenith(sun_zenith);
    spectral_radiance[i] = eval_quintic_bezier(control_points, t);
  }
  return spectral_radiance;
}

float F(float theta, float gamma, float coeffs[9]) {
  float A = coeffs[0];
  float B = coeffs[1];
  float C = coeffs[2];
  float D = coeffs[3];
  float E = coeffs[4];
  float F = coeffs[5];
  float G = coeffs[6];
  float H = coeffs[8];
  float I = coeffs[7];
  float chi = (1.0 + pow(cos(gamma), 2.0)) / pow(1.0 + H*H - 2.0 * H * cos(gamma), 1.5);
  
  return (
    (1.0 + A * exp(B / (cos(theta) + 0.01))) *
    (C + D * exp(E * gamma) + F * pow(cos(gamma), 2.0) + G * chi + I * sqrt(cos(theta)))
  );
}

float3 spectral_radiance(float theta, float gamma, int albedo, int turbidity, float sun_zenith) {
  float3 XYZ;
  for (int i = 0; i < 3; ++i) {
    float coeffs[9];
    get_coeffs(i, albedo, turbidity, sun_zenith, coeffs);
    XYZ[i] = F(theta, gamma, coeffs);
  }
  return XYZ;
}

// Returns angle between two directions defined by zentih and azimuth angles
float angle(float z1, float a1, float z2, float a2) {
  return acos(
    sin(z1) * cos(a1) * sin(z2) * cos(a2) +
    sin(z1) * sin(a1) * sin(z2) * sin(a2) +
    cos(z1) * cos(z2));
}

float3 sample_sky(float gamma, float theta, uint32_t albedo, uint32_t turbidity, float sun_zenith) {
  return spectral_radiance(theta, gamma, albedo, turbidity, sun_zenith) * mean_spectral_radiance(albedo, turbidity, sun_zenith);
}

// CIE-XYZ to linear RGB
float3 XYZ_to_RGB(float3 XYZ) {
  float3x3 XYZ_to_linear = float3x3(
     3.24096994, -0.96924364, 0.55630080,
    -1.53738318,  1.8759675, -0.20397696,
    -0.49861076,  0.04155506, 1.05697151
  );
  return mul(XYZ, XYZ_to_linear);
}

// Clamps color between 0 and 1 smoothly
float3 expose(float3 color, float exposure) {
  return float3(2, 2, 2) / (float3(1, 1, 1) + exp(-exposure * color)) - float3(1, 1, 1);
}

float angle_of_dot(float dot) { return acos(max(dot, 0.0000001f)); }

FragmentOut frag(Interpolators i) {
  float3 sample_dir = i.view_pos;
  float3 sun_dir = sky_data.sun_dir;

  float cos_theta = dot(sample_dir, float3(0, 1, 0));
  float cos_gamma = dot(sample_dir, sun_dir);

  float gamma = angle_of_dot(cos_gamma);
  float theta = angle_of_dot(cos_theta);

  float sun_zenith = clamp(((sun_dir.y - 1) * -0.5f) * M_PI, 0.0f, M_PI / 2.0f - 0.1f);

  float3 XYZ = sample_sky(gamma, theta, sky_data.albedo, sky_data.turbidity, sun_zenith);
  float3 RGB = XYZ_to_RGB(XYZ);

  float3 col = expose(RGB, 0.1);
  
  FragmentOut o;
  o.color = float4(col, 1.0);
  o.depth = 0; // The skybox has no depth no matter what the geometry says

  return o;
}

// No-op version of this shader for debugging shader compiler crashes
/*
#include "common.hlsli"

[[vk::push_constant]]
ConstantBuffer<SkyPushConstants> consts : register(b0);

struct SkyData {
  float turbidity;
  float albedo;
  float3 sun_dir;
};
ConstantBuffer<SkyData> sky_data : register(b1, space0); // Fragment Stage Only

struct VertexIn {
  float3 local_pos : SV_POSITION;
};

struct Interpolators {
};

struct FragmentOut {
  float4 color : SV_TARGET;
};

Interpolators vert(VertexIn i) {
  Interpolators o;
  return o;
}

FragmentOut frag(Interpolators i) {  
  FragmentOut o;
  o.color = float4(1.0, 0.0, 0.0, 1.0);
  return o;
}
*/
