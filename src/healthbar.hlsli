#include "fullscreenvert.hlsli"

[[vk::push_constant]] ConstantBuffer<FullscreenPushConstants> consts
    : register(b0);

float4 frag(Interpolators i) : SV_TARGET {
  float seconds = consts.time[0];

  // SDF for Rounding
  float2 coords = i.uv0;
  coords.x *= 8.0;
  float2 pointOnLineSeg = float2(clamp(coords.x, 0.5, 7.5), 0.5);
  float sdf = distance(coords, pointOnLineSeg) * 2 - 1;
  clip(-sdf);

  // Border SDF
  const float borderSize = 0.3;
  float borderSdf = sdf + borderSize;
  float pd =
      fwidth(borderSdf); // Screen space partial derivative for anti-aliasing
  float invBorderMask = saturate(borderSdf / pd);
  float3 borderMask = 1 - invBorderMask;
  float3 borderColor = float3(0.3, 0.3, 0.3);

  // TODO: Move to parameter
  float health = (sin(seconds * 0.3) + 1) * 0.5;

  // Flash at low health
  float healthBarMask = health > i.uv0.x;
  float3 healthBarColor = lerp(float3(1, 0, 0), float3(0, 1, 0), health);
  if (health < 0.2) {
    float flash = cos(seconds * 4) * 0.4 + 1;
    healthBarColor *= flash;
  }

  // Final Color composition
  healthBarColor *= healthBarMask * borderMask;
  borderColor *= invBorderMask;

  float3 finalColor = healthBarColor + borderColor;
  return float4(finalColor, 1);
}
