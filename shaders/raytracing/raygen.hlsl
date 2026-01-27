// shaders/raytracing/raygen.hlsl
// Ray generation shader

#include "common.hlsli"

[shader("raygeneration")]
void RayGen()
{
    uint3 launchIndex = DispatchRaysIndex();
    uint3 launchDim = DispatchRaysDimensions();

    // Sample at pixel centers to match rasterization (add 0.5)
    float2 uv = (float2(launchIndex.xy) + 0.5) / float2(launchDim.xy);
    float2 ndc = uv * 2.0 - 1.0;

    float aspect = camParams[1]; // provided by host
    float fov = camParams[0];
    float f = tan(radians(fov) * 0.5);

    // Build camera basis robustly: if camForward is nearly parallel to camUp,
    // pick an alternate reference vector to avoid a zero-length cross product.
    float3 refUp = abs(camForward.y) > 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(0.0f, 1.0f, 0.0f);
    float3 R = cross(camForward, refUp);
    R = normalize(R);
    // If normalization produced NaNs (very unlikely after choosing refUp),
    // fall back to a safe axis.
    if (all(R == R) == false) { R = float3(1,0,0); }
    float3 U = normalize(cross(R, camForward));

    float3 dir = normalize(ndc.x * R * aspect * f + (-ndc.y) * U * f + camForward);

    RayDesc ray;
    ray.Origin = camPos;
    ray.Direction = dir;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;

    RayPayload payload;
    payload.color = float4(0, 0, 0, 1);

#ifdef RAYGEN_DEBUG
    // Debug mode: output UV gradient to verify ray generation and output copy
    // Write flipped vertically to match raster orientation (copy to RTV assumes top-left origin)
    uint outY = launchDim.y - 1 - launchIndex.y;
    g_output[int2(launchIndex.x, outY)] = float4(uv.x, uv.y, 0.0, 1.0);
    return;
#endif

    TraceRay(g_accel, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);

    // Flip Y when writing so DXR image matches raster output orientation
#ifndef RAYGEN_DEBUG
    uint outY = launchDim.y - 1 - launchIndex.y;
    g_output[int2(launchIndex.x, outY)] = payload.color;
#else
    // In debug mode `outY` already defined above
    g_output[int2(launchIndex.x, outY)] = payload.color;
#endif
}