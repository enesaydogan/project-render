// shaders/raytracing.hlsl
// Main raytracing shader library - includes all raytracing shaders

// Include common definitions
#include "raytracing/common.hlsli"

// Include wavefront shader files. The legacy monolithic RayGen path is no
// longer part of the shipping DXR library.
#include "raytracing/wavefront_primary_raygen.hlsl"
#include "raytracing/wavefront_secondary_raygen.hlsl"
#include "raytracing/wavefront_shadow_raygen.hlsl"
#include "raytracing/miss.hlsl"
#include "raytracing/hit.hlsl"
