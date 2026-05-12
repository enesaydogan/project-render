#include "raytracing/common.hlsli"
#include "raytracing/wavefront_transport.hlsli"

cbuffer WavefrontSecondaryResolveConstants : register(b1)
{
    uint outputWidth;
    uint outputHeight;
    uint maxDispatchCount;
    uint reservedFlags;
};

static const uint kWavefrontContinuationQueueCounterA = WAVEFRONT_QUEUE_PATH_A;
static const uint kWavefrontContinuationQueueCounterB = WAVEFRONT_QUEUE_PATH_B;
static const uint kWavefrontShadowQueueCounter = WAVEFRONT_QUEUE_SHADOW;
static const float kWavefrontRayBias = 0.002f;

inline uint2 WavefrontSecondaryPixelCoord(uint pixelIndex)
{
    return uint2(pixelIndex % outputWidth, pixelIndex / outputWidth);
}

inline float3 SafeNormalize(float3 value, float3 fallback)
{
    float lenSq = dot(value, value);
    return (lenSq > 1.0e-8) ? value * rsqrt(lenSq) : fallback;
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

inline bool KeepWavefrontSecondaryShadow(float keepProbability,
                                         inout RNG rng,
                                         inout float3 weight)
{
    if (keepProbability >= 0.999f) {
        return true;
    }
    if (next_float(rng) >= keepProbability) {
        return false;
    }
    weight *= rcp(max(keepProbability, 1.0e-4f));
    return true;
}

inline float3 BuildDiffuseContinuation(float3 normal, inout RNG rng)
{
    float3 localSample = sample_hemisphere_cosine(next_float2(rng));
    return SafeNormalize(align_to_normal(localSample, normal), normal);
}

inline bool BuildSpecularContinuation(float3 rayDir, float3 normal,
                                      float roughness, inout RNG rng,
                                      out float3 continuationDir)
{
    float3 halfVector = SampleGGX(next_float2(rng), normal,
                                  max(roughness, 0.001));
    float3 reflected = reflect(rayDir, halfVector);
    float reflectedLenSq = dot(reflected, reflected);
    if (reflectedLenSq <= 1.0e-8) {
        continuationDir = normal;
        return false;
    }

    continuationDir = reflected * rsqrt(reflectedLenSq);
    float3 viewDir = normalize(-rayDir);
    float NdotL = saturate(dot(normal, continuationDir));
    float NdotH = saturate(dot(normal, halfVector));
    float VdotH = saturate(dot(viewDir, halfVector));
    return (NdotL > 1.0e-5 && NdotH > 1.0e-5 && VdotH > 1.0e-5);
}

inline float3 BuildTransmissionContinuation(float3 rayDir, float3 normal,
                                            float ior, bool thinWalled)
{
    if (thinWalled) {
        return normalize(rayDir);
    }

    float entering = dot(rayDir, normal) < 0.0 ? 1.0 : 0.0;
    float3 faceNormal = (entering > 0.5) ? normal : -normal;
    float eta = (entering > 0.5) ? rcp(max(ior, 1.0)) : max(ior, 1.0);
    float3 refracted = refract(rayDir, faceNormal, eta);
    if (dot(refracted, refracted) < 1.0e-8) {
        refracted = reflect(rayDir, faceNormal);
    }
    return SafeNormalize(refracted, -faceNormal);
}

inline void EmitWavefrontContinuationPath(uint queueIndex,
                                          bool destinationIsQueueA,
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
    if (destinationIsQueueA) {
        g_wavefrontPathQueueA[queueIndex] = nextState;
    } else {
        g_wavefrontPathQueueB[queueIndex] = nextState;
    }
}

inline float3 EvaluateWavefrontSecondaryContribution(
    WavefrontHitRecord record,
    WavefrontPathState state,
    float3 worldNormal,
    float3 hitPos)
{
    float3 baseColor = UnpackPayloadAlbedo(record.packedAlbedo);
    float4 surface = UnpackPayloadSurface(record.packedSurface);
    float metallic = saturate(surface.y);
    float transmission = saturate(surface.z);
    float3 diffuseAlbedo = baseColor * (1.0 - metallic) *
                           (1.0 - transmission);
    float horizon = saturate(worldNormal.y * 0.5 + 0.5);
    float3 ambient = diffuseAlbedo * lerp(0.08, 0.24, horizon);
    return max(state.throughput, 0.0) *
           max(WavefrontHitRecordGetColor(record) + ambient, 0.0);
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    const bool sourceIsQueueA = (reservedFlags & WAVEFRONT_QUEUE_FLAG_SOURCE_IS_A) != 0u;
    const uint activeCount = min(maxDispatchCount,
                                 sourceIsQueueA
                                     ? g_wavefrontQueueCounters[WAVEFRONT_QUEUE_PATH_A]
                                     : g_wavefrontQueueCounters[WAVEFRONT_QUEUE_PATH_B]);
    uint pathIndex = dispatchThreadID.x;
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
            dispatchThreadID.x >= perBinCapacity ||
            dispatchThreadID.x >=
                g_wavefrontQueueCounters[
                    WAVEFRONT_MATERIAL_BIN_COUNTER_BASE + materialBin]) {
            return;
        }
        pathIndex =
            g_wavefrontMaterialBinIndices[materialBin * perBinCapacity +
                                          dispatchThreadID.x];
    }
    if (pathIndex >= activeCount) {
        return;
    }

    if (pathIndex == 0u) {
        uint previousValue = 0u;
        InterlockedAdd(g_wavefrontStats[26], activeCount, previousValue);
    }

    WavefrontHitRecord record = g_wavefrontHitQueue[pathIndex];
    const bool destinationIsQueueA = !sourceIsQueueA;
    const uint continuationQueueCounter = destinationIsQueueA
                                              ? kWavefrontContinuationQueueCounterA
                                              : kWavefrontContinuationQueueCounterB;
    WavefrontPathState state;
    if (sourceIsQueueA) {
        state = g_wavefrontPathQueueA[pathIndex];
    } else {
        state = g_wavefrontPathQueueB[pathIndex];
    }
    uint continuationQueueCapacity = 0u;
    uint continuationQueueStride = 0u;
    if (destinationIsQueueA) {
        g_wavefrontPathQueueA.GetDimensions(continuationQueueCapacity, continuationQueueStride);
    } else {
        g_wavefrontPathQueueB.GetDimensions(continuationQueueCapacity, continuationQueueStride);
    }
    uint shadowQueueCapacity = 0u;
    uint shadowQueueStride = 0u;
    g_wavefrontShadowQueue.GetDimensions(shadowQueueCapacity, shadowQueueStride);
    uint2 pixel = WavefrontSecondaryPixelCoord(record.pixelIndex);
    if (pixel.x >= outputWidth || pixel.y >= outputHeight) {
        return;
    }

    float3 contribution = float3(0.0, 0.0, 0.0);
    const bool isMiss = WavefrontHitRecordIsMiss(record);
    if (missOnly && !isMiss) {
        return;
    }
    if (isMiss) {
        if (useMaterialBinList) {
            return;
        }
        float3 missRadiance = WavefrontHitRecordGetColor(record);
        if (WavefrontGetPathRayType(state.packedState) == RAY_TYPE_DIFFUSE) {
            missRadiance *= GetDxrIndirectIblBoost();
        }
        contribution = max(state.throughput, 0.0) * missRadiance;
        uint previousValue = 0u;
        InterlockedAdd(g_wavefrontStats[28], 1u, previousValue);
    } else {
        float3 rayDir = normalize(state.direction);
        float3 hitPos = state.origin + rayDir * record.hitT;
        float3 normal = UnpackNormalOctahedron(record.packedNormal);
        float3 albedo = UnpackPayloadAlbedo(record.packedAlbedo);
        float4 surface = UnpackPayloadSurface(record.packedSurface);
        float roughness = saturate(surface.x);
        float metallic = saturate(surface.y);
        float transmission = saturate(surface.z);
        float translucency = saturate(surface.w);
        float specularWeight = saturate(UnpackPayloadSpecularWeight(record.packedIorType));
        float ior = UnpackPayloadIor(record.packedIorType);
        bool thinWalled = UnpackPayloadThinWalled(record.packedIorType);
        float3 transmissionTint = UnpackPayloadTransmissionColor(record.packedTransmission);
        float3 specularColor = UnpackPayloadSpecularColor(record.packedSpecular);
        float3 specularAlbedo = ComputeWavefrontSpecularThroughput(
            albedo, metallic, ior, specularWeight, specularColor, transmission);
        RNG rng;
        rng.state = state.rngState ^
                    (record.pixelIndex * 0x7F4A7C15u) ^ 0xC2B2AE3Du;
        const uint maxSpecularBounceCount =
            (maxSpecularBounces > 0.0) ? (uint)maxSpecularBounces : 0u;
        const uint maxRefractiveBounceCount =
            (maxRefractiveBounces > 0.0) ? (uint)maxRefractiveBounces : 0u;
        const bool fastGi =
            (reservedFlags & WAVEFRONT_RESOLVE_FLAG_FAST_GI) != 0u;
        const bool thinSecondaryShadows =
            (reservedFlags & WAVEFRONT_RESOLVE_FLAG_THIN_SECONDARY_SHADOWS) != 0u;
        uint maxDiffuseBounceCount =
            (maxGIBounces > 0.0) ? (uint)maxGIBounces : 0u;
        if (fastGi) {
            maxDiffuseBounceCount = min(maxDiffuseBounceCount, 1u);
        }

        uint nextRayType = RAY_TYPE_DIFFUSE;
        float3 nextDirection = normal;
        float3 nextThroughput = state.throughput * max(albedo, 0.02.xxx);
        
        // Evaluate probabilities for path continuation to avoid hard cut-offs
        float transmissionProb = 0.0;
        float reflectionProb = 0.0;
        float diffuseProb = 0.0;
        ComputeWavefrontLobeProbabilities(normal, -rayDir,
                          albedo, metallic, transmission,
                          translucency, ior, specularWeight,
                          specularColor,
                          reflectionProb, diffuseProb,
                          transmissionProb);
        
        float rnd = next_float(rng);
        if (transmissionProb > 0.0 && rnd < transmissionProb) {
            nextRayType = RAY_TYPE_REFRACTION;
            nextDirection =
                BuildTransmissionContinuation(rayDir, normal, ior,
                                              thinWalled);
            nextThroughput = state.throughput * max(transmissionTint, 0.02.xxx) * max(transmission, 0.1) / max(transmissionProb, 1.0e-4);
        } else if (reflectionProb > 0.0 && rnd < (transmissionProb + reflectionProb)) {
            nextRayType = RAY_TYPE_REFLECTION;
            if (BuildSpecularContinuation(rayDir, normal, roughness, rng,
                                          nextDirection)) {
                nextThroughput = state.throughput * max(specularAlbedo, 0.04.xxx) / max(reflectionProb, 1.0e-4);
            } else {
                nextThroughput = float3(0.0, 0.0, 0.0);
            }
        } else {
            nextRayType = RAY_TYPE_DIFFUSE;
            nextDirection = BuildDiffuseContinuation(normal, rng);
            nextThroughput = state.throughput * max(albedo, 0.02.xxx) * saturate(dot(normal, nextDirection)) / max(diffuseProb, 1.0e-4);
        }
        nextThroughput = max(nextThroughput, 0.0);

        if (fastGi && nextRayType == RAY_TYPE_DIFFUSE &&
            any(nextThroughput > 1.0e-4)) {
            const float keepProbability = 0.5;
            if (next_float(rng) >= keepProbability) {
                nextThroughput = float3(0.0, 0.0, 0.0);
            } else {
                nextThroughput *= rcp(keepProbability);
            }
        }

        contribution = EvaluateWavefrontSecondaryContribution(record, state, normal, hitPos);

        const bool allowContinuation =
            any(nextThroughput > 1.0e-4) &&
            WavefrontHasBounceBudget(state.packedState,
                                     nextRayType,
                                     maxSpecularBounceCount,
                                     maxRefractiveBounceCount,
                                     maxDiffuseBounceCount);
        if (allowContinuation) {
            uint continuationIndex = 0u;
            InterlockedAdd(g_wavefrontQueueCounters[continuationQueueCounter],
                           1u, continuationIndex);
            if (continuationIndex < continuationQueueCapacity) {
                EmitWavefrontContinuationPath(
                    continuationIndex,
                    destinationIsQueueA,
                    record.pixelIndex,
                    hitPos + nextDirection * kWavefrontRayBias,
                    nextDirection,
                    rng.state,
                    nextThroughput,
                    WavefrontAdvancePackedState(state.packedState, nextRayType));
                uint previousValue = 0u;
                InterlockedAdd(g_wavefrontStats[16], 1u, previousValue);
                if (nextRayType == RAY_TYPE_REFRACTION) {
                    InterlockedAdd(g_wavefrontStats[19], 1u, previousValue);
                } else if (nextRayType == RAY_TYPE_REFLECTION) {
                    InterlockedAdd(g_wavefrontStats[18], 1u, previousValue);
                } else {
                    InterlockedAdd(g_wavefrontStats[17], 1u, previousValue);
                }
            } else {
                uint previousValue = 0u;
                InterlockedAdd(g_wavefrontStats[21], 1u, previousValue);
            }
        }

        WavefrontLightSamplerContext lightSampler =
            WavefrontCreateLightSampler(hitPos);
        WavefrontLightSample lightSample =
            WavefrontSampleDirectLight(lightSampler, hitPos, rng);
        WavefrontLightSample explicitSunSample =
            WavefrontSampleDirectionalLight(1.0);
        uint bounceDepth = WavefrontGetSpecularBounceCount(state.packedState) +
                           WavefrontGetRefractiveBounceCount(state.packedState) +
                           WavefrontGetDiffuseBounceCount(state.packedState);
        const float secondaryShadowKeepProbability =
            thinSecondaryShadows ? ((bounceDepth <= 1u) ? 0.5f : 0.25f) : 1.0f;
        float3 sunShadowWeight = state.throughput *
                                 ComputeWavefrontDirectLightingWeightForView(
                                     record, normal, -rayDir,
                                     explicitSunSample.direction) *
                                 explicitSunSample.radiance;
        if (any(sunShadowWeight > 1.0e-4) &&
            KeepWavefrontSecondaryShadow(secondaryShadowKeepProbability,
                                         rng, sunShadowWeight)) {
            uint shadowIndex = 0u;
            InterlockedAdd(g_wavefrontQueueCounters[kWavefrontShadowQueueCounter],
                           1u, shadowIndex);
            if (shadowIndex < shadowQueueCapacity) {
                EmitWavefrontShadowTask(
                    shadowIndex,
                    WavefrontBuildShadowOrigin(hitPos, normal,
                                               explicitSunSample.direction,
                                               kWavefrontRayBias),
                    explicitSunSample.direction,
                    explicitSunSample.maxDistance,
                    explicitSunSample.packedLightIndex,
                    sunShadowWeight,
                    record.pixelIndex);
                uint previousValue = 0u;
                InterlockedAdd(g_wavefrontStats[20], 1u, previousValue);
            } else {
                uint previousValue = 0u;
                InterlockedAdd(g_wavefrontStats[22], 1u, previousValue);
            }
        }
        float3 shadowWeight = state.throughput *
                              ComputeWavefrontDirectLightingWeightForView(
                                  record, normal, -rayDir,
                                  lightSample.direction) *
                              lightSample.radiance;
        if (WavefrontGetLightSampleType(lightSample.packedLightIndex) !=
                WAVEFRONT_LIGHT_SAMPLE_DIRECTIONAL &&
            any(shadowWeight > 1.0e-4) &&
            KeepWavefrontSecondaryShadow(secondaryShadowKeepProbability,
                                         rng, shadowWeight)) {
            uint shadowIndex = 0u;
            InterlockedAdd(g_wavefrontQueueCounters[kWavefrontShadowQueueCounter],
                           1u, shadowIndex);
            if (shadowIndex < shadowQueueCapacity) {
                EmitWavefrontShadowTask(
                    shadowIndex,
                    WavefrontBuildShadowOrigin(hitPos, normal,
                                               lightSample.direction,
                                               kWavefrontRayBias),
                    lightSample.direction,
                    lightSample.maxDistance,
                    lightSample.packedLightIndex,
                    shadowWeight,
                    record.pixelIndex);
                uint previousValue = 0u;
                InterlockedAdd(g_wavefrontStats[20], 1u, previousValue);
            } else {
                uint previousValue = 0u;
                InterlockedAdd(g_wavefrontStats[22], 1u, previousValue);
            }
        }

        if (bounceDepth <= 1u) {
            float envSampleLod = clamp(log2(max(length(hitPos - camPos), 1.0e-3) * 0.02) +
                                           0.35,
                                       0.0, 10.0);
            LightSample envSample = sample_env_map(envMap, envConditionalCdf,
                                                   envMarginalCdf, linearSampler,
                                                   rng, envSampleLod);
            float NdotL_env = saturate(dot(normal, envSample.L));
            if (envSample.pdf > 1.0e-8 && NdotL_env > 0.0) {
                float misW = ComputeWavefrontEnvironmentMisWeight(record, normal,
                                                                  hitPos,
                                                                  envSample.L,
                                                                  envSample.pdf);
                float3 envShadowWeight = state.throughput *
                                         ComputeWavefrontDirectLightingWeightForView(
                                             record, normal, -rayDir,
                                             envSample.L) *
                                         (misW / max(envSample.pdf, 1.0e-8));
                if (any(envShadowWeight > 1.0e-4) &&
                    KeepWavefrontSecondaryShadow(secondaryShadowKeepProbability,
                                                 rng, envShadowWeight)) {
                    uint shadowIndex = 0u;
                    InterlockedAdd(g_wavefrontQueueCounters[kWavefrontShadowQueueCounter],
                                   1u, shadowIndex);
                    if (shadowIndex < shadowQueueCapacity) {
                        EmitWavefrontShadowTask(
                            shadowIndex,
                            WavefrontBuildShadowOrigin(hitPos, normal,
                                                       envSample.L,
                                                       kWavefrontRayBias),
                            envSample.L,
                            10000.0,
                            WavefrontPackLightSampleMetadata(
                                WAVEFRONT_LIGHT_SAMPLE_ENV, 0u),
                            envShadowWeight,
                            record.pixelIndex);
                        uint previousValue = 0u;
                        InterlockedAdd(g_wavefrontStats[20], 1u, previousValue);
                    } else {
                        uint previousValue = 0u;
                        InterlockedAdd(g_wavefrontStats[22], 1u, previousValue);
                    }
                }
            }
        }

        uint previousValue = 0u;
        InterlockedAdd(g_wavefrontStats[27], 1u, previousValue);
    }

    contribution = max(contribution, 0.0);
    if (any(contribution > 0.0)) {
        WavefrontAtomicAddShadowContribution(record.pixelIndex,
                                             contribution);
    }
}
