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
    const uint rayType = state.packedState & 0xFFu;

    RayDesc ray;
    ray.Origin = state.origin;
    ray.Direction = normalize(state.direction);
    ray.TMin = 0.002;
    ray.TMax = 10000.0;

    RayPayload payload = InitWavefrontPayload(rayType);
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
        uint previousValue = 0u;
        record.packedState |= WAVEFRONT_HIT_STATE_MISS;
        InterlockedAdd(g_wavefrontQueueCounters[3], 1u, previousValue);
        InterlockedAdd(g_wavefrontStats[7], 1u, previousValue);
    } else {
        uint previousValue = 0u;
        InterlockedAdd(g_wavefrontQueueCounters[2], 1u, previousValue);
        InterlockedAdd(g_wavefrontStats[6], 1u, previousValue);
    }

    g_wavefrontHitQueue[pathIndex] = record;
}