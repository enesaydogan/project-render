// shaders/tonemap_cs.hlsl
// Simple tonemap + gamma pass: linear HDR (FP16) -> display LDR (R10G10B10A2_UNORM)

Texture2D<float4> g_hdrInput : register(t0);
RWTexture2D<float4> g_out : register(u0);

cbuffer TonemapCB : register(b0)
{
    uint outWidth;
    uint outHeight;
    float exposure;
    float _pad;
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

    float3 hdr = g_hdrInput.Load(int3(id.xy, 0)).rgb;
    hdr *= exposure;

    float3 mapped = ToneMapFilmic(hdr);

    // Gamma to match existing LinearToSRGB() (approx).
    float3 srgb = pow(max(mapped, 0.0), 1.0 / 2.2);

    g_out[id.xy] = float4(srgb, 1.0);
}
