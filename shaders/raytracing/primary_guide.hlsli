#ifndef RAYTRACING_PRIMARY_GUIDE_HLSLI
#define RAYTRACING_PRIMARY_GUIDE_HLSLI

inline float3 BuildPrimaryCenterDirection(uint2 pixel, uint2 dimensions)
{
    if (dimensions.x == 0u || dimensions.y == 0u) {
        return normalize(camForward);
    }

    float2 uv = (float2(pixel) + 0.5) / float2(dimensions);
    return BuildCameraPrimaryDirection(uv);
}

inline void TracePrimaryGuide(float3 initialOrigin,
                              float3 initialDirection,
                              out float3 guideOrigin,
                              out float3 guideDirection,
                              out RayPayload guidePayload,
                              out uint guideState)
{
    guideOrigin = initialOrigin;
    guideDirection = normalize(initialDirection);
    guidePayload = InitRayPayload(RAY_TYPE_PRIMARY);
    guideState = 0u;

    uint guideRayType = RAY_TYPE_PRIMARY;
    const uint maxPrimaryDeltaSteps =
        min(8u, max(1u, (uint)maxRefractiveBounces));
    [loop]
    for (uint deltaStep = 0u; deltaStep < maxPrimaryDeltaSteps; ++deltaStep) {
        RayDesc ray;
        ray.Origin = guideOrigin;
        ray.Direction = guideDirection;
        ray.TMin = 0.002;
        ray.TMax = 10000.0;

        RayPayload payload = InitRayPayload(guideRayType);
        TraceRay(g_accel, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
        SHADER_COUNTER_ADD(SHADER_COUNTER_TRACE_RAYS, 1);

        if (payload.t < 0.0) {
            guidePayload = payload;
            guideState |= WAVEFRONT_GUIDE_STATE_MISS;
            return;
        }

        const float4 surface = saturate(payload.surface);
        const float roughness = max(0.001, surface.x);
        const float transmission = surface.z;
        const float ior = UnpackPayloadIor(payload.packedIorType);
        if (!ShouldResolveDeltaTransmission(roughness, transmission, ior)) {
            guidePayload = payload;
            return;
        }

        float3 guidePos = guideOrigin + guideDirection * payload.t;
        float3 nextDirection = guideDirection;
        const bool thinWalled = UnpackPayloadThinWalled(payload.packedIorType);
        const float thickness = UnpackPayloadThickness(payload.packedSpecular);

        if (!thinWalled) {
            const float3 normal = UnpackNormalOctahedron(payload.packedNormal);
            if (!RefractDeterministic(-guideDirection, normal, ior,
                                      nextDirection)) {
                guidePayload = payload;
                return;
            }

            if (guideRayType != RAY_TYPE_REFRACTION) {
                const float travel = EffectiveArchGlassThickness(thickness) /
                                     max(abs(dot(nextDirection, normal)), 0.2);
                guidePos = guidePos + nextDirection * min(travel, 2.0);
                nextDirection = guideDirection;
            }
        }

        guideOrigin = guidePos + nextDirection * 0.002;
        guideDirection = normalize(nextDirection);
        guideRayType = RAY_TYPE_REFRACTION;
        guideState |= WAVEFRONT_GUIDE_STATE_THROUGH_TRANSMISSION;
    }
}

inline void TracePrimaryGuideForPixel(uint2 pixel,
                                      uint2 dimensions,
                                      out float3 guideOrigin,
                                      out float3 guideDirection,
                                      out RayPayload guidePayload,
                                      out uint guideState)
{
    TracePrimaryGuide(camPos, BuildPrimaryCenterDirection(pixel, dimensions),
                      guideOrigin, guideDirection, guidePayload, guideState);
}

#endif // RAYTRACING_PRIMARY_GUIDE_HLSLI
