// shaders/sky_bake_cs.hlsl
// Compute shader to bake volumetric clouds into a latitude-longitude texture.

// Use the same CloudCB / noise textures from clouds.hlsl (space2).
// Full-quality cloud baker using the same RaymarchClouds implementation as runtime clouds.

#include "clouds.hlsl"

cbuffer CameraCB : register(b0)
{
    float3 pos;
    float debugMode;
    float3 forward;
    float _pad1;
    float3 up;
    float _pad2;
    float fov;
    float aspect;
    float nearZ;
    float farZ;
    float intensity;
    float globalFrameCount;
    float lightCount;
    float maxSpecularBounces;
    float maxRefractiveBounces;
    float maxGIBounces;
    float maxSPP;
    float accumulationCount;

    float4 lightDir;
    float4 lightColor;
};

RWTexture2D<float4> g_bakedSky : register(u0);

cbuffer CloudBakeCB : register(b11, space2)
{
    uint bakeStartY;
    uint bakeEndY;
    uint2 bakePad;
};

float3 LatLongToDirection(float2 uv) {
    float theta = (uv.x - 0.5) * (2.0 * 3.14159265359);
    float phi = uv.y * 3.14159265359;
    float sinPhi = sin(phi);
    return normalize(float3(sinPhi * sin(theta), cos(phi), sinPhi * cos(theta)));
}

float2 BakeSampleJitter(uint2 pix, uint sampleIndex)
{
    float2 seed = float2((float)pix.x, (float)pix.y);
    float x = CloudHash12(seed + float2(19.17, 3.91) + (float)sampleIndex * 7.13);
    float y = CloudHash12(seed + float2(-5.43, 27.29) + (float)sampleIndex * 11.71);
    return float2(x, y) - 0.5;
}

[numthreads(16,16,1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint2 dim;
    g_bakedSky.GetDimensions(dim.x, dim.y);
    uint targetY = id.y + bakeStartY;
    if (id.x >= dim.x || targetY >= dim.y || targetY >= bakeEndY) return;

    // Bake from current camera position so horizon/coverage match active view context.
    float3 rayOrigin = pos;

    int requestedSamples = (dim.x >= 2048)
        ? CloudCB.finalBakeSamples
        : CloudCB.previewBakeSamples;
    int sampleCount = clamp(requestedSamples, 1, 64);
    float2 texelSize = 1.0 / float2(dim);
    float jitterStrength = max(0.0, CloudCB.bakeJitterStrength);

    float3 radianceSum = 0.0;
    float transmittanceSum = 0.0;
    [loop]
    for (int sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
        uint2 pix = uint2(id.x, targetY);
        float2 jitter = (sampleCount > 1)
            ? BakeSampleJitter(pix, (uint)sampleIndex) * jitterStrength
            : float2(0.0, 0.0);
        float2 uv = (float2(pix) + 0.5 + jitter) * texelSize;
        uv = float2(frac(uv.x), saturate(uv.y));
        float3 dir = LatLongToDirection(uv);

        float4 cloudOut = RaymarchClouds(rayOrigin, dir, 0.0, 100000.0,
                                         normalize(lightDir.xyz),
                                         lightColor.rgb * lightColor.w);
        radianceSum += max(cloudOut.rgb, 0.0);
        transmittanceSum += saturate(cloudOut.a);
    }

    float invSamples = 1.0 / (float)sampleCount;
    g_bakedSky[uint2(id.x, targetY)] = float4(radianceSum * invSamples,
                                              saturate(transmittanceSum * invSamples));
}

