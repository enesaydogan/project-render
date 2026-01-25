// shaders/raytracing/miss.hlsl
// Miss shader

#include "common.hlsli"

[shader("miss")]
void Miss(inout RayPayload payload)
{
    payload.color = float4(0.2, 0.2, 0.2, 1.0); // Dark grey background
}