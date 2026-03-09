#define GROUP_SIZE_X 8
#define GROUP_SIZE_Y 8

cbuffer SvgfConstants : register(b0)
{
    uint g_width;
    uint g_height;
    uint g_stepWidth;
    uint g_resetHistory;
    float g_temporalAlpha;
    float g_momentsAlpha;
    float g_phiColor;
    float g_phiNormal;
    float g_phiDepth;
    float g_normalRejectCos;
    float g_depthRejectScale;
    float g_pad0;
};

Texture2D<float4> g_temporalColor : register(t0);
Texture2D<float2> g_moments : register(t1);
Texture2D<float> g_historyLength : register(t2);

RWTexture2D<float> g_varianceOut : register(u0);

static const float kKernel[4] = { 0.25, 0.1875, 0.125, 0.0625 }; // 7x7 weights

[numthreads(GROUP_SIZE_X, GROUP_SIZE_Y, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    if (dispatchThreadID.x >= g_width || dispatchThreadID.y >= g_height)
        return;

    int2 pixel = int2(dispatchThreadID.xy);
    float historyLength = max(g_historyLength.Load(int3(pixel, 0)), 1.0);

    float accumVariance = 0.0;
    float accumWeight = 0.0;

    for (int oy = -3; oy <= 3; ++oy)
    {
        for (int ox = -3; ox <= 3; ++ox)
        {
            int2 samplePixel = clamp(pixel + int2(ox, oy), int2(0, 0),
                                     int2(int(g_width) - 1, int(g_height) - 1));
            float2 sampleMoments = g_moments.Load(int3(samplePixel, 0));
            float sampleHistoryLength =
                max(g_historyLength.Load(int3(samplePixel, 0)), 1.0);
            float sampleVariance =
                max(sampleMoments.y - sampleMoments.x * sampleMoments.x, 0.0);

            float kernelWeight = kKernel[abs(ox)] * kKernel[abs(oy)];
            // Give more weight to samples with more history
            float historyWeight = sqrt(max(sampleHistoryLength, 1.0));
            float weight = kernelWeight * historyWeight;

            accumVariance += sampleVariance * weight;
            accumWeight += weight;
        }
    }

    float variance = (accumWeight > 0.0) ? (accumVariance / accumWeight) : 0.0;
    if (historyLength < 4.0)
    {
        variance *= (4.0 / historyLength);
    }

    g_varianceOut[pixel] = max(variance, 1e-5);
}
