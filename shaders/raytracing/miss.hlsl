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
    
    // In PT mode, we skip tone mapping here and do it in RayGen after accumulation
    payload.color = color;
    payload.t = -1.0;
    payload.normal = float3(0,0,0);
    payload.position = float3(0,0,0);
    payload.albedo = float3(0,0,0);
}
