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

    // Build camera basis
    float3 R = normalize(cross(camForward, camUp));
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
    uint outY = launchDim.y - 1 - launchIndex.y;
    g_output[int2(launchIndex.x, outY)] = payload.color;
}