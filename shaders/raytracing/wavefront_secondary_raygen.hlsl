#include "common.hlsli"

static RayPayload InitWavefrontSecondaryPayload(uint rayType)
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
void WavefrontSecondaryRayGen()
{
    const uint pathIndex = DispatchRaysIndex().x;
    const uint queueFlags =
        g_wavefrontReserved[WAVEFRONT_RESERVED_SECONDARY_DISPATCH_CONFIG_INDEX].w;
    const uint activeCount =
        ((queueFlags & WAVEFRONT_QUEUE_FLAG_SOURCE_IS_A) != 0u)
            ? g_wavefrontQueueCounters[0]
            : g_wavefrontQueueCounters[4];
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

    uint lanePreviousValue = 0u;
    if (rayType == RAY_TYPE_DIFFUSE) {
        InterlockedAdd(g_wavefrontStats[47], 1u, lanePreviousValue);
    } else if (rayType == RAY_TYPE_REFLECTION || rayType == RAY_TYPE_REFRACTION) {
        InterlockedAdd(g_wavefrontStats[48], 1u, lanePreviousValue);
    }

    RayDesc ray;
    ray.Origin = state.origin;
    ray.Direction = normalize(state.direction);
    ray.TMin = 0.002;
    ray.TMax = 10000.0;

    RayPayload payload = InitWavefrontSecondaryPayload(rayType);
    TraceRay(g_accel, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
    SHADER_COUNTER_ADD(SHADER_COUNTER_TRACE_RAYS, 1);

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
        rayType,
        WavefrontClassifyMaterialBin(payload.packedSurface,
                                     payload.packedIorType,
                                     payload.packedColor0,
                                     payload.packedColor1),
        0u);

    if (payload.t < 0.0) {
        record.packedState |= WAVEFRONT_HIT_STATE_MISS;
        uint previousValue = 0u;
        InterlockedAdd(g_wavefrontStats[25], 1u, previousValue);
    } else {
        uint previousValue = 0u;
        InterlockedAdd(g_wavefrontStats[24], 1u, previousValue);
        WavefrontCompactMaterialBinIndex(
            WAVEFRONT_SECONDARY_MATERIAL_BIN_STATS_BASE, record.reserved,
            pathIndex);
    }

    g_wavefrontHitQueue[pathIndex] = record;
}
