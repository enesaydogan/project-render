// shaders/raytracing/hit.hlsl
// Closest hit shader

#include "common.hlsli"

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    // For now, just show a solid color to test raytracing
    payload.color = float4(1.0, 0.5, 0.0, 1.0); // Orange color
}