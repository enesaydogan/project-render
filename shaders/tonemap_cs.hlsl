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
    float ssaoEnabled; // mapped to _pad[0]
    float _pad[1];
};

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
    float ssao = g_ssaoTexture.Load(int3(id.xy, 0)).r;
    if (ssaoEnabled == 0.0) ssao = 1.0;

    float3 bloom = 0.0.xxx;
    if (bloomWidth > 0 && bloomHeight > 0)
    {
        float2 uv = (float2(id.xy) + 0.5) / float2(outWidth, outHeight);
        uint2 bloomCoord = min(uint2(uv * float2(bloomWidth, bloomHeight)),
                               uint2(bloomWidth - 1, bloomHeight - 1));
        bloom = g_bloomTexture.Load(int3(bloomCoord, 0)).rgb;
    }

    hdr = hdr * ssao + bloom;
    hdr *= exposure;

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
