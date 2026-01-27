// shaders/raytracing/miss.hlsl
// Miss shader

#include "common.hlsli"

[shader("miss")]
void Miss(inout RayPayload payload)
{
    payload.color = float4(0.1, 0.1, 0.12, 1.0); // Match raster background
}