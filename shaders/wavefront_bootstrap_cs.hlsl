#include "raytracing/common.hlsli"

cbuffer WavefrontBootstrapConstants : register(b1)
{
    uint outputWidth;
    uint outputHeight;
    uint backendMode;
    uint maxPathCount;
};

static uint WangHash(uint value)
{
    value = (value ^ 61u) ^ (value >> 16u);
    value *= 9u;
    value = value ^ (value >> 4u);
    value *= 0x27d4eb2du;
    value = value ^ (value >> 15u);
    return value;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadID.xy;
    if (pixel.x >= outputWidth || pixel.y >= outputHeight) {
        return;
    }

    const uint pixelIndex = pixel.y * outputWidth + pixel.x;

    if (pixelIndex == 0) {
        const uint totalPathCount = outputWidth * outputHeight;
        const uint groupCount = (totalPathCount + 63u) / 64u;

        g_wavefrontDispatchArgs[0].groupCountX = groupCount;
        g_wavefrontDispatchArgs[0].groupCountY = 1u;
        g_wavefrontDispatchArgs[0].groupCountZ = 1u;
        g_wavefrontDispatchArgs[0].activeCount = totalPathCount;

        g_wavefrontStats[0] = outputWidth;
        g_wavefrontStats[1] = outputHeight;
        g_wavefrontStats[2] = totalPathCount;
        g_wavefrontStats[3] = backendMode;
        g_wavefrontStats[4] = asuint(globalFrameCount);
        g_wavefrontStats[5] = asuint(accumulationCount);
        g_wavefrontStats[50] = WAVEFRONT_ABI_VERSION;
    }

    uint queueIndex = 0u;
    InterlockedAdd(g_wavefrontQueueCounters[WAVEFRONT_QUEUE_PATH_A], 1u,
                   queueIndex);
    if (queueIndex >= maxPathCount) {
        InterlockedAdd(g_wavefrontStats[15], 1u);
        return;
    }

    const float2 screenDim = float2(outputWidth, outputHeight);
    const float2 uv = (float2(pixel) + 0.5 + float2(jitterX, jitterY)) / screenDim;
    const float2 ndc = uv * 2.0 - 1.0;

    const float fInv = tan(radians(fov) * 0.5);
    const float3 forwardDir = normalize(camForward);
    const float3 rightDir = normalize(cross(forwardDir, camUp));
    const float3 upDir = normalize(cross(rightDir, forwardDir));

    const float yView = (-ndc.y) * fInv;
    const float xView = ndc.x * aspect * fInv;

    WavefrontPathState state;
    state.origin = camPos;
    state.direction = normalize(xView * rightDir + yView * upDir + forwardDir);
    state.pixelIndex = pixelIndex;
    state.rngState = WangHash(pixelIndex ^ asuint(globalFrameCount) ^ 0x9E3779B9u);
    state.throughput = float3(1.0, 1.0, 1.0);
    state.packedState = RAY_TYPE_PRIMARY;

    g_wavefrontPathQueueA[queueIndex] = state;
}
