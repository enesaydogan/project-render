// shaders/raytracing/miss.hlsl
// Miss shader

#include "common.hlsli"
#include "../clouds.hlsl" // Volume Clouds logic

[shader("miss")]
void Miss(inout RayPayload payload)
{
    float3 dir = WorldRayDirection();
    uint rayType = UnpackPayloadRayType(payload.packedIorType);
    float2 uv = DirectionToUVRotated(dir);

    // Keep direct camera view and transmission-through-glass view aligned so
    // sky/cloud appearance does not shift when looking through windows.
    bool isViewLikeRay = (rayType == RAY_TYPE_PRIMARY || rayType == RAY_TYPE_REFRACTION);
    float pathDistance = max(length(WorldRayOrigin() - camPos), 1e-3);
    float envLod = isViewLikeRay ? 0.0 : clamp(log2(pathDistance * 0.02) + 0.35, 0.0, 10.0);
    float cloudLod = isViewLikeRay ? 0.0 : min(10.0, envLod + 0.5);

    // Sample scene-linear environment radiance. Camera exposure is applied once
    // in the DXR tonemap pass after accumulation.
    float3 color = envMap.SampleLevel(linearSampler, uv, envLod).rgb *
                   GetDxrProceduralSkyBoost();

    // Add Analytic Sun Disc
    float3 L = normalize(lightDir.xyz);
    float cosTheta = dot(normalize(dir), L);
    float cosSunRadius = cos(lightDir.w);

    // Do not add sun disc for rays that already use Next Event Estimation (NEE)
    // to avoid double-counting the sun.
    bool useNEE = (rayType == RAY_TYPE_DIFFUSE || rayType == RAY_TYPE_REFLECTION || rayType == RAY_TYPE_REFRACTION);
    if (cosTheta > cosSunRadius && !useNEE) {
         // Physically correct sun radiance = Illuminance (Lux) / Solid Angle (sr)
         // Omega = 2 * PI * (1 - cos(theta))
         float sunSolidAngle = 2.0f * PI * (1.0f - cosSunRadius);
         float3 sunRadiance = (lightColor.rgb * lightColor.w) / max(sunSolidAngle, 1e-7f);
         const float dxrSunDiscMatchGain = 1.12f;
         color = sunRadiance * dxrSunDiscMatchGain;
    }
    
    // --- Volumetric Clouds (baked) ---
    // Use pre-baked lat-long cloud texture for misses/reflections/refractions.
    bool allowClouds = (rayType == RAY_TYPE_PRIMARY ||
                        rayType == RAY_TYPE_REFLECTION ||
                        rayType == RAY_TYPE_REFRACTION ||
                        rayType == RAY_TYPE_DIFFUSE ||
                        rayType == RAY_TYPE_GI_EVAL);
    if (cloudRenderingEnabled > 0.5 && allowClouds) {
        int dbg = (int)SHADER_DEBUG_MODE;
        bool cloudDebugView = (dbg >= 11 && dbg <= 16);
        
        float2 skyUv = DirectionToUVRotated(dir);
        float4 baked = bakedClouds.SampleLevel(linearSampler, skyUv, cloudLod);
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
            float skyLeak = 0.035 * denseCore;
            float3 fullCloudColor = skyColor * (baked.a + skyLeak) + baked.rgb;
            // Additional soft floor, biased to dense regions only.
            fullCloudColor += skyColor * (0.006 * denseCore);
            // Relaxed clamp for physical units + exposure
            color = clamp(fullCloudColor, 0.0, 100000.0);
        }
    }

    // Fill payload
    // RayPayload in common.hlsli has float3 color
    PayloadSetColor(payload, max(color, 0.0));
    payload.t = -1.0;

    // Fill remaining payload members to defaults to avoid undefined behavior.
    payload.packedNormal = PackNormalOctahedron(float3(0, 1, 0));
    payload.packedAlbedo = PackPayloadAlbedo(float3(0, 0, 0));
    payload.packedSurface = PackPayloadSurface(0.0, 0.0, 0.0, 0.0);
    payload.surface = float4(0.0, 0.0, 0.0, 0.0);
    payload.packedIorType = PackPayloadIorType(1.0, rayType, false, 1.0);
    payload.packedTransmission = PackPayloadTransmissionColor(float3(1.0, 1.0, 1.0));
    payload.packedSpecular = PackPayloadSpecularColor(float3(1.0, 1.0, 1.0));
}
