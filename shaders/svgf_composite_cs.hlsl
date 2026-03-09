#define GROUP_SIZE_X 8
#define GROUP_SIZE_Y 8

Texture2D<float4> g_illumination : register(t0);
Texture2D<float4> g_albedo : register(t1);

RWTexture2D<float4> g_outputColor : register(u0);

float3 RemodulateByAlbedo(float3 illumination, float3 albedo)
{
    if (!any(albedo > 0.0))
        return illumination;

    float3 safeAlbedo = float3(
        (albedo.x > 0.0) ? albedo.x : 1.0,
        (albedo.y > 0.0) ? albedo.y : 1.0,
        (albedo.z > 0.0) ? albedo.z : 1.0);
    return illumination * safeAlbedo;
}

[numthreads(GROUP_SIZE_X, GROUP_SIZE_Y, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint width, height;
    g_outputColor.GetDimensions(width, height);
    if (dispatchThreadID.x >= width || dispatchThreadID.y >= height)
        return;

    int2 pixel = int2(dispatchThreadID.xy);
    float3 illumination = g_illumination.Load(int3(pixel, 0)).rgb;
    float3 albedo = g_albedo.Load(int3(pixel, 0)).rgb;
    g_outputColor[pixel] = float4(RemodulateByAlbedo(illumination, albedo), 1.0);
}
