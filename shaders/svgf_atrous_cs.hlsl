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

Texture2D<float4> g_inputColor : register(t0);
Texture2D<float> g_variance : register(t1);
Texture2D<float4> g_normalRoughness : register(t2);
Texture2D<float> g_depth : register(t3);

RWTexture2D<float4> g_outputColor : register(u0);

static const float kKernel[5] = {1.0, 2.0, 4.0, 2.0, 1.0};
static const float3 kLuma = float3(0.2126, 0.7152, 0.0722);

[numthreads(GROUP_SIZE_X, GROUP_SIZE_Y, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    if (dispatchThreadID.x >= g_width || dispatchThreadID.y >= g_height)
        return;

    int2 pixel = int2(dispatchThreadID.xy);
    float3 centerColor = g_inputColor.Load(int3(pixel, 0)).rgb;
    float centerDepth = g_depth.Load(int3(pixel, 0));
    float3 centerNormal = normalize(g_normalRoughness.Load(int3(pixel, 0)).xyz);
    float centerVariance = max(g_variance.Load(int3(pixel, 0)), 1e-5);
    float centerLuma = dot(centerColor, kLuma);

    float3 accumColor = 0.0;
    float accumWeight = 0.0;

    [unroll]
    for (int ky = -2; ky <= 2; ++ky)
    {
        [unroll]
        for (int kx = -2; kx <= 2; ++kx)
        {
            int2 samplePixel = pixel + int2(kx, ky) * int(g_stepWidth);
            samplePixel.x = clamp(samplePixel.x, 0, int(g_width) - 1);
            samplePixel.y = clamp(samplePixel.y, 0, int(g_height) - 1);

            float3 sampleColor = g_inputColor.Load(int3(samplePixel, 0)).rgb;
            float sampleDepth = g_depth.Load(int3(samplePixel, 0));
            float3 sampleNormal = normalize(g_normalRoughness.Load(int3(samplePixel, 0)).xyz);
            float sampleLuma = dot(sampleColor, kLuma);

            float kernelWeight = kKernel[abs(kx)] * kKernel[abs(ky)];
            float colorWeight = exp(-abs(sampleLuma - centerLuma) /
                                    max(0.75 * g_phiColor * sqrt(centerVariance), 1e-4));
            float normalWeight = pow(saturate(dot(centerNormal, sampleNormal)), g_phiNormal / 24.0);
            float depthWeight = exp(-abs(sampleDepth - centerDepth) /
                                    max(0.75 * g_phiDepth, 1e-4));
            float weight = kernelWeight * colorWeight * normalWeight * depthWeight;

            accumColor += sampleColor * weight;
            accumWeight += weight;
        }
    }

    if (accumWeight <= 1e-5)
        g_outputColor[pixel] = float4(centerColor, 1.0);
    else
        g_outputColor[pixel] = float4(accumColor / accumWeight, 1.0);
}
