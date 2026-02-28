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

float3 LatLongToDirection(float2 uv) {
    float theta = (uv.x - 0.5) * (2.0 * 3.14159265359);
    float phi = uv.y * 3.14159265359;
    float sinPhi = sin(phi);
    return normalize(float3(sinPhi * sin(theta), cos(phi), sinPhi * cos(theta)));
}

[numthreads(16,16,1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint2 dim;
    g_bakedSky.GetDimensions(dim.x, dim.y);
    if (id.x >= dim.x || id.y >= dim.y) return;

    float2 uv = (float2(id.xy) + 0.5) / float2(dim);
    float3 dir = LatLongToDirection(uv);

    // Bake from current camera position so horizon/coverage match active view context.
    float3 rayOrigin = pos;

    // Use the exact volumetric march path used elsewhere.
    float4 cloudOut = RaymarchClouds(rayOrigin, dir, 0.0, 100000.0,
                                     normalize(lightDir.xyz),
                                     lightColor.rgb * lightColor.w);
    cloudOut.a = saturate(cloudOut.a);
    cloudOut.rgb = max(cloudOut.rgb, 0.0);
    g_bakedSky[id.xy] = cloudOut;
}

