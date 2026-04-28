#include "raytracing/common.hlsli"
#include "raytracing/wavefront_transport.hlsli"

cbuffer WavefrontSecondaryResolveConstants : register(b1)
{
    uint outputWidth;
    uint outputHeight;
    uint maxDispatchCount;
    uint reservedFlags;
};

static const uint kWavefrontContinuationQueueCounterA = 0u;
static const uint kWavefrontContinuationQueueCounterB = 4u;
static const uint kWavefrontShadowQueueCounter = 5u;
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
    float3 specularColor = UnpackPayloadSpecularColor(record.packedSpecular);
    float4 surface = UnpackPayloadSurface(record.packedSurface);
    float roughness = saturate(surface.x);
    float metallic = saturate(surface.y);
    float transmission = saturate(surface.z);
    float translucency = saturate(surface.w);
    float specularWeight = saturate(UnpackPayloadSpecularWeight(record.packedIorType));

    float3 V = normalize(camPos - hitPos);
    float3 L = normalize(lightDir.xyz);
    float3 H = normalize(V + L);
    float NdotL = saturate(dot(worldNormal, L));
    float NdotV = saturate(dot(worldNormal, V));
    float NdotH = saturate(dot(worldNormal, H));
    float VdotH = saturate(dot(V, H));

    float3 diffuseColor = baseColor * (1.0 - metallic) * (1.0 - transmission);
    float3 dielectricF0 = 0.04.xxx * specularWeight;
    float3 F0 = lerp(dielectricF0 * max(specularColor, 0.0), baseColor, metallic);
    float alpha = max(roughness * roughness, 0.03);
    float alpha2 = alpha * alpha;
    float denom = max(NdotH * NdotH * (alpha2 - 1.0) + 1.0, 1.0e-4);
    float D = alpha2 / max(PI * denom * denom, 1.0e-4);
    float k = (alpha + 1.0) * (alpha + 1.0) * 0.125;
    float Gv = NdotV / max(lerp(NdotV, 1.0, k), 1.0e-4);
    float Gl = NdotL / max(lerp(NdotL, 1.0, k), 1.0e-4);
    float3 F = F0 + (1.0 - F0) * pow(max(1.0 - VdotH, 0.0), 5.0);
    float3 specular = (D * Gv * Gl) * F;

    float horizon = saturate(worldNormal.y * 0.5 + 0.5);
    float3 ambient = diffuseColor * lerp(0.04, 0.14, horizon) * intensity;
    float3 transmissionTint =
        UnpackPayloadTransmissionColor(record.packedTransmission) * transmission;
    float edgeLight = pow(max(1.0 - NdotV, 0.0), 3.0);
    float3 translucentWrap = transmissionTint * (0.08 + 0.24 * edgeLight) *
                             max(translucency, transmission) * intensity;

    float3 localLighting = WavefrontHitRecordGetColor(record) + ambient + translucentWrap;
    return max(state.throughput, 0.0) * max(localLighting, 0.0);
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    const bool sourceIsQueueA = (reservedFlags & WAVEFRONT_QUEUE_FLAG_SOURCE_IS_A) != 0u;
    const uint activeCount = min(maxDispatchCount,
                                 sourceIsQueueA ? g_wavefrontQueueCounters[0]
                                                : g_wavefrontQueueCounters[4]);
    const uint pathIndex = dispatchThreadID.x;
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
    if (WavefrontHitRecordIsMiss(record)) {
        contribution = max(state.throughput, 0.0) * WavefrontHitRecordGetColor(record);
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
        float specularWeight = saturate(UnpackPayloadSpecularWeight(record.packedIorType));
        float ior = UnpackPayloadIor(record.packedIorType);
        float3 transmissionTint = UnpackPayloadTransmissionColor(record.packedTransmission);
        float3 specularAlbedo = UnpackPayloadSpecularColor(record.packedSpecular) *
                                saturate(max(metallic, specularWeight) *
                                         (1.0 - 0.5 * transmission));
        RNG rng;
        rng.state = state.rngState ^ (pathIndex * 0x7F4A7C15u) ^ 0xC2B2AE3Du;
        const uint maxSpecularBounceCount =
            (maxSpecularBounces > 0.0) ? (uint)maxSpecularBounces : 0u;
        const uint maxRefractiveBounceCount =
            (maxRefractiveBounces > 0.0) ? (uint)maxRefractiveBounces : 0u;
        const uint maxDiffuseBounceCount =
            (maxGIBounces > 0.0) ? (uint)maxGIBounces : 0u;

        uint nextRayType = RAY_TYPE_DIFFUSE;
        float3 nextDirection = normal;
        float3 nextThroughput = state.throughput * max(albedo, 0.02.xxx);
        
        // Evaluate probabilities for path continuation to avoid hard cut-offs
        float transmissionProb = max(transmission, 0.0);
        float reflectionProb = saturate(max(metallic, specularWeight) * (1.0 - transmissionProb));
        float diffuseProb = saturate(1.0 - transmissionProb - reflectionProb);
        
        float rnd = next_float(rng);
        if (transmissionProb > 0.0 && rnd < transmissionProb) {
            nextRayType = RAY_TYPE_REFRACTION;
            nextDirection = BuildTransmissionContinuation(rayDir, normal, ior);
            nextThroughput = state.throughput * max(transmissionTint, 0.02.xxx) * max(transmission, 0.1) / max(transmissionProb, 1.0e-4);
        } else if (reflectionProb > 0.0 && rnd < (transmissionProb + reflectionProb)) {
            nextRayType = RAY_TYPE_REFLECTION;
            nextDirection = BuildSpecularContinuation(rayDir, normal);
            nextThroughput = state.throughput * max(specularAlbedo, 0.04.xxx) / max(reflectionProb, 1.0e-4);
        } else {
            nextRayType = RAY_TYPE_DIFFUSE;
            nextDirection = BuildDiffuseContinuation(normal, rng);
            nextThroughput = state.throughput * max(albedo, 0.02.xxx) * saturate(dot(normal, nextDirection)) / max(diffuseProb, 1.0e-4);
        }
        nextThroughput = max(nextThroughput, 0.0);

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

        WavefrontLightSample lightSample =
            WavefrontSampleDirectLight(hitPos, rng);
        float3 shadowWeight = state.throughput *
                              ComputeWavefrontDirectLightingWeight(
                                  record, normal, hitPos, lightSample.direction);
        if (any(shadowWeight > 1.0e-4)) {
            uint shadowIndex = 0u;
            InterlockedAdd(g_wavefrontQueueCounters[kWavefrontShadowQueueCounter],
                           1u, shadowIndex);
            if (shadowIndex < shadowQueueCapacity) {
                EmitWavefrontShadowTask(
                    shadowIndex,
                    hitPos + normal * kWavefrontRayBias,
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

        uint previousValue = 0u;
        InterlockedAdd(g_wavefrontStats[27], 1u, previousValue);
        WavefrontAccumulateMaterialBinStat(
            WAVEFRONT_SECONDARY_MATERIAL_BIN_STATS_BASE, record.reserved);
    }

    contribution = max(contribution, 0.0);
    g_output[pixel] = float4(g_output[pixel].rgb + contribution, 1.0);
    float4 accum = g_accumulation[pixel];
    g_accumulation[pixel] = float4(accum.rgb + contribution, accum.a);
}