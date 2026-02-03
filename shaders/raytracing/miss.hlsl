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

    // Add Analytic Sun Disc
    // lightDir.w holds the sun *radius* in radians (set in main.cpp)
    float3 L = normalize(lightDir.xyz);
    float cosTheta = dot(normalize(dir), L);
    // Use cosine of angular radius
    float cosSunRadius = cos(lightDir.w);
    
    // Draw sun disc if ray points within the cone
    if (cosTheta > cosSunRadius) {
        // Evaluate sun radiance (Color * Intensity)
        // We replace the sky color with the sun color here
        color = lightColor.rgb * lightColor.w;
    }
    
    // In PT mode, we skip tone mapping here and do it in RayGen after accumulation
    payload.color = color;
    payload.t = -1.0;
    payload.normal = float3(0,0,0);
    payload.position = float3(0,0,0);
    payload.albedo = float3(0,0,0);
    payload.emissive = float3(0,0,0);
    payload.refractionColor = float3(0,0,0);
    payload.ior = 1.0;
    payload.roughness = 1.0;
    payload.metalness = 0.0;
    payload.thinWalled = 0.0;
    payload.translucency = 0.0;
    payload.matIndex = 0;
}
