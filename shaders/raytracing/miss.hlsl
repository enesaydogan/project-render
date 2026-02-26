// shaders/raytracing/miss.hlsl
// Miss shader

#include "common.hlsli"
#include "../clouds.hlsl" // Volume Clouds logic

[shader("miss")]
void Miss(inout RayPayload payload)
{
    float3 dir = WorldRayDirection();
    float2 uv = DirectionToUVRotated(dir);
    
    // Sample environment map and apply intensity
    float3 color = envMap.SampleLevel(linearSampler, uv, 0).rgb * intensity;

    // Add Analytic Sun Disc
    float3 L = normalize(lightDir.xyz);
    float cosTheta = dot(normalize(dir), L);
    float cosSunRadius = cos(lightDir.w);

    // Do not add sun disc for rays that already use Next Event Estimation (NEE)
    // to avoid double-counting the sun.
    bool useNEE = (payload.rayType == RAY_TYPE_DIFFUSE || payload.rayType == RAY_TYPE_REFLECTION || payload.rayType == RAY_TYPE_REFRACTION);
    if (cosTheta > cosSunRadius && !useNEE) {
         // Physically correct sun radiance = Illuminance (Lux) / Solid Angle (sr)
         // Omega = 2 * PI * (1 - cos(theta))
         float sunSolidAngle = 2.0f * PI * (1.0f - cosSunRadius);
         float3 sunRadiance = (lightColor.rgb * lightColor.w) / max(sunSolidAngle, 1e-7f);
         const float dxrSunDiscMatchGain = 1.12f;
         color = sunRadiance * intensity * dxrSunDiscMatchGain;
    }
    
    // --- Volumetric Clouds (baked) ---
    // Use pre-baked lat-long cloud texture for misses/reflections/refractions.
    bool allowClouds = (payload.rayType == RAY_TYPE_PRIMARY || 
                        payload.rayType == RAY_TYPE_REFLECTION || 
                        payload.rayType == RAY_TYPE_REFRACTION ||
                        payload.rayType == RAY_TYPE_DIFFUSE ||
                        payload.rayType == RAY_TYPE_GI_EVAL);
    if (cloudRenderingEnabled > 0.5 && allowClouds) {
        int dbg = (int)SHADER_DEBUG_MODE;
        bool cloudDebugView = (dbg >= 11 && dbg <= 16);
        
        float2 skyUv = DirectionToUVRotated(dir);
        float4 baked = bakedClouds.SampleLevel(linearSampler, skyUv, 0);
        baked.a = saturate(baked.a);
        baked.rgb = max(baked.rgb, 0.0);
        
        if (cloudDebugView) {
            color = baked.rgb;
        } else {
            // Composite: Sky * Transmittance + CloudColor
            float3 skyColor = color;
            float opacity = 1.0 - baked.a;
            // Lift only dense cloud cores to keep silhouette and edge contrast.
            float denseCore = pow(saturate(opacity), 2.2);
            float skyLeak = 0.10 * denseCore;
            float3 fullCloudColor = skyColor * (baked.a + skyLeak) + baked.rgb;
            // Additional soft floor, biased to dense regions only.
            fullCloudColor += skyColor * (0.025 * denseCore);
            // Relaxed clamp for physical units + exposure
            color = clamp(fullCloudColor, 0.0, 100000.0);
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
