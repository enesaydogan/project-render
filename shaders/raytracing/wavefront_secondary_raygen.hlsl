#include "common.hlsli"

static RayPayload InitWavefrontSecondaryPayload(uint rayType)
{
    RayPayload payload;
    payload.t = -1.0;
    payload.packedColor1 = 0u;
    PayloadSetColor(payload, float3(0.0, 0.0, 0.0));
    payload.packedNormal = PackNormalOctahedron(float3(0.0, 1.0, 0.0));
    payload.packedAlbedo = PackPayloadAlbedo(float3(0.0, 0.0, 0.0));
    payload.surface = MakePayloadSurface(1.0, 0.0, 0.0, 0.0);
    payload.packedIorType = PackPayloadIorType(1.0, rayType, false, 1.0);
    payload.packedTransmission = PackPayloadTransmissionColor(float3(1.0, 1.0, 1.0));
    payload.packedSpecular = PackPayloadSpecularColor(float3(1.0, 1.0, 1.0));
    payload.packedParallaxSelfShadow = PackWavefrontParallaxSelfShadow(1.0);
    return payload;
}

[shader("raygeneration")]
void WavefrontSecondaryRayGen()
{
    const uint pathIndex = DispatchRaysIndex().x;
    const uint queueFlags =
        g_wavefrontReserved[WAVEFRONT_RESERVED_SECONDARY_DISPATCH_CONFIG_INDEX].w;
    const uint activeCount =
        ((queueFlags & WAVEFRONT_QUEUE_FLAG_SOURCE_IS_A) != 0u)
            ? g_wavefrontQueueCounters[WAVEFRONT_QUEUE_PATH_A]
            : g_wavefrontQueueCounters[WAVEFRONT_QUEUE_PATH_B];
    if (pathIndex >= activeCount) {
        return;
    }

    uint queueCapacity, dummy;
    if ((queueFlags & WAVEFRONT_QUEUE_FLAG_SOURCE_IS_A) != 0u) {
        g_wavefrontPathQueueA.GetDimensions(queueCapacity, dummy);
    } else {
        g_wavefrontPathQueueB.GetDimensions(queueCapacity, dummy);
    }
    if (pathIndex >= queueCapacity) {
        return;
    }

    if (pathIndex == 0u) {
        uint previousValue = 0u;
        InterlockedAdd(g_wavefrontStats[23], min(activeCount, queueCapacity),
                       previousValue);
    }

    WavefrontPathState state;
    if ((queueFlags & WAVEFRONT_QUEUE_FLAG_SOURCE_IS_A) != 0u) {
        state = g_wavefrontPathQueueA[pathIndex];
    } else {
        state = g_wavefrontPathQueueB[pathIndex];
    }
    const uint rayType = WavefrontGetPathRayType(state.packedState);
    const bool wantsDiffuse =
        (queueFlags & WAVEFRONT_QUEUE_FLAG_FILTER_DIFFUSE) != 0u;
    const bool wantsSpecular =
        (queueFlags & WAVEFRONT_QUEUE_FLAG_FILTER_SPECULAR) != 0u;
    if (wantsDiffuse && rayType != RAY_TYPE_DIFFUSE) {
        return;
    }
    if (wantsSpecular &&
        rayType != RAY_TYPE_REFLECTION && rayType != RAY_TYPE_REFRACTION) {
        return;
    }

    // Aggregate per-ray-type lane stats per wave instead of one global
    // atomic per ray.
    const bool isDiffuseLane = (rayType == RAY_TYPE_DIFFUSE);
    const bool isSpecularLane =
        (rayType == RAY_TYPE_REFLECTION || rayType == RAY_TYPE_REFRACTION);
    const uint waveDiffuseCount = WaveActiveCountBits(isDiffuseLane);
    const uint waveSpecularCount = WaveActiveCountBits(isSpecularLane);
    if (WaveIsFirstLane()) {
        uint lanePreviousValue = 0u;
        if (waveDiffuseCount != 0u) {
            InterlockedAdd(g_wavefrontStats[47], waveDiffuseCount,
                           lanePreviousValue);
        }
        if (waveSpecularCount != 0u) {
            InterlockedAdd(g_wavefrontStats[48], waveSpecularCount,
                           lanePreviousValue);
        }
    }

    const float3 traceDirection = normalize(state.direction);
    RayDesc ray;
    ray.Origin = state.origin;
    ray.Direction = traceDirection;
    ray.TMin = 0.002;
    ray.TMax = 10000.0;

    // Legacy deterministic glass reflections trace with GI_EVAL payload
    // semantics so miss rays carry the full reflected environment.  Keep the
    // queued path state as REFLECTION for wavefront scheduling and bounce
    // accounting; only the traced payload type changes the hit/miss shading
    // contract.
    const uint payloadRayType =
        (rayType == RAY_TYPE_REFLECTION) ? RAY_TYPE_GI_EVAL : rayType;
    RayPayload payload = InitWavefrontSecondaryPayload(payloadRayType);
    TraceRay(g_accel, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
    SHADER_COUNTER_ADD(SHADER_COUNTER_TRACE_RAYS, 1);

    WavefrontHitRecord record;
    record.hitT = payload.t;
    record.pixelIndex = state.pixelIndex;
    record.packedColor0 = payload.packedColor0;
    record.packedColor1 = payload.packedColor1;
    record.packedNormal = payload.packedNormal;
    record.packedAlbedo = payload.packedAlbedo;
    record.packedIorType = payload.packedIorType;
    record.packedTransmission = payload.packedTransmission;
    record.packedSpecular = payload.packedSpecular;
    record.packedState = state.packedState;
    const float4 payloadSurface = payload.surface;
    record.reserved = WavefrontApplyParallaxSelfShadowToSortKey(
        WavefrontPackMaterialSortKey(
            rayType,
            WavefrontClassifyMaterialBinFromSurface(payloadSurface,
                                                    payload.packedIorType,
                                                    payload.packedColor0,
                                                    payload.packedColor1),
            0u),
        payload.packedParallaxSelfShadow / 255.0);
    record.surface = payloadSurface;
    record.guideOrigin = state.origin;
    record.guidePackedState =
        (payload.t < 0.0) ? WAVEFRONT_GUIDE_STATE_MISS : 0u;
    record.guideDirection = traceDirection;
    record.guideHitT = payload.t;
    record.guidePackedNormal = payload.packedNormal;
    record.guidePackedAlbedo = payload.packedAlbedo;
    record.guidePackedIorType = payload.packedIorType;
    record.guidePackedTransmission = payload.packedTransmission;
    record.guidePackedSpecular = payload.packedSpecular;
    record.guideSurface = payloadSurface;
    const bool isMiss = (payload.t < 0.0);
    if (isMiss) {
        record.packedState |= WAVEFRONT_HIT_STATE_MISS;
    }

    // Aggregate hit/miss stats per wave instead of one global atomic per ray.
    const uint waveMissCount = WaveActiveCountBits(isMiss);
    const uint waveHitCount = WaveActiveCountBits(!isMiss);
    if (WaveIsFirstLane()) {
        uint previousValue = 0u;
        if (waveMissCount != 0u) {
            InterlockedAdd(g_wavefrontStats[25], waveMissCount, previousValue);
        }
        if (waveHitCount != 0u) {
            InterlockedAdd(g_wavefrontStats[24], waveHitCount, previousValue);
        }
    }

    if (!isMiss) {
        WavefrontCompactMaterialBinIndex(
            WAVEFRONT_SECONDARY_MATERIAL_BIN_STATS_BASE, record.reserved,
            pathIndex);
    }

    g_wavefrontHitQueue[pathIndex] = record;
}
