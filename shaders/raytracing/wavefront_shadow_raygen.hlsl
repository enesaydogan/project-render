#include "common.hlsli"
#include "wavefront_transport.hlsli"

[shader("raygeneration")]
void WavefrontShadowRayGen()
{
    const uint taskIndex = DispatchRaysIndex().x;
    const uint activeCount = g_wavefrontQueueCounters[WAVEFRONT_QUEUE_SHADOW];
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

    // pixelIndex is encoded with the bootstrap's current-frame render
    // width (see g_wavefrontStats[0]/[1]). With DRR enabled the texture
    // dims (g_output.GetDimensions) are at max-render — not what the
    // index was packed with — so decode using the stashed current width.
    const uint width = max(g_wavefrontStats[0], 1u);
    const uint height = max(g_wavefrontStats[1], 1u);
    if (g_wavefrontStats[0] == 0u || g_wavefrontStats[1] == 0u) {
        return;
    }

    const uint pixelIndex = task.packedState;
    const uint2 pixel = uint2(pixelIndex % width, pixelIndex / width);
    if (pixel.x >= width || pixel.y >= height) {
        return;
    }

    const uint lightSampleType = WavefrontGetLightSampleType(task.packedLightIndex);
    float3 visibilityDirection = normalize(task.direction);
    if (lightSampleType == WAVEFRONT_LIGHT_SAMPLE_DIRECTIONAL &&
        lightDir.w > 0.0) {
        RNG shadowRng;
        uint stableSeed =
            (pixelIndex * 0xC2B2AE35u) ^
            (task.packedLightIndex * 0x27D4EB2Du);
        shadowRng.state = (((uint)globalFrameCount * 0x9E3779B9u) ^
                           stableSeed);
        visibilityDirection = WavefrontBuildSunVisibilityDirection(
            visibilityDirection, shadowRng);
    }

    RayPayload shadowPayload;
    shadowPayload.t = 1.0;
    shadowPayload.packedColor1 = 0u;
    PayloadSetColor(shadowPayload, float3(0.0, 0.0, 0.0));
    shadowPayload.packedNormal = PackNormalOctahedron(float3(0.0, 1.0, 0.0));
    shadowPayload.packedAlbedo = PackPayloadAlbedo(float3(0.0, 0.0, 0.0));
    shadowPayload.packedSurface = PackPayloadSurface(1.0, 0.0, 0.0, 0.0);
    shadowPayload.surface = float4(1.0, 0.0, 0.0, 0.0);
    shadowPayload.packedIorType = PackPayloadIorType(1.0, RAY_TYPE_SHADOW, false, 1.0);
    shadowPayload.packedTransmission = PackPayloadTransmissionColor(float3(1.0, 1.0, 1.0));
    shadowPayload.packedSpecular = PackPayloadSpecularColor(float3(1.0, 1.0, 1.0));

    RayDesc shadowRay;
    shadowRay.Origin = task.origin;
    shadowRay.Direction = visibilityDirection;
    shadowRay.TMin = 0.001;
    shadowRay.TMax = max(0.001, task.maxDistance - 0.002);

    TraceRay(g_accel, RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xFF, 0, 0, 0, shadowRay, shadowPayload);
    const bool visible = (shadowPayload.t < 0.0);
    SHADER_COUNTER_ADD(SHADER_COUNTER_SHADOW_TRACES, 1);

    if (visible) {
        uint previousValue = 0u;
        InterlockedAdd(g_wavefrontStats[30], 1u, previousValue);
        float3 contribution = max(task.throughput, 0.0);
        if (lightSampleType == WAVEFRONT_LIGHT_SAMPLE_ENV) {
            contribution *= WavefrontEvaluateShadowTaskRadiance(
                task.packedLightIndex, task.origin, task.direction);
        }
        WavefrontAtomicAddShadowContribution(pixelIndex, contribution);
    } else {
        uint previousValue = 0u;
        InterlockedAdd(g_wavefrontStats[31], 1u, previousValue);
    }
}
