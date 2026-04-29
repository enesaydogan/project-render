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

RWTexture2D<float4> g_reservoir0 : register(u2);
RWTexture2D<float4> g_reservoir1 : register(u3);

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
    if (pathIndex >= activeCount) {
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
        rng.state = state.rngState ^ (pathIndex * 0x6C8E9CF5u) ^ 0xA24BAED5u;

        WavefrontLightSample sunSample =
            WavefrontSampleDirectionalLight(1.0);
        float sunTarget = WavefrontEvaluateReservoirTarget(
            record, normal, hitPos, sunSample);
        update_reservoir(reservoir, 0xFFFFFFFFu, sunTarget, rng);

        WavefrontLightSamplerContext lightSampler =
            WavefrontCreateLightSampler(hitPos);
        const uint numLights = lightSampler.availableLights;
        if (numLights > 0u) {
            const uint lightIndex = next_uint(rng) % numLights;
            WavefrontLightSample localSample =
                WavefrontSampleFlatLight(hitPos, lightIndex,
                                         (float)numLights);
            float localTarget = WavefrontEvaluateReservoirTarget(
                record, normal, hitPos, localSample);
            update_reservoir(reservoir, lightIndex, localTarget, rng);
        }

        WavefrontLightSample finalSample;
        finalSample.direction = float3(0.0, 1.0, 0.0);
        finalSample.maxDistance = 0.0;
        finalSample.radiance = float3(0.0, 0.0, 0.0);
        finalSample.packedLightIndex =
            WavefrontPackLightSampleMetadata(WAVEFRONT_LIGHT_SAMPLE_DIRECTIONAL, 0u);
        if (reservoir.lightIndex == 0xFFFFFFFFu) {
            finalSample = WavefrontSampleDirectionalLight(1.0);
        } else if (reservoir.lightIndex < numLights) {
            finalSample = WavefrontSampleFlatLight(hitPos,
                                                   reservoir.lightIndex,
                                                   1.0);
        }
        float finalTarget = WavefrontEvaluateReservoirTarget(
            record, normal, hitPos, finalSample);
        finalize_reservoir(reservoir, finalTarget);
    }

    StoreWavefrontDiReservoir(pixel, reservoir);
}
