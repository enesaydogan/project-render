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

Texture2D<float4> g_noisyInput : register(t0);
Texture2D<float> g_depth : register(t1);
Texture2D<float4> g_normalRoughness : register(t2);
Texture2D<float2> g_motionVectors : register(t3);
Texture2D<float4> g_prevHistoryColor : register(t4);
Texture2D<float2> g_prevMoments : register(t5);
Texture2D<float> g_prevHistoryLength : register(t6);
Texture2D<float> g_prevDepth : register(t7);
Texture2D<float4> g_prevNormalRoughness : register(t8);
Texture2D<float4> g_albedo : register(t9);

RWTexture2D<float4> g_temporalColorOut : register(u0);
RWTexture2D<float2> g_momentsOut : register(u1);
RWTexture2D<float> g_historyLengthOut : register(u2);

static const float3 kLuma = float3(0.2126, 0.7152, 0.0722);

float3 DemodulateByAlbedo(float3 color, float3 albedo)
{
    return color / max(albedo, 0.01);
}

[numthreads(GROUP_SIZE_X, GROUP_SIZE_Y, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    if (dispatchThreadID.x >= g_width || dispatchThreadID.y >= g_height)
        return;

    int2 pixel = int2(dispatchThreadID.xy);
    float3 noisy = g_noisyInput.Load(int3(pixel, 0)).rgb;
    float3 albedo = g_albedo.Load(int3(pixel, 0)).rgb;
    // History is accumulated in illumination space to avoid reprojecting with
    // the wrong material albedo after disocclusions.
    noisy = DemodulateByAlbedo(noisy, albedo);
    float currDepth = g_depth.Load(int3(pixel, 0));
    float3 currNormal = normalize(g_normalRoughness.Load(int3(pixel, 0)).xyz);
    float currLum = dot(noisy, kLuma);

    bool validHistory = (g_resetHistory == 0);
    float2 mv = g_motionVectors.Load(int3(pixel, 0));
    if (abs(mv.x) > 1e5 || abs(mv.y) > 1e5)
        validHistory = false;

    float2 uv = (float2)pixel + mv;
    int2 prevPixel = int2(floor(uv));
    float2 fracPart = frac(uv);

    float3 historyColor = 0.0;
    float2 historyMoments = 0.0;
    float historyLength = 0.0;
    float totalWeight = 0.0;

    for (int y = 0; y <= 1; ++y)
    {
        for (int x = 0; x <= 1; ++x)
        {
            int2 p = prevPixel + int2(x, y);
            if (p.x >= 0 && p.y >= 0 && p.x < int(g_width) && p.y < int(g_height))
            {
                float pDepth = g_prevDepth.Load(int3(p, 0));
                float3 pNormal = normalize(g_prevNormalRoughness.Load(int3(p, 0)).xyz);
                
                float depthDelta = abs(pDepth - currDepth);
                float normalDot = dot(pNormal, currNormal);
                float depthTolerance = max(0.01, max(abs(currDepth), abs(pDepth)) * g_depthRejectScale);
                
                if (normalDot >= g_normalRejectCos && depthDelta <= depthTolerance)
                {
                    float w = (x == 0 ? 1.0 - fracPart.x : fracPart.x) *
                              (y == 0 ? 1.0 - fracPart.y : fracPart.y);
                    
                    historyColor += g_prevHistoryColor.Load(int3(p, 0)).rgb * w;
                    historyMoments += g_prevMoments.Load(int3(p, 0)) * w;
                    historyLength += g_prevHistoryLength.Load(int3(p, 0)) * w;
                    totalWeight += w;
                }
            }
        }
    }

    if (validHistory && totalWeight > 0.01)
    {
        float3 prevColor = historyColor / totalWeight;
        float2 prevMoments = historyMoments / totalWeight;
        float prevLen = historyLength / totalWeight;

        // Outlier rejection: be very careful not to kill valid new light
        float historyMean = prevMoments.x;
        float historyVar = max(0.0, prevMoments.y - historyMean * historyMean);
        float sigma = sqrt(historyVar);
        float colorLuma = dot(noisy, kLuma);
        
        // Only clamp if we have significant history (> 8 frames)
        if (prevLen > 8.0)
        {
            // Be more permissive with bright pixels (lumaMax) than dark ones (lumaMin)
            // This prevents darkening of valid bright signals like sun or ceiling bounce
            float lumaMin = historyMean - 2.0 * sigma;
            float lumaMax = historyMean + 5.0 * sigma; // Increased to 5-sigma for light preservation
            
            if (colorLuma < lumaMin || colorLuma > lumaMax)
            {
                float clampedLuma = clamp(colorLuma, lumaMin, lumaMax);
                noisy *= (clampedLuma / max(colorLuma, 1e-4));
                currLum = clampedLuma;
            }
        }

        float alpha = 1.0 / min(prevLen + 1.0, 128.0);
        float colorAlpha = max(g_temporalAlpha, alpha);
        float momentsAlpha = max(g_momentsAlpha, alpha);

        historyColor = lerp(prevColor, noisy, saturate(colorAlpha));
        historyMoments = lerp(prevMoments, float2(currLum, currLum * currLum), saturate(momentsAlpha));
        historyLength = min(prevLen + 1.0, 128.0);
    }
    else
    {
        historyColor = noisy;
        historyMoments = float2(currLum, currLum * currLum);
        historyLength = 1.0;
    }

    g_temporalColorOut[pixel] = float4(historyColor, 1.0);
    g_momentsOut[pixel] = historyMoments;
    g_historyLengthOut[pixel] = historyLength;
}
