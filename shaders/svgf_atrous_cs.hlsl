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
    uint g_remodulateAlbedo;
};

Texture2D<float4> g_inputColor : register(t0);
Texture2D<float> g_variance : register(t1);
Texture2D<float4> g_normalRoughness : register(t2);
Texture2D<float> g_depth : register(t3);
Texture2D<float4> g_albedo : register(t4);

RWTexture2D<float4> g_outputColor : register(u0);

static const float kKernel[3] = { 0.375, 0.25, 0.0625 };
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
    float centerVariance = g_stepWidth == 1 ? max(g_variance.Load(int3(pixel, 0)), 1e-5) : max(g_inputColor.Load(int3(pixel, 0)).a, 1e-5);
    float centerLuma = dot(centerColor, kLuma);

    float3 accumColor = 0.0;
    float accumVariance = 0.0;
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
            float sampleVariance = g_stepWidth == 1 ? max(g_variance.Load(int3(samplePixel, 0)), 1e-5) : max(g_inputColor.Load(int3(samplePixel, 0)).a, 1e-5);
            float sampleLuma = dot(sampleColor, kLuma);

            float kernelWeight = kKernel[abs(kx)] * kKernel[abs(ky)];
            // Add a small epsilon to variance to prevent it from going to zero and causing boiling
            float varianceEstimate = max(0.5 * (centerVariance + sampleVariance), 1e-6);
            
            // Relative color weight: Use the maximum of center and sample luma 
            // to make the edge detection brightness-independent.
            float lumaDiff = abs(sampleLuma - centerLuma);
            // Dynamic Phi: Boost phi in dark areas to gather more light (reduces energy loss)
            float dynamicPhi = g_phiColor * (1.0 + 1.0 / (max(centerLuma, 0.05)));
            
            float colorWeight = exp(-lumaDiff /
                                    max(dynamicPhi * sqrt(varianceEstimate) + 1e-2, 1e-3));
            
            // Stronger normal weight exponent for better edge preservation
            float normalWeight = pow(saturate(dot(centerNormal, sampleNormal)),
                                     g_phiNormal);
            float maxDepth = max(max(abs(centerDepth), abs(sampleDepth)), 1e-4);
            float depthTolerance =
                max(0.01,
                    g_phiDepth * g_depthRejectScale * maxDepth *
                        max(float(g_stepWidth), 0.5));
            float depthWeight = exp(-abs(sampleDepth - centerDepth) / depthTolerance);
            float weight = kernelWeight * colorWeight * normalWeight * depthWeight;

            accumColor += sampleColor * weight;
            accumVariance += sampleVariance * weight;
            accumWeight += weight;
        }
    }

    // Final Normalization: Strict weight compensation to prevent energy leak
    float3 finalColor = accumColor / max(accumWeight, 1e-4);
    float finalVariance = accumVariance / max(accumWeight * accumWeight, 1e-4);

    // REMOVED: g_remodulateAlbedo logic. 
    // This must NOT be here because Atrous runs in multiple iterations.

    g_outputColor[pixel] = float4(finalColor, finalVariance);
}
