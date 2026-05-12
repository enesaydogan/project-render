#include "raytracing/common.hlsli"
#include "raytracing/wavefront_transport.hlsli"

cbuffer WavefrontShadowIntegrateConstants : register(b1)
{
    uint outputWidth;
    uint outputHeight;
    uint reserved0;
    uint reserved1;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadID.xy;
    if (pixel.x >= outputWidth || pixel.y >= outputHeight) {
        return;
    }

    uint pixelIndex = pixel.y * outputWidth + pixel.x;
    uint baseIndex = pixelIndex * 4u;
    uint packedR = g_wavefrontShadowContribution[baseIndex + 0u];
    uint packedG = g_wavefrontShadowContribution[baseIndex + 1u];
    uint packedB = g_wavefrontShadowContribution[baseIndex + 2u];
    if ((packedR | packedG | packedB) == 0u) {
        return;
    }

    float3 contribution = float3(packedR, packedG, packedB) *
                          kWavefrontShadowContributionInvScale;

    const bool deferAccumulation =
        (reserved0 & WAVEFRONT_RESOLVE_FLAG_DEFER_ACCUMULATION) != 0u;
    if (deferAccumulation) {
        g_output[pixel] = float4(g_output[pixel].rgb + contribution,
                                 g_output[pixel].a);
    } else {
        float4 accum = g_accumulation[pixel];
        float historyCount = max(accum.a, 1.0);
        float3 outputContribution = (dlssRayReconstruction > 0.5)
                                        ? contribution
                                        : (contribution / historyCount);

        g_output[pixel] = float4(g_output[pixel].rgb + outputContribution,
                                 1.0);
        g_accumulation[pixel] = float4(accum.rgb + contribution, accum.a);
    }
    g_wavefrontShadowContribution[baseIndex + 0u] = 0u;
    g_wavefrontShadowContribution[baseIndex + 1u] = 0u;
    g_wavefrontShadowContribution[baseIndex + 2u] = 0u;
    g_wavefrontShadowContribution[baseIndex + 3u] = 0u;
}
