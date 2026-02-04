// shaders/raytracing/miss.hlsl
// Miss shader

#include "common.hlsli"
#include "../clouds.hlsl" // Volume Clouds logic

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

    if (cosTheta > cosSunRadius) {
         // Simple sun disc (use integrated lightColor)
         color += lightColor.rgb * intensity;
    }

    // --- Volumetric Clouds ---
    float tMin = 0.0;
    float tMax = 50000.0; // Far
    
    // Pass global lightColor logic
    float4 cloudRes = RaymarchClouds(WorldRayOrigin(), dir, tMin, tMax, L, lightColor.rgb);

    // If a cloud debug view is selected, show it directly.
    // (Avoid compositing with the environment, which makes debug hard to read.)
    int dbg = (int)debugMode;
    if (dbg >= 11) {
        color = cloudRes.rgb;
    } else {
        // Composite clouds over sky
        // cloudRes.rgb is accumulated color, cloudRes.a is Final Transmittance (0 = blocked, 1 = transparent)
        // So: Color = Sky * Transmittance + CloudColor
        color = color * cloudRes.a + cloudRes.rgb;
    }

    // Fill payload
    // RayPayload in common.hlsli has float3 color
    payload.color = color;
    payload.t = -1.0;
    
    // Fill remaining payload members to default to avoid undefined behavior or validation errors
    payload.normal = float3(0,0,0);
    payload.position = float3(0,0,0);
    payload.albedo = float3(0,0,0);
    payload.emissive = float3(0,0,0);
    payload.refractionColor = float3(0,0,0);
    payload.ior = 1.0;
    payload.roughness = 0.0;
    payload.metalness = 0.0;
    payload.thinWalled = 0.0;
    payload.translucency = 0.0;
    payload.matIndex = 0;
}
