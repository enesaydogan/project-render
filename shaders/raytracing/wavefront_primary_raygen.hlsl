#include "common.hlsli"

static RayPayload InitWavefrontPayload(uint rayType)
{
    RayPayload payload;
    payload.t = -1.0;
    payload.packedColor1 = 0u;
    PayloadSetColor(payload, float3(0.0, 0.0, 0.0));
    payload.packedNormal = PackNormalOctahedron(float3(0.0, 1.0, 0.0));
    payload.packedAlbedo = PackPayloadAlbedo(float3(0.0, 0.0, 0.0));
    payload.packedSurface = PackPayloadSurface(1.0, 0.0, 0.0, 0.0);
    payload.packedIorType = PackPayloadIorType(1.0, rayType, false, 1.0);
    payload.packedTransmission = PackPayloadTransmissionColor(float3(1.0, 1.0, 1.0));
    payload.packedSpecular = PackPayloadSpecularColor(float3(1.0, 1.0, 1.0));
    return payload;
}

static float3 BuildWavefrontPrimaryCenterDirection(uint pixelIndex)
{
    uint width = 0u;
    uint height = 0u;
    g_output.GetDimensions(width, height);
    if (width == 0u || height == 0u) {
        return normalize(camForward);
    }

    uint2 pixel = uint2(pixelIndex % width, pixelIndex / width);
    float2 uv = (float2(pixel) + 0.5) / float2(width, height);
    float2 ndc = uv * 2.0 - 1.0;
    float fInv = tan(radians(fov) * 0.5);
    float3 forwardDir = normalize(camForward);
    float3 rightDir = normalize(cross(forwardDir, camUp));
    float3 upDir = normalize(cross(rightDir, forwardDir));
    float yView = (-ndc.y) * fInv;
    float xView = ndc.x * aspect * fInv;
    return normalize(xView * rightDir + yView * upDir + forwardDir);
}

static void ResolveWavefrontPrimaryGuide(uint pixelIndex,
                                         out float3 guideOrigin,
                                         out float3 guideDirection,
                                         out RayPayload guidePayload,
                                         out uint guideState)
{
    guideOrigin = camPos;
    guideDirection = BuildWavefrontPrimaryCenterDirection(pixelIndex);
    guidePayload = InitWavefrontPayload(RAY_TYPE_PRIMARY);
    guideState = 0u;

    uint guideRayType = RAY_TYPE_PRIMARY;
    const uint maxPrimaryDeltaSteps = 8u;
    [loop]
    for (uint deltaStep = 0u; deltaStep < maxPrimaryDeltaSteps; ++deltaStep) {
        RayDesc ray;
        ray.Origin = guideOrigin;
        ray.Direction = guideDirection;
        ray.TMin = 0.002;
        ray.TMax = 10000.0;

        RayPayload payload = InitWavefrontPayload(guideRayType);
        TraceRay(g_accel, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
        SHADER_COUNTER_ADD(SHADER_COUNTER_TRACE_RAYS, 1);

        if (payload.t < 0.0) {
            guidePayload = payload;
            guideState |= WAVEFRONT_GUIDE_STATE_MISS;
            return;
        }

        const float4 surface = UnpackPayloadSurface(payload.packedSurface);
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

[shader("raygeneration")]
void WavefrontPrimaryRayGen()
{
    const uint pathIndex = DispatchRaysIndex().x;
    const uint activeCount = g_wavefrontDispatchArgs[0].activeCount;
    if (pathIndex >= activeCount) {
        return;
    }

    if (pathIndex == 0u) {
        g_wavefrontQueueCounters[1] = activeCount;
    }

    WavefrontPathState state = g_wavefrontPathQueueA[pathIndex];
    uint currentRayType = state.packedState & 0xFFu;
    float3 traceOrigin = state.origin;
    float3 traceDirection = normalize(state.direction);
    float3 throughput = max(state.throughput, 0.0);
    const uint maxPrimaryDeltaSteps = 8u;
    const uint maxRefractiveBounceCount =
        (maxRefractiveBounces > 0.0) ? (uint)maxRefractiveBounces : 0u;

    float3 guideOrigin = camPos;
    float3 guideDirection = BuildWavefrontPrimaryCenterDirection(state.pixelIndex);
    RayPayload guidePayload = InitWavefrontPayload(RAY_TYPE_PRIMARY);
    uint guideState = 0u;
    ResolveWavefrontPrimaryGuide(state.pixelIndex, guideOrigin, guideDirection,
                                 guidePayload, guideState);

    RayPayload payload = InitWavefrontPayload(currentRayType);
    [loop]
    for (uint deltaStep = 0u; deltaStep < maxPrimaryDeltaSteps; ++deltaStep) {
        RayDesc ray;
        ray.Origin = traceOrigin;
        ray.Direction = traceDirection;
        ray.TMin = 0.002;
        ray.TMax = 10000.0;

        payload = InitWavefrontPayload(currentRayType);
        TraceRay(g_accel, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
        SHADER_COUNTER_ADD(SHADER_COUNTER_TRACE_RAYS, 1);

        if (payload.t < 0.0) {
            break;
        }

        const float4 surface = UnpackPayloadSurface(payload.packedSurface);
        const float roughness = max(0.001, surface.x);
        const float transmission = surface.z;
        const float ior = UnpackPayloadIor(payload.packedIorType);
        if (!ShouldResolveDeltaTransmission(roughness, transmission, ior)) {
            break;
        }

        if (WavefrontGetRefractiveBounceCount(state.packedState) >=
            maxRefractiveBounceCount) {
            break;
        }

        float3 resolvePos = traceOrigin + traceDirection * payload.t;
        float3 resolveNextDir = traceDirection;
        const bool thinWalled = UnpackPayloadThinWalled(payload.packedIorType);
        const float thickness = UnpackPayloadThickness(payload.packedSpecular);
        throughput *= max(UnpackPayloadTransmissionColor(payload.packedTransmission),
                          float3(0.0, 0.0, 0.0)) * transmission;

        if (!thinWalled) {
            const float3 resolveNormal =
                UnpackNormalOctahedron(payload.packedNormal);
            const float3 resolveView = -traceDirection;
            if (!RefractDeterministic(resolveView, resolveNormal, ior,
                                      resolveNextDir)) {
                break;
            }

            if (currentRayType != RAY_TYPE_REFRACTION) {
                const float travel = EffectiveArchGlassThickness(thickness) /
                                     max(abs(dot(resolveNextDir, resolveNormal)),
                                         0.2);
                resolvePos = resolvePos + resolveNextDir * min(travel, 2.0);
                resolveNextDir = traceDirection;
            }
        }

        traceOrigin = resolvePos + resolveNextDir * 0.002;
        traceDirection = normalize(resolveNextDir);
        currentRayType = RAY_TYPE_REFRACTION;
        state.packedState = WavefrontAdvancePackedState(state.packedState,
                                                        RAY_TYPE_REFRACTION);
    }

    state.origin = traceOrigin;
    state.direction = traceDirection;
    state.throughput = throughput;
    g_wavefrontPathQueueA[pathIndex] = state;

    WavefrontHitRecord record;
    record.hitT = payload.t;
    record.pixelIndex = state.pixelIndex;
    record.packedColor0 = payload.packedColor0;
    record.packedColor1 = payload.packedColor1;
    record.packedNormal = payload.packedNormal;
    record.packedAlbedo = payload.packedAlbedo;
    record.packedSurface = payload.packedSurface;
    record.packedIorType = payload.packedIorType;
    record.packedTransmission = payload.packedTransmission;
    record.packedSpecular = payload.packedSpecular;
    record.packedState = state.packedState;
    record.reserved = WavefrontPackMaterialSortKey(
        currentRayType,
        WavefrontClassifyMaterialBin(payload.packedSurface,
                                     payload.packedIorType,
                                     payload.packedColor0,
                                     payload.packedColor1),
        0u);
    record.guideOrigin = guideOrigin;
    record.guidePackedState = guideState;
    record.guideDirection = guideDirection;
    record.guideHitT = guidePayload.t;
    record.guidePackedNormal = guidePayload.packedNormal;
    record.guidePackedAlbedo = guidePayload.packedAlbedo;
    record.guidePackedSurface = guidePayload.packedSurface;
    record.guidePackedIorType = guidePayload.packedIorType;
    record.guidePackedTransmission = guidePayload.packedTransmission;
    record.guidePackedSpecular = guidePayload.packedSpecular;
    record.guideReserved0 = 0u;
    record.guideReserved1 = 0u;

    if (payload.t < 0.0) {
        if (dlssRayReconstruction > 0.5 &&
            (guideState & WAVEFRONT_GUIDE_STATE_MISS) != 0u) {
            record.packedColor0 = guidePayload.packedColor0;
            record.packedColor1 = guidePayload.packedColor1;
        }
        uint previousValue = 0u;
        record.packedState |= WAVEFRONT_HIT_STATE_MISS;
        InterlockedAdd(g_wavefrontQueueCounters[3], 1u, previousValue);
        InterlockedAdd(g_wavefrontStats[7], 1u, previousValue);
    } else {
        uint previousValue = 0u;
        InterlockedAdd(g_wavefrontQueueCounters[2], 1u, previousValue);
        InterlockedAdd(g_wavefrontStats[6], 1u, previousValue);
        WavefrontCompactMaterialBinIndex(
            WAVEFRONT_PRIMARY_MATERIAL_BIN_STATS_BASE, record.reserved,
            pathIndex);
    }

    g_wavefrontHitQueue[pathIndex] = record;
}
