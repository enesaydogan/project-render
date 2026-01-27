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

    // Build camera basis matching the raster renderer
    // f = 1.0 / tan(fov/2) to match raster projection
    float f_inv = tan(radians(fov) * 0.5);
    
    float3 R = normalize(cross(camForward, camUp)); // Right (F x U in RH)
    float3 U = normalize(cross(R, camForward));    // Up (orthonormal)
    
    // ndc.y is -1 at top of screen (uv.y=0). Invert it so top pixels look UP.
    float3 dir = normalize(ndc.x * R * aspect * f_inv + (-ndc.y) * U * f_inv + camForward);

    RayDesc ray;
    ray.Origin = camPos;
    ray.Direction = dir;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;

    RayPayload payload;
    payload.color = float4(0, 0, 0, 1);

#ifdef RAYGEN_DEBUG
    // Debug mode: output UV gradient to verify ray generation and output copy
    g_output[launchIndex.xy] = float4(uv.x, uv.y, 0.0, 1.0);
    return;
#endif

    TraceRay(g_accel, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);

    // Write to output. launchIndex.y=0 is the top dispatch, matching Row 0 (Top) of RT.
    g_output[launchIndex.xy] = payload.color;
}