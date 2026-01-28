// shaders/raytracing/miss.hlsl
// Miss shader

#include "common.hlsli"

[shader("miss")]
void Miss(inout RayPayload payload)
{
    float3 dir = WorldRayDirection();
    float2 uv = DirectionToUV(dir);
    
    // Sample environment map and apply intensity
    float3 color = envMap.SampleLevel(linearSampler, uv, 0).rgb * intensity;
    
    // Apply tone mapping and gamma to match the hit shader (Standardized Output)
    color = color / (color + 1.0);
    color = pow(color, 1.0/2.2);
    
    payload.color = float4(color, 1.0);
}
