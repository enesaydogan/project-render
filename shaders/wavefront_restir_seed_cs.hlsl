#include "raytracing/common.hlsli"
#include "raytracing/wavefront_transport.hlsli"
#include "restir_lib.hlsl"

cbuffer WavefrontRestirSeedConstants : register(b1)
{
    uint outputWidth;
    uint outputHeight;
    uint activeCount;
    uint reservedFlags;
};

inline uint2 WavefrontRestirPixelCoord(uint pixelIndex)
{
    return uint2(pixelIndex % outputWidth, pixelIndex / outputWidth);
}

inline bool WavefrontRestirReservoirFlip()
{
    return (((uint)globalFrameCount) & 1u) == 1u;
}

inline void StoreWavefrontDiReservoir(uint2 pixel, Reservoir reservoir)
{
    float4 packedReservoir = pack_reservoir(reservoir);
    if (WavefrontRestirReservoirFlip()) {
        g_reservoir1[pixel] = packedReservoir;
    } else {
        g_reservoir0[pixel] = packedReservoir;
    }
}

inline float WavefrontEvaluateReservoirTarget(WavefrontHitRecord record,
                                              float3 worldNormal,
                                              float3 hitPos,
                                              WavefrontLightSample lightSample)
{
    float3 directWeight = ComputeWavefrontDirectLightingWeight(
        record, worldNormal, hitPos, lightSample.direction);
    return length(max(lightSample.radiance * directWeight, 0.0));
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    const uint dispatchIndex = dispatchThreadID.x;
    uint pathIndex = dispatchIndex;
    const uint queueActiveCount =
        min(activeCount, g_wavefrontQueueCounters[WAVEFRONT_QUEUE_PATH_A]);
    const bool useMaterialBinList =
        (reservedFlags & WAVEFRONT_QUEUE_FLAG_USE_MATERIAL_BIN_LIST) != 0u;
    const bool missOnly =
        (reservedFlags & WAVEFRONT_QUEUE_FLAG_MISS_ONLY) != 0u;
    if (useMaterialBinList) {
        const uint materialBin = WavefrontGetMaterialBinFromQueueFlags(
            reservedFlags);
        uint binIndexCapacity = 0u;
        uint binIndexStride = 0u;
        g_wavefrontMaterialBinIndices.GetDimensions(binIndexCapacity,
                                                    binIndexStride);
        const uint perBinCapacity =
            binIndexCapacity / WAVEFRONT_MATERIAL_BIN_COUNT;
        if (materialBin >= WAVEFRONT_MATERIAL_BIN_COUNT ||
            dispatchIndex >= perBinCapacity ||
            dispatchIndex >=
                g_wavefrontQueueCounters[
                    WAVEFRONT_MATERIAL_BIN_COUNTER_BASE + materialBin]) {
            return;
        }
        pathIndex =
            g_wavefrontMaterialBinIndices[materialBin * perBinCapacity +
                                          dispatchIndex];
    }
    if (pathIndex >= queueActiveCount) {
        return;
    }

    WavefrontHitRecord record = g_wavefrontHitQueue[pathIndex];
    WavefrontPathState state = g_wavefrontPathQueueA[pathIndex];
    uint2 pixel = WavefrontRestirPixelCoord(record.pixelIndex);
    if (pixel.x >= outputWidth || pixel.y >= outputHeight) {
        return;
    }

    const bool isMiss = WavefrontHitRecordIsMiss(record);
    if (missOnly && !isMiss) {
        return;
    }
    if (useMaterialBinList && isMiss) {
        return;
    }

    Reservoir reservoir = init_reservoir();
    if (!isMiss) {
        float3 rayDir = normalize(state.direction);
        float3 hitPos = state.origin + rayDir * record.hitT;
        float3 normal = UnpackNormalOctahedron(record.packedNormal);
        RNG rng;
        rng.state = state.rngState ^
                    (record.pixelIndex * 0x6C8E9CF5u) ^ 0xA24BAED5u;

        WavefrontLightSample sunSample =
            WavefrontSampleDirectionalLight(1.0);
        float sunTarget = WavefrontEvaluateReservoirTarget(
            record, normal, hitPos, sunSample);
        update_reservoir(reservoir, 0xFFFFFFFFu, sunTarget, rng);

        WavefrontLightSamplerContext lightSampler =
            WavefrontCreateLightSampler(hitPos);
        const uint numLights = lightSampler.availableLights;
        bool hasLocalCandidate = numLights > 0u;
#ifdef REGIR_ENABLED
        hasLocalCandidate =
            hasLocalCandidate ||
            (lightSampler.mode == WAVEFRONT_LIGHT_SAMPLER_REGIR);
#endif
        if (hasLocalCandidate) {
            uint lightIndex = 0xFFFFFFFFu;
#ifdef REGIR_ENABLED
            if (lightSampler.mode == WAVEFRONT_LIGHT_SAMPLER_REGIR) {
                lightIndex = ReGIR_SampleCandidate(hitPos, rng, g_regirParams);
                if (lightIndex == 0xFFFFFFFFu && numLights > 0u)
                    lightIndex = next_uint(rng) % numLights;
            } else {
                lightIndex = next_uint(rng) % numLights;
            }
#else
            lightIndex = next_uint(rng) % numLights;
#endif
            if (lightIndex != 0xFFFFFFFFu) {
#ifdef REGIR_ENABLED
                WavefrontLightSample localSample =
                    WavefrontIsEmissiveProxyLightIndex(lightIndex)
                        ? WavefrontSampleEmissiveProxyLight(hitPos, lightIndex, 1.0)
                        : WavefrontSampleFlatLight(hitPos, lightIndex,
                                                   (float)numLights, rng);
#else
                WavefrontLightSample localSample =
                    WavefrontSampleFlatLight(hitPos, lightIndex,
                                             (float)numLights, rng);
#endif
                float localTarget = WavefrontEvaluateReservoirTarget(
                    record, normal, hitPos, localSample);
                update_reservoir(reservoir, lightIndex, localTarget, rng);
            }
        }

        WavefrontLightSample finalSample;
        finalSample.direction = float3(0.0, 1.0, 0.0);
        finalSample.maxDistance = 0.0;
        finalSample.radiance = float3(0.0, 0.0, 0.0);
        finalSample.packedLightIndex =
            WavefrontPackLightSampleMetadata(WAVEFRONT_LIGHT_SAMPLE_DIRECTIONAL, 0u);
        if (reservoir.lightIndex == 0xFFFFFFFFu) {
            finalSample = WavefrontSampleDirectionalLight(1.0);
#ifdef REGIR_ENABLED
        } else if (WavefrontIsEmissiveProxyLightIndex(reservoir.lightIndex)) {
            finalSample = WavefrontSampleEmissiveProxyLight(
                hitPos, reservoir.lightIndex, 1.0);
#endif
        } else if (reservoir.lightIndex < numLights) {
            finalSample = WavefrontSampleFlatLightUnweighted(
                hitPos, reservoir.lightIndex, rng);
        }
        float finalTarget = WavefrontEvaluateReservoirTarget(
            record, normal, hitPos, finalSample);
        finalize_reservoir(reservoir, finalTarget);
    }

    StoreWavefrontDiReservoir(pixel, reservoir);
}
