#include "common.hlsli"
#include "primary_guide.hlsli"

[shader("raygeneration")]
void WavefrontPrimaryRayGen()
{
    const uint pathIndex = DispatchRaysIndex().x;
    const uint activeCount = g_wavefrontDispatchArgs[0].activeCount;
    if (pathIndex >= activeCount) {
        return;
    }

    if (pathIndex == 0u) {
        g_wavefrontQueueCounters[WAVEFRONT_QUEUE_PRIMARY_ACTIVE] = activeCount;
    }

    WavefrontPathState state = g_wavefrontPathQueueA[pathIndex];
    uint currentRayType = state.packedState & 0xFFu;
    float3 traceOrigin = state.origin;
    float3 traceDirection = normalize(state.direction);

    uint width = 0u;
    uint height = 0u;
    g_output.GetDimensions(width, height);
    uint2 pixel = uint2(state.pixelIndex % max(width, 1u),
                        state.pixelIndex / max(width, 1u));
    float3 guideOrigin = camPos;
    float3 guideDirection = BuildPrimaryCenterDirection(pixel,
                                                        uint2(width, height));
    RayPayload guidePayload = InitRayPayload(RAY_TYPE_PRIMARY);
    uint guideState = 0u;
    const bool needsPrimaryGuide =
        DxrFeatureEnabled(DXR_FEATURE_PRIMARY_GUIDE);

    RayDesc ray;
    ray.Origin = traceOrigin;
    ray.Direction = traceDirection;
    ray.TMin = 0.002;
    ray.TMax = 10000.0;

    RayPayload payload = InitRayPayload(currentRayType);
    TraceRay(g_accel, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
    SHADER_COUNTER_ADD(SHADER_COUNTER_TRACE_RAYS, 1);

    if (needsPrimaryGuide) {
        TracePrimaryGuideForPixel(pixel, uint2(width, height), guideOrigin,
                                  guideDirection, guidePayload, guideState);
    } else {
        guideOrigin = traceOrigin;
        guideDirection = traceDirection;
        guidePayload = payload;
        guideState = (payload.t < 0.0) ? WAVEFRONT_GUIDE_STATE_MISS : 0u;
        if (currentRayType == RAY_TYPE_REFRACTION) {
            guideState |= WAVEFRONT_GUIDE_STATE_THROUGH_TRANSMISSION;
        }
    }

    state.origin = traceOrigin;
    state.direction = traceDirection;
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
        WavefrontClassifyMaterialBinFromSurface(payload.surface,
                                                payload.packedIorType,
                                                payload.packedColor0,
                                                payload.packedColor1),
        0u);
    record.surface = payload.surface;
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
    record.guideSurface = guidePayload.surface;

    if (payload.t < 0.0) {
        if (dlssRayReconstruction > 0.5 &&
            (guideState & WAVEFRONT_GUIDE_STATE_MISS) != 0u) {
            record.packedColor0 = guidePayload.packedColor0;
            record.packedColor1 = guidePayload.packedColor1;
        }
        uint previousValue = 0u;
        record.packedState |= WAVEFRONT_HIT_STATE_MISS;
        InterlockedAdd(g_wavefrontQueueCounters[WAVEFRONT_QUEUE_PRIMARY_MISS],
                       1u, previousValue);
        InterlockedAdd(g_wavefrontStats[7], 1u, previousValue);
    } else {
        uint previousValue = 0u;
        InterlockedAdd(g_wavefrontQueueCounters[WAVEFRONT_QUEUE_PRIMARY_HIT],
                       1u, previousValue);
        InterlockedAdd(g_wavefrontStats[6], 1u, previousValue);
        WavefrontCompactMaterialBinIndex(
            WAVEFRONT_PRIMARY_MATERIAL_BIN_STATS_BASE, record.reserved,
            pathIndex);
    }

    g_wavefrontHitQueue[pathIndex] = record;
}
