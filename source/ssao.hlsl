#include "fullscreenvert.hlsli"

Texture2D depth_map : register(t0, space0);    // Fragment Stage Only
Texture2D normal_map : register(t1, space0);    // Fragment Stage Only
sampler static_sampler : register(s2, space0); // Immutable Sampler

float frag(Interpolators i) : SV_TARGET {
    return 1.0f;
}