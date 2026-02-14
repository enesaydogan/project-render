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

    // --- Volumetric Clouds (baked) ---
    // Use pre-baked lat-long cloud texture for misses/reflections/refractions.
    bool allowClouds = (payload.rayType == RAY_TYPE_PRIMARY || 
                        payload.rayType == RAY_TYPE_REFLECTION || 
                        payload.rayType == RAY_TYPE_REFRACTION ||
                        payload.rayType == RAY_TYPE_DIFFUSE);
    if (cloudRenderingEnabled > 0.5 && allowClouds) {
        int dbg = (int)SHADER_DEBUG_MODE;
        bool cloudDebugView = (dbg >= 11 && dbg <= 16);

        float2 skyUv = DirectionToUV(dir);
        float4 baked = bakedClouds.SampleLevel(linearSampler, skyUv, 0);
        baked.a = saturate(baked.a);
        baked.rgb = max(baked.rgb, 0.0);

        if (cloudDebugView) {
            color = baked.rgb;
        } else {
            // Composite: Sky * Transmittance + CloudColor
            float3 skyColor = color;
            float3 fullCloudColor = skyColor * baked.a + baked.rgb;
            color = clamp(fullCloudColor, 0.0, 128.0);
        }
    }

    // Fill payload
    // RayPayload in common.hlsli has float3 color
    payload.color = max(color, 0.0);
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
    // Preserve rayDepth; set by caller before TraceRay.
}
