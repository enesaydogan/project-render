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

        if (WavefrontDirectionalLightActive()) {
            WavefrontLightSample sunSample =
                WavefrontSampleDirectionalLight(1.0);
            float sunTarget = WavefrontEvaluateReservoirTarget(
                record, normal, hitPos, sunSample);
            update_reservoir(reservoir, 0xFFFFFFFFu, sunTarget, rng);
        }

        WavefrontLightSamplerContext lightSampler =
            WavefrontCreateLightSampler(hitPos);
        const uint numLights = lightSampler.availableLights;
        bool hasLocalCandidate = numLights > 0u;
        hasLocalCandidate =
            hasLocalCandidate ||
            (lightSampler.mode == WAVEFRONT_LIGHT_SAMPLER_REGIR);
        if (hasLocalCandidate) {
            // Multi-sample seed: with only 1 ReGIR sample per pixel per
            // frame, dense many-light scenes need the temporal+spatial
            // reuse to do all the variance reduction — and the temporal M
            // cap (~30) bottlenecks convergence. Drawing K candidates here
            // divides per-frame variance by ~K and lifts the load off the
            // temporal pass. Cost: K cheap light evaluations (no shadow
            // rays), so it's roughly free compared to the rest of the
            // frame work.
            const uint kLocalSeedSamples = 4u;
            uint lightIndex = 0xFFFFFFFFu;
            if (lightSampler.mode == WAVEFRONT_LIGHT_SAMPLER_REGIR) {
                [unroll]
                for (uint k = 0u; k < kLocalSeedSamples; ++k) {
                    float regirSampleWeight = 0.0;
                    lightIndex = ReGIR_SampleCandidateWeighted(
                        hitPos, rng, g_regirParams, regirSampleWeight);
                    if (lightIndex == 0xFFFFFFFFu && numLights > 0u) {
                        lightIndex = next_uint(rng) % numLights;
                        regirSampleWeight = (float)numLights;
                    }
                    if (WavefrontIsEmissiveProxyLightIndex(lightIndex) ||
                        lightIndex < numLights) {
                        WavefrontLightSample localSample;
                        if (WavefrontIsEmissiveProxyLightIndex(lightIndex)) {
                            localSample = WavefrontSampleEmissiveProxyLight(
                                hitPos, lightIndex,
                                max(regirSampleWeight, 1.0), rng);
                        } else {
                            localSample = WavefrontSampleFlatLight(
                                hitPos, lightIndex,
                                max(regirSampleWeight, 1.0), rng);
                        }
                        float localTarget = WavefrontEvaluateReservoirTarget(
                            record, normal, hitPos, localSample);
                        update_reservoir(reservoir, lightIndex, localTarget,
                                         rng);
                    }
                }
            } else {
                [unroll]
                for (uint k = 0u; k < kLocalSeedSamples; ++k) {
                    lightIndex = next_uint(rng) % numLights;
                    WavefrontLightSample localSample =
                        WavefrontSampleFlatLight(hitPos, lightIndex,
                                                 (float)numLights, rng);
                    float localTarget = WavefrontEvaluateReservoirTarget(
                        record, normal, hitPos, localSample);
                    update_reservoir(reservoir, lightIndex, localTarget, rng);
                }
            }
        }

        WavefrontLightSample finalSample;
        finalSample.direction = float3(0.0, 1.0, 0.0);
        finalSample.maxDistance = 0.0;
        finalSample.radiance = float3(0.0, 0.0, 0.0);
        finalSample.packedLightIndex =
            WavefrontPackLightSampleMetadata(WAVEFRONT_LIGHT_SAMPLE_DIRECTIONAL, 0u);
        if (reservoir.lightIndex == 0xFFFFFFFFu &&
            WavefrontDirectionalLightActive()) {
            finalSample = WavefrontSampleDirectionalLight(1.0);
        } else if (WavefrontIsEmissiveProxyLightIndex(reservoir.lightIndex)) {
            finalSample = WavefrontSampleEmissiveProxyLight(
                hitPos, reservoir.lightIndex, 1.0, rng);
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
