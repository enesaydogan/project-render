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
    if (!any(albedo > 0.0))
        return color;

    float3 safeAlbedo = float3(
        (albedo.x > 0.0) ? albedo.x : 1.0,
        (albedo.y > 0.0) ? albedo.y : 1.0,
        (albedo.z > 0.0) ? albedo.z : 1.0);
    return color / safeAlbedo;
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

    int2 prevPixel = int2(round((float2)pixel + mv));
    if (prevPixel.x < 0 || prevPixel.y < 0 || prevPixel.x >= int(g_width) || prevPixel.y >= int(g_height))
        validHistory = false;

    float3 historyColor = noisy;
    float2 historyMoments = float2(currLum, currLum * currLum);
    float historyLength = 1.0;

    if (validHistory)
    {
        float prevDepth = g_prevDepth.Load(int3(prevPixel, 0));
        float3 prevNormal = normalize(g_prevNormalRoughness.Load(int3(prevPixel, 0)).xyz);
        float depthDelta = abs(prevDepth - currDepth);
        float normalDot = dot(prevNormal, currNormal);
        float depthTolerance =
            max(0.01, max(abs(currDepth), abs(prevDepth)) * g_depthRejectScale);
        validHistory = normalDot >= g_normalRejectCos &&
                       depthDelta <= depthTolerance;
    }

    if (validHistory)
    {
        float3 prevColor = g_prevHistoryColor.Load(int3(prevPixel, 0)).rgb;
        float2 prevMoments = g_prevMoments.Load(int3(prevPixel, 0));
        float prevLen = max(g_prevHistoryLength.Load(int3(prevPixel, 0)), 1.0);

        float historyAlpha = 1.0 / min(prevLen + 1.0, 128.0);
        float colorAlpha = max(g_temporalAlpha, historyAlpha);
        float momentsAlpha = max(g_momentsAlpha, historyAlpha);
        historyColor = lerp(prevColor, noisy, saturate(colorAlpha));
        historyMoments = lerp(prevMoments, float2(currLum, currLum * currLum),
                              saturate(momentsAlpha));
        historyLength = min(prevLen + 1.0, 128.0);
    }

    g_temporalColorOut[pixel] = float4(historyColor, 1.0);
    g_momentsOut[pixel] = historyMoments;
    g_historyLengthOut[pixel] = historyLength;
}
