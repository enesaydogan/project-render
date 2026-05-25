#include "raytracing/common.hlsli"

cbuffer WavefrontAccumulateConstants : register(b1)
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

    float4 sampleAndMarker = g_output[pixel];
    if (sampleAndMarker.a <= 0.5) {
        return;
    }

    float3 sampleColor = sampleAndMarker.rgb;
    if (!all(isfinite(sampleColor))) {
        sampleColor = float3(0.0, 0.0, 0.0);
    }
    sampleColor = max(sampleColor, 0.0);

    const float3 kLumaWeights = float3(0.2126, 0.7152, 0.0722);
    const float kMaxSampleLuminance = 100000.0;
    float sampleLum = dot(sampleColor, kLumaWeights);
    if (!isfinite(sampleLum)) {
        sampleLum = 0.0;
    }
    if (sampleLum > kMaxSampleLuminance) {
        sampleColor *= kMaxSampleLuminance / sampleLum;
        sampleLum = kMaxSampleLuminance;
    }

    float4 history = g_accumulation[pixel];
    float historyCount = history.a;
    bool invalidHistory = !isfinite(historyCount) || historyCount < 1.0 ||
                          any(!isfinite(history.rgb));

    float safeHistoryCount = invalidHistory ? 0.0 : historyCount;
    float3 historySum =
        invalidHistory ? float3(0.0, 0.0, 0.0) : history.rgb;
    float3 oldMean =
        (safeHistoryCount > 0.0)
            ? (historySum / safeHistoryCount)
            : float3(0.0, 0.0, 0.0);
    float oldMeanLum = dot(oldMean, kLumaWeights);

    float nextCount = safeHistoryCount + 1.0;
    float3 nextSum = historySum + sampleColor;
    float3 nextMean = nextSum / nextCount;
    float nextMeanLum = dot(nextMean, kLumaWeights);

    float prevM2 = invalidHistory ? 0.0 : g_variance[pixel];
    float nextM2 = prevM2 +
                   (sampleLum - oldMeanLum) * (sampleLum - nextMeanLum);
    if (!isfinite(nextM2)) {
        nextM2 = 0.0;
    }
    nextM2 = max(nextM2, 0.0);

    g_accumulation[pixel] = float4(nextSum, nextCount);
    g_variance[pixel] = nextM2;

    if (dlssRayReconstruction > 0.5) {
        // DLSS-RR performs its own temporal accumulation; feed it the raw
        // single-sample color. Apply a tighter firefly clamp than the generic
        // 100k-nit cap so that extreme ReSTIR outliers in multi-light scenes
        // don't destabilize the denoiser's temporal history.
        const float kDLSSFireflyMaxLuminance = 10000.0;
        if (sampleLum > kDLSSFireflyMaxLuminance) {
            sampleColor *= kDLSSFireflyMaxLuminance / sampleLum;
        }
        g_output[pixel] = float4(sampleColor, 1.0);
    } else {
        g_output[pixel] = float4(nextMean, 1.0);
    }
}
