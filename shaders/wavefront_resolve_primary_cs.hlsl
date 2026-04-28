#include "raytracing/common.hlsli"
#include "raytracing/wavefront_transport.hlsli"
#include "restir_lib.hlsl"

cbuffer WavefrontResolveConstants : register(b1)
{
    uint outputWidth;
    uint outputHeight;
    uint activeCount;
    uint reservedFlags;
};

static const float2 kInvalidMvec = float2(-1e6, -1e6);
static const uint kWavefrontSecondaryQueueCounter = 4u;
static const uint kWavefrontShadowQueueCounter = 5u;
static const float kWavefrontRayBias = 0.002f;

RWTexture2D<float4> g_reservoir0 : register(u2);
RWTexture2D<float4> g_reservoir1 : register(u3);
RWTexture2D<float4> g_gi_reservoir_a0 : register(u4);
RWTexture2D<float4> g_gi_reservoir_a1 : register(u5);
RWTexture2D<float4> g_gi_reservoir_a2 : register(u6);
RWTexture2D<float4> g_gi_reservoir_b0 : register(u7);
RWTexture2D<float4> g_gi_reservoir_b1 : register(u8);
RWTexture2D<float4> g_gi_reservoir_b2 : register(u9);

inline uint2 WavefrontPixelCoord(uint pixelIndex)
{
    return uint2(pixelIndex % outputWidth, pixelIndex / outputWidth);
}

inline bool WavefrontReservoirFlip()
{
    return (((uint)globalFrameCount) & 1u) == 1u;
}

inline void StoreWavefrontDiReservoir(uint2 pixel, Reservoir reservoir)
{
    float4 packedReservoir = pack_reservoir(reservoir);
    if (WavefrontReservoirFlip()) {
        g_reservoir1[pixel] = packedReservoir;
    } else {
        g_reservoir0[pixel] = packedReservoir;
    }
}

inline void ClearWavefrontGiReservoir(uint2 pixel)
{
    const float4 zero4 = float4(0.0, 0.0, 0.0, 0.0);
    if (WavefrontReservoirFlip()) {
        g_gi_reservoir_b0[pixel] = zero4;
        g_gi_reservoir_b1[pixel] = zero4;
        g_gi_reservoir_b2[pixel] = zero4;
    } else {
        g_gi_reservoir_a0[pixel] = zero4;
        g_gi_reservoir_a1[pixel] = zero4;
        g_gi_reservoir_a2[pixel] = zero4;
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

inline float2 ComputeWavefrontSkyMotion(float3 rayDir, float2 currScreen)
{
    float2 motion = float2(0.0, 0.0);
    if (prevValid > 0.5) {
        float3 forwardPrev = normalize(prevForward);
        float3 rightPrev = normalize(cross(forwardPrev, prevUp));
        float3 upPrev = normalize(cross(rightPrev, forwardPrev));
        float fInvPrev = tan(radians(prevFov) * 0.5);
        float vxPrev = dot(rayDir, rightPrev);
        float vyPrev = dot(rayDir, upPrev);
        float vzPrev = dot(rayDir, forwardPrev);
        if (vzPrev > 0.001) {
            float ndcXPrev = vxPrev / (vzPrev * prevAspect * fInvPrev);
            float ndcYPrev = -vyPrev / (vzPrev * fInvPrev);
            float2 prevScreen =
                (float2(ndcXPrev, ndcYPrev) * 0.5 + 0.5) * float2(outputWidth, outputHeight);
            float2 screenMin = float2(0.0, 0.0);
            float2 screenMax = float2(outputWidth, outputHeight);
            motion = (any(prevScreen < screenMin) || any(prevScreen > screenMax))
                         ? kInvalidMvec
                         : (prevScreen - currScreen);
        }
    }
    return motion;
}

inline float2 ComputeWavefrontSurfaceMotion(float3 hitPos, float2 currScreen)
{
    float2 motion = kInvalidMvec;
    if (prevValid > 0.5) {
        float3 forwardPrev = normalize(prevForward);
        float3 rightPrev = normalize(cross(forwardPrev, prevUp));
        float3 upPrev = normalize(cross(rightPrev, forwardPrev));
        float fInvPrev = tan(radians(prevFov) * 0.5);
        float3 relPrev = hitPos - prevPos;
        float vxPrev = dot(relPrev, rightPrev);
        float vyPrev = dot(relPrev, upPrev);
        float vzPrev = dot(relPrev, forwardPrev);
        if (vzPrev > 0.001) {
            float ndcXPrev = vxPrev / (vzPrev * prevAspect * fInvPrev);
            float ndcYPrev = -vyPrev / (vzPrev * fInvPrev);
            float2 prevScreen =
                (float2(ndcXPrev, ndcYPrev) * 0.5 + 0.5) * float2(outputWidth, outputHeight);
            float2 screenMin = float2(0.0, 0.0);
            float2 screenMax = float2(outputWidth, outputHeight);
            motion = (any(prevScreen < screenMin) || any(prevScreen > screenMax))
                         ? kInvalidMvec
                         : (prevScreen - currScreen);
        }
    }
    return motion;
}

inline void ReserveWavefrontQueueDimensions(out uint pathQueueCapacity,
                                            out uint pathQueueStride,
                                            out uint shadowQueueCapacity,
                                            out uint shadowQueueStride)
{
    g_wavefrontPathQueueB.GetDimensions(pathQueueCapacity, pathQueueStride);
    g_wavefrontShadowQueue.GetDimensions(shadowQueueCapacity, shadowQueueStride);
}

inline float3 SafeNormalize(float3 value, float3 fallback)
{
    float lenSq = dot(value, value);
    return (lenSq > 1.0e-8) ? value * rsqrt(lenSq) : fallback;
}

inline float3 BuildDiffuseContinuation(float3 normal, inout RNG rng)
{
    float3 localSample = sample_hemisphere_cosine(next_float2(rng));
    return SafeNormalize(align_to_normal(localSample, normal), normal);
}

inline float3 BuildSpecularContinuation(float3 rayDir, float3 normal)
{
    return SafeNormalize(reflect(rayDir, normal), normal);
}

inline float3 BuildTransmissionContinuation(float3 rayDir, float3 normal,
                                            float ior)
{
    float entering = dot(rayDir, normal) < 0.0 ? 1.0 : 0.0;
    float3 faceNormal = (entering > 0.5) ? normal : -normal;
    float eta = (entering > 0.5) ? rcp(max(ior, 1.0)) : max(ior, 1.0);
    float3 refracted = refract(rayDir, faceNormal, eta);
    if (dot(refracted, refracted) < 1.0e-8) {
        refracted = reflect(rayDir, faceNormal);
    }
    return SafeNormalize(refracted, -faceNormal);
}

inline void EmitWavefrontSecondaryPath(uint queueIndex,
                                       uint pixelIndex,
                                       float3 origin,
                                       float3 direction,
                                       uint rngState,
                                       float3 throughput,
                                       uint packedState)
{
    WavefrontPathState nextState;
    nextState.origin = origin;
    nextState.pixelIndex = pixelIndex;
    nextState.direction = direction;
    nextState.rngState = rngState;
    nextState.throughput = throughput;
    nextState.packedState = packedState;
    g_wavefrontPathQueueB[queueIndex] = nextState;
}

inline void EmitWavefrontShadowTask(uint queueIndex,
                                    float3 origin,
                                    float3 direction,
                                    float maxDistance,
                                    uint packedLightIndex,
                                    float3 throughput,
                                    uint pixelIndex)
{
    WavefrontShadowTask task;
    task.origin = origin;
    task.maxDistance = maxDistance;
    task.direction = direction;
    task.packedLightIndex = packedLightIndex;
    task.throughput = throughput;
    task.packedState = pixelIndex;
    g_wavefrontShadowQueue[queueIndex] = task;
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    const uint pathIndex = dispatchThreadID.x;
    if (pathIndex >= activeCount) {
        return;
    }

    WavefrontHitRecord record = g_wavefrontHitQueue[pathIndex];
    WavefrontPathState state = g_wavefrontPathQueueA[pathIndex];
    uint2 pixel = WavefrontPixelCoord(record.pixelIndex);
    if (pixel.x >= outputWidth || pixel.y >= outputHeight) {
        return;
    }

    if (pathIndex == 0u) {
        g_wavefrontStats[8] = activeCount;
    }

    float2 currScreen = float2(pixel) + 0.5;
    float3 rayDir = normalize(state.direction);
    bool isMiss = WavefrontHitRecordIsMiss(record);

    float3 color = WavefrontHitRecordGetColor(record);
    float depth = (dlssRayReconstruction > 0.5) ? farZ : 1.0;
    float linearDepth = farZ;
    float2 motion = ComputeWavefrontSkyMotion(rayDir, currScreen);
    float3 normal = float3(0.0, 1.0, 0.0);
    float3 albedo = float3(0.0, 0.0, 0.0);
    float3 specularAlbedo = float3(0.0, 0.0, 0.0);
    float roughness = 1.0;
    uint pathQueueCapacity = 0u;
    uint pathQueueStride = 0u;
    uint shadowQueueCapacity = 0u;
    uint shadowQueueStride = 0u;
    Reservoir diReservoir = init_reservoir();
    WavefrontLightSample selectedDirectLightSample =
        WavefrontSampleDirectionalLight(1.0);
    float selectedDirectLightWeight = 0.0;
    ReserveWavefrontQueueDimensions(pathQueueCapacity, pathQueueStride,
                                    shadowQueueCapacity, shadowQueueStride);

    if (!isMiss) {
        float3 hitPos = state.origin + rayDir * record.hitT;
        normal = UnpackNormalOctahedron(record.packedNormal);
        albedo = UnpackPayloadAlbedo(record.packedAlbedo);
        float4 surface = UnpackPayloadSurface(record.packedSurface);
        roughness = saturate(surface.x);
        float metallic = saturate(surface.y);
        float transmission = saturate(surface.z);
        float translucency = saturate(surface.w);
        float specularWeight = saturate(UnpackPayloadSpecularWeight(record.packedIorType));
        float ior = UnpackPayloadIor(record.packedIorType);
        float3 transmissionTint = UnpackPayloadTransmissionColor(record.packedTransmission);
        specularAlbedo = UnpackPayloadSpecularColor(record.packedSpecular) *
                         saturate(max(metallic, specularWeight) * (1.0 - 0.5 * transmission));
        motion = ComputeWavefrontSurfaceMotion(hitPos, currScreen);

        RNG rng;
        rng.state = state.rngState ^ (pathIndex * 0x9E3779B9u) ^ 0xB5297A4Du;
        const uint maxSpecularBounceCount =
            (maxSpecularBounces > 0.0) ? (uint)maxSpecularBounces : 0u;
        const uint maxRefractiveBounceCount =
            (maxRefractiveBounces > 0.0) ? (uint)maxRefractiveBounces : 0u;
        const uint maxDiffuseBounceCount =
            (maxGIBounces > 0.0) ? (uint)maxGIBounces : 0u;

        uint nextRayType = RAY_TYPE_DIFFUSE;
        float3 nextDirection = normal;
        float3 nextThroughput = state.throughput * max(albedo, 0.02.xxx);
        if (transmission > 0.05) {
            nextRayType = RAY_TYPE_REFRACTION;
            nextDirection = BuildTransmissionContinuation(rayDir, normal, ior);
            nextThroughput *= max(transmissionTint, 0.02.xxx) * max(transmission, 0.1);
        } else if (metallic > 0.45 || roughness < 0.3 || specularWeight > 0.55) {
            nextRayType = RAY_TYPE_REFLECTION;
            nextDirection = BuildSpecularContinuation(rayDir, normal);
            nextThroughput *= max(specularAlbedo, 0.04.xxx);
        } else {
            nextRayType = RAY_TYPE_DIFFUSE;
            nextDirection = BuildDiffuseContinuation(normal, rng);
            nextThroughput *= saturate(dot(normal, nextDirection));
        }
        nextThroughput = max(nextThroughput, 0.0);

        {
            WavefrontLightSample sunSample =
                WavefrontSampleDirectionalLight(1.0);
            float sunTarget = WavefrontEvaluateReservoirTarget(
                record, normal, hitPos, sunSample);
            update_reservoir(diReservoir, 0xFFFFFFFFu, sunTarget, rng);

            const uint numLights = (uint)lightCount;
            if (numLights > 0u) {
                const uint lightIndex = next_uint(rng) % numLights;
                WavefrontLightSample localSample =
                    WavefrontSampleFlatLight(hitPos, lightIndex,
                                             (float)numLights);
                float localTarget = WavefrontEvaluateReservoirTarget(
                    record, normal, hitPos, localSample);
                update_reservoir(diReservoir, lightIndex, localTarget, rng);
            }

            WavefrontLightSample finalSample;
            if (diReservoir.lightIndex == 0xFFFFFFFFu) {
                finalSample = WavefrontSampleDirectionalLight(1.0);
            } else {
                finalSample = WavefrontSampleFlatLight(hitPos,
                                                       diReservoir.lightIndex,
                                                       1.0);
            }
            float finalTarget = WavefrontEvaluateReservoirTarget(
                record, normal, hitPos, finalSample);
            finalize_reservoir(diReservoir, finalTarget);
            selectedDirectLightSample = finalSample;
            selectedDirectLightWeight = diReservoir.W;
        }

        float3 forwardDir = normalize(camForward);
        float viewZ = dot(hitPos - camPos, forwardDir);
        if (viewZ > 0.0) {
            linearDepth = viewZ;
            if (dlssRayReconstruction > 0.5) {
                depth = viewZ;
            } else {
                float A = farZ / (farZ - nearZ);
                float B = (-nearZ * farZ) / (farZ - nearZ);
                depth = saturate(A + (B / viewZ));
            }
        }

        if (debugMode <= 0.5 && debugVisualizationMode <= 0.5) {
            color = EvaluateWavefrontPrimaryPreview(record, normal, hitPos);
        }

        uint previousValue = 0u;
        InterlockedAdd(g_wavefrontStats[9], 1u, previousValue);
        WavefrontAccumulateMaterialBinStat(
            WAVEFRONT_PRIMARY_MATERIAL_BIN_STATS_BASE, record.reserved);
        if (transmission > 0.05) {
            InterlockedAdd(g_wavefrontStats[12], 1u, previousValue);
        } else if (metallic > 0.45 || roughness < 0.3 || specularWeight > 0.55) {
            InterlockedAdd(g_wavefrontStats[11], 1u, previousValue);
        } else {
            InterlockedAdd(g_wavefrontStats[10], 1u, previousValue);
        }

        const bool allowContinuation =
            any(nextThroughput > 1.0e-4) &&
            WavefrontHasBounceBudget(state.packedState,
                                     nextRayType,
                                     maxSpecularBounceCount,
                                     maxRefractiveBounceCount,
                                     maxDiffuseBounceCount);
        if (allowContinuation) {
            uint secondaryIndex = 0u;
            InterlockedAdd(g_wavefrontQueueCounters[kWavefrontSecondaryQueueCounter], 1u,
                           secondaryIndex);
            if (secondaryIndex < pathQueueCapacity) {
                EmitWavefrontSecondaryPath(
                    secondaryIndex,
                    record.pixelIndex,
                    hitPos + nextDirection * kWavefrontRayBias,
                    nextDirection,
                    rng.state,
                    nextThroughput,
                    WavefrontAdvancePackedState(state.packedState, nextRayType));
                InterlockedAdd(g_wavefrontStats[16], 1u, previousValue);
                if (nextRayType == RAY_TYPE_REFRACTION) {
                    InterlockedAdd(g_wavefrontStats[19], 1u, previousValue);
                } else if (nextRayType == RAY_TYPE_REFLECTION) {
                    InterlockedAdd(g_wavefrontStats[18], 1u, previousValue);
                } else {
                    InterlockedAdd(g_wavefrontStats[17], 1u, previousValue);
                }
            } else {
                InterlockedAdd(g_wavefrontStats[21], 1u, previousValue);
            }
        }

        float3 shadowWeight = state.throughput *
                              ComputeWavefrontDirectLightingWeight(
                                  record, normal, hitPos,
                                  selectedDirectLightSample.direction) *
                              selectedDirectLightWeight;
        if (selectedDirectLightWeight > 0.0 && any(shadowWeight > 1.0e-4)) {
            uint shadowIndex = 0u;
            InterlockedAdd(g_wavefrontQueueCounters[kWavefrontShadowQueueCounter],
                           1u, shadowIndex);
            if (shadowIndex < shadowQueueCapacity) {
                EmitWavefrontShadowTask(
                    shadowIndex,
                    hitPos + normal * kWavefrontRayBias,
                    selectedDirectLightSample.direction,
                    selectedDirectLightSample.maxDistance,
                    selectedDirectLightSample.packedLightIndex,
                    shadowWeight,
                    record.pixelIndex);
                InterlockedAdd(g_wavefrontStats[20], 1u, previousValue);
            } else {
                InterlockedAdd(g_wavefrontStats[22], 1u, previousValue);
            }
        }
    } else {
        uint previousValue = 0u;
        InterlockedAdd(g_wavefrontStats[13], 1u, previousValue);
    }

    if (isMiss) {
        StoreWavefrontDiReservoir(pixel, init_reservoir());
    } else {
        StoreWavefrontDiReservoir(pixel, diReservoir);
    }
    ClearWavefrontGiReservoir(pixel);

    color = max(color, 0.0);
    g_output[pixel] = float4(color, 1.0);
    g_accumulation[pixel] = float4(color, 1.0);
    g_depth[pixel] = depth;
    g_linearDepth[pixel] = linearDepth;
    g_motionVectors[pixel] = motion;
    g_albedoOut[pixel] = float4(albedo, 1.0);
    g_normalRoughnessOut[pixel] = float4(normalize(normal), roughness);
    g_specularAlbedo[pixel] = float4(specularAlbedo, 1.0);
    g_specHitDistance[pixel] = 0.0;
    g_specularMotionVectors[pixel] = any(specularAlbedo > 0.0) ? motion : kInvalidMvec;
    g_transmissionAccumulation[pixel] = float4(0.0, 0.0, 0.0, 0.0);
    g_transmissionVariance[pixel] = 0.0;
}