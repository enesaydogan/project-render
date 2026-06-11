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

    // IMPORTANT: pixelIndex was packed by the bootstrap as
    //   pixelIndex = y * currentRenderWidth + x
    // where currentRenderWidth is the per-frame dispatch width (the value
    // the bootstrap CB calls `outputWidth`). With DRR enabled the
    // render-resolution textures are allocated at the mode's max-render
    // dims, so g_output.GetDimensions() returns *max*-render dims, not the
    // current-frame value. Using that to decode pixelIndex shifts every
    // pixel to the wrong row and produces horizontal streak corruption.
    // The bootstrap stashes the actual current width/height in
    // g_wavefrontStats[0/1] for exactly this reason.
    const uint width = max(g_wavefrontStats[0], 1u);
    const uint height = max(g_wavefrontStats[1], 1u);
    uint2 pixel = uint2(state.pixelIndex % width,
                        state.pixelIndex / width);
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
    record.packedIorType = payload.packedIorType;
    record.packedTransmission = payload.packedTransmission;
    record.packedSpecular = payload.packedSpecular;
    record.packedState = state.packedState;
    const float4 payloadSurface = UnpackPayloadSurface(payload.packedSurface);
    record.reserved = WavefrontApplyParallaxSelfShadowToSortKey(
        WavefrontPackMaterialSortKey(
            currentRayType,
            WavefrontClassifyMaterialBinFromSurface(payloadSurface,
                                                    payload.packedIorType,
                                                    payload.packedColor0,
                                                    payload.packedColor1),
            0u),
        payload.packedParallaxSelfShadow / 255.0);
    record.surface = payloadSurface;

    // Guide data lives in its own queue and is only consumed by the primary
    // resolve's RR block; skip the write entirely when nothing will read it.
    if (needsPrimaryGuide || dlssRayReconstruction > 0.5) {
        WavefrontGuideRecord guideRecord;
        guideRecord.guideOrigin = guideOrigin;
        guideRecord.guidePackedState = guideState;
        guideRecord.guideDirection = guideDirection;
        guideRecord.guideHitT = guidePayload.t;
        guideRecord.guidePackedNormal = guidePayload.packedNormal;
        guideRecord.guidePackedAlbedo = guidePayload.packedAlbedo;
        guideRecord.guidePackedIorType = guidePayload.packedIorType;
        guideRecord.guidePackedTransmission = guidePayload.packedTransmission;
        guideRecord.guidePackedSpecular = guidePayload.packedSpecular;
        guideRecord.guideSurface = UnpackPayloadSurface(guidePayload.packedSurface);
        g_wavefrontGuideQueue[pathIndex] = guideRecord;
    }

    const bool isMiss = (payload.t < 0.0);
    if (isMiss) {
        if (dlssRayReconstruction > 0.5 &&
            (guideState & WAVEFRONT_GUIDE_STATE_MISS) != 0u) {
            record.packedColor0 = guidePayload.packedColor0;
            record.packedColor1 = guidePayload.packedColor1;
        }
        record.packedState |= WAVEFRONT_HIT_STATE_MISS;
    }

    // Aggregate hit/miss counters per wave: one atomic per wave instead of
    // two per ray.
    const uint waveMissCount = WaveActiveCountBits(isMiss);
    const uint waveHitCount = WaveActiveCountBits(!isMiss);
    if (WaveIsFirstLane()) {
        uint previousValue = 0u;
        if (waveMissCount != 0u) {
            InterlockedAdd(
                g_wavefrontQueueCounters[WAVEFRONT_QUEUE_PRIMARY_MISS],
                waveMissCount, previousValue);
            InterlockedAdd(g_wavefrontStats[7], waveMissCount, previousValue);
        }
        if (waveHitCount != 0u) {
            InterlockedAdd(
                g_wavefrontQueueCounters[WAVEFRONT_QUEUE_PRIMARY_HIT],
                waveHitCount, previousValue);
            InterlockedAdd(g_wavefrontStats[6], waveHitCount, previousValue);
        }
    }

    if (!isMiss) {
        WavefrontCompactMaterialBinIndex(
            WAVEFRONT_PRIMARY_MATERIAL_BIN_STATS_BASE, record.reserved,
            pathIndex);
    }

    g_wavefrontHitQueue[pathIndex] = record;
}
