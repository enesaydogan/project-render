#include "common.hlsli"

static RayPayload InitWavefrontShadowPayload()
{
    RayPayload payload;
    payload.t = 1.0;
    payload.packedColor1 = 0u;
    PayloadSetColor(payload, float3(0.0, 0.0, 0.0));
    payload.packedNormal = PackNormalOctahedron(float3(0.0, 1.0, 0.0));
    payload.packedAlbedo = PackPayloadAlbedo(float3(0.0, 0.0, 0.0));
    payload.packedSurface = PackPayloadSurface(1.0, 0.0, 0.0, 0.0);
    payload.packedIorType = PackPayloadIorType(1.0, RAY_TYPE_SHADOW, false, 1.0);
    payload.packedTransmission = PackPayloadTransmissionColor(float3(1.0, 1.0, 1.0));
    payload.packedSpecular = PackPayloadSpecularColor(float3(1.0, 1.0, 1.0));
    return payload;
}

[shader("raygeneration")]
void WavefrontShadowRayGen()
{
    const uint taskIndex = DispatchRaysIndex().x;
    const uint activeCount = g_wavefrontQueueCounters[5];
    if (taskIndex >= activeCount) {
        return;
    }

    uint queueCapacity, dummy;
    g_wavefrontShadowQueue.GetDimensions(queueCapacity, dummy);
    if (taskIndex >= queueCapacity) {
        return;
    }

    if (taskIndex == 0u) {
        uint previousValue = 0u;
        InterlockedAdd(g_wavefrontStats[29], activeCount, previousValue);
    }

    WavefrontShadowTask task = g_wavefrontShadowQueue[taskIndex];

    uint width = 0u;
    uint height = 0u;
    g_output.GetDimensions(width, height);
    if (width == 0u || height == 0u) {
        return;
    }

    const uint pixelIndex = task.packedState;
    const uint2 pixel = uint2(pixelIndex % width, pixelIndex / width);
    if (pixel.x >= width || pixel.y >= height) {
        return;
    }

    RayDesc ray;
    ray.Origin = task.origin;
    ray.Direction = normalize(task.direction);
    ray.TMin = 0.002;
    ray.TMax = max(task.maxDistance, 0.002);

    RayPayload payload = InitWavefrontShadowPayload();
    TraceRay(g_accel,
             RAY_FLAG_SKIP_CLOSEST_HIT_SHADER |
                 RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
             0xFF, 0, 0, 0, ray, payload);
    SHADER_COUNTER_ADD(SHADER_COUNTER_SHADOW_TRACES, 1);

    if (payload.t < 0.0) {
        uint previousValue = 0u;
        InterlockedAdd(g_wavefrontStats[30], 1u, previousValue);
        const float3 contribution = max(task.throughput, 0.0) *
                                    WavefrontEvaluateShadowTaskRadiance(
                                        task.packedLightIndex,
                                        task.origin,
                                        task.direction);
        float4 accum = g_accumulation[pixel];
        float historyCount = max(accum.a, 1.0);
        float3 outputContribution = (dlssRayReconstruction > 0.5)
                                        ? contribution
                                        : (contribution / historyCount);
        g_output[pixel] = float4(g_output[pixel].rgb + outputContribution, 1.0);
        g_accumulation[pixel] = float4(accum.rgb + contribution, accum.a);
    } else {
        uint previousValue = 0u;
        InterlockedAdd(g_wavefrontStats[31], 1u, previousValue);
    }
}
