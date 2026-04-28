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

    if (payload.t < 0.0) {
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
