#include "common.hlsli"
#include "wavefront_transport.hlsli"

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

    float3 visibilityDirection = normalize(task.direction);
    if (WavefrontGetLightSampleType(task.packedLightIndex) ==
            WAVEFRONT_LIGHT_SAMPLE_DIRECTIONAL &&
        lightDir.w > 0.0) {
        RNG shadowRng;
        shadowRng.state =
            ((uint)globalFrameCount * 0x9E3779B9u) ^
            (taskIndex * 0x85EBCA6Bu) ^
            (pixelIndex * 0xC2B2AE35u);
        visibilityDirection = WavefrontBuildSunVisibilityDirection(
            visibilityDirection, shadowRng);
    }

    const bool visible = WavefrontTraceVisibility(
        task.origin, visibilityDirection, task.maxDistance);
    SHADER_COUNTER_ADD(SHADER_COUNTER_SHADOW_TRACES, 1);

    if (visible) {
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
