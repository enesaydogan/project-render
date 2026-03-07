Texture2D<float4> g_denoisedDiffuseInput : register(t0);
Texture2D<float4> g_denoisedSpecularInput : register(t1);
Texture2D<float4> g_rawDiffuseInput : register(t2);
Texture2D<float4> g_rawSpecularInput : register(t3);
Texture2D<float4> g_stableInput : register(t4);
Texture2D<float4> g_albedoInput : register(t5);    // primary-hit diffuse albedo
Texture2D<float4> g_specAlbedoInput : register(t6); // primary-hit F_env (specular albedo)
RWTexture2D<float4> g_out : register(u0);

float Luminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint width, height;
    g_out.GetDimensions(width, height);
    if (id.x >= width || id.y >= height)
        return;

    float3 denoisedDiffuse = g_denoisedDiffuseInput.Load(int3(id.xy, 0)).rgb;
    float3 denoisedSpecular = g_denoisedSpecularInput.Load(int3(id.xy, 0)).rgb;
    float4 stable = g_stableInput.Load(int3(id.xy, 0));
    float3 albedo = g_albedoInput.Load(int3(id.xy, 0)).rgb;
    float3 specAlbedo = g_specAlbedoInput.Load(int3(id.xy, 0)).rgb;

    // Re-apply the same albedo clamps used in the demodulation step in the
    // path tracer shader so both round-trips are exact (no energy gain/loss).
    float3 demodAlbedo = max(albedo, float3(0.01f, 0.01f, 0.01f));
    float3 demodSpecAlbedo = max(specAlbedo, float3(0.01f, 0.01f, 0.01f));

    // Reconstruct: stable emission + denoised irradiance × albedo + denoised specular × F_env
    float3 color = max(stable.rgb + denoisedDiffuse * demodAlbedo + denoisedSpecular * demodSpecAlbedo, 0.0f.xxx);

    // Suppress warnings from unused raw buffers (kept in the root sig for
    // potential future use such as adaptive sharpening).
    (void)g_rawDiffuseInput;
    (void)g_rawSpecularInput;

    g_out[id.xy] = float4(color, 1.0f);
}
