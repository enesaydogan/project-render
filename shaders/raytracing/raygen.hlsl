// shaders/raytracing/raygen.hlsl
// Ray generation shader

#include "common.hlsli"

[shader("raygeneration")]
void RayGen()
{
    uint3 launchIndex = DispatchRaysIndex();
    uint3 launchDim = DispatchRaysDimensions();

    float2 uv = float2(launchIndex.xy) / float2(launchDim.xy);
    float2 ndc = uv * 2.0 - 1.0;

    float aspect = camParams.y; // provided by host
    float fov = camParams.x;
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

    TraceRay(g_accel, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);

    g_output[launchIndex.xy] = payload.color;
}