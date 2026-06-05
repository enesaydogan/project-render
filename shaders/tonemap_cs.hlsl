// shaders/tonemap_cs.hlsl
// Simple tonemap + gamma pass: linear HDR (FP16) -> display LDR (R10G10B10A2_UNORM)

Texture2D<float4> g_hdrInput : register(t0);
Texture2D<float> g_ssaoTexture : register(t1);
Texture2D<float4> g_bloomTexture : register(t2);
RWTexture2D<float4> g_out : register(u0);

cbuffer TonemapCB : register(b0)
{
    uint outWidth;
    uint outHeight;
    float exposure;
    float vignette;  // 0 to 1
    float saturation; // 1.0 is neutral
    float contrast;   // 1.0 is neutral
    float ssaoEnabled;
    float ssaoCompositeWeight;
    float whiteBalance; // -1.0 cool, 0.0 neutral, 1.0 warm
    float tonemapDebugMode; // >0 -> bypass tonemap so debug colors aren't crushed
    float2 _pad0;
};

float3 ApplyWhiteBalance(float3 color, float balance)
{
    balance = clamp(balance, -1.0, 1.0);
    float3 warm = float3(1.18, 1.04, 0.82);
    float3 cool = float3(0.82, 0.96, 1.20);
    float3 neutral = float3(1.0, 1.0, 1.0);
    float3 factors = balance >= 0.0
        ? lerp(neutral, warm, balance)
        : lerp(neutral, cool, -balance);
    factors *= rcp(max(dot(factors, float3(0.2126, 0.7152, 0.0722)), 1.0e-4));
    return color * factors;
}

float3 ToneMapFilmic(float3 x)
{
    // Match shaders/raytracing/common.hlsli ToneMap() for consistency.
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= outWidth || id.y >= outHeight)
        return;

    uint bloomWidth = 0;
    uint bloomHeight = 0;
    g_bloomTexture.GetDimensions(bloomWidth, bloomHeight);

    float3 hdr = g_hdrInput.Load(int3(id.xy, 0)).rgb;

    // Debug view bypass: skip exposure / tonemap / SSAO / bloom / vignette
    // and just write the raster PS output through a gamma curve. Without
    // this, SSAO composite (aoTerm) can multiply diagnostic colors to zero,
    // which is what made every g_debugMode > 0 mode look black on raster.
    if (tonemapDebugMode > 0.0) {
        float3 dbgSrgb = pow(max(hdr, 0.0), 1.0 / 2.2);
        g_out[id.xy] = float4(dbgSrgb, 1.0);
        return;
    }

    float ssao = g_ssaoTexture.Load(int3(id.xy, 0)).r;
    if (ssaoEnabled == 0.0) ssao = 1.0;
    float aoTerm = lerp(1.0, saturate(ssao), saturate(ssaoCompositeWeight));

    float3 bloom = 0.0.xxx;
    if (bloomWidth > 0 && bloomHeight > 0)
    {
        float2 uv = (float2(id.xy) + 0.5) / float2(outWidth, outHeight);
        uint2 bloomCoord = min(uint2(uv * float2(bloomWidth, bloomHeight)),
                               uint2(bloomWidth - 1, bloomHeight - 1));
        bloom = g_bloomTexture.Load(int3(bloomCoord, 0)).rgb;
    }

    hdr = hdr * aoTerm + bloom;
    hdr *= exposure;
    hdr = ApplyWhiteBalance(hdr, whiteBalance);

    float3 mapped = ToneMapFilmic(hdr);

    // Contrast
    mapped = saturate((mapped - 0.5) * contrast + 0.5);

    // Saturation
    float luma = dot(mapped, float3(0.2126, 0.7152, 0.0722));
    mapped = lerp(luma.xxx, mapped, saturation);

    // Vignette
    float2 centerUV = (float2(id.xy) + 0.5) / float2(outWidth, outHeight);
    float d = distance(centerUV, float2(0.5, 0.5));
    float v = smoothstep(0.8, 0.8 - vignette * 0.5, d);
    mapped *= v;

    // Gamma to match existing LinearToSRGB() (approx).
    float3 srgb = pow(max(mapped, 0.0), 1.0 / 2.2);

    g_out[id.xy] = float4(srgb, 1.0);
}
