Texture2D<float4> g_rawNrdInput : register(t0);
Texture2D<float4> g_denoisedInput : register(t1);
RWTexture2D<float4> g_out : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint width, height;
    g_out.GetDimensions(width, height);
    if (id.x >= width || id.y >= height)
        return;

    float4 raw = g_rawNrdInput.Load(int3(id.xy, 0));
    float3 denoised = g_denoisedInput.Load(int3(id.xy, 0)).rgb;

    // Miss pixels (sky / clouds / background) should stay untouched. Feeding
    // them through the diffuse denoiser softens the baked sky too much.
    bool hasPrimarySurface = raw.a > 1e-6f;
    float3 color = hasPrimarySurface ? denoised : raw.rgb;

    g_out[id.xy] = float4(color, 1.0f);
}
