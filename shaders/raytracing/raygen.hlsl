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

    // Build camera basis matching the raster renderer exactly (Left-Handed View Space)
    // f = 1.0 / tan(fov/2)
    float f_inv = tan(radians(fov) * 0.5);
    
    float3 forward = normalize(camForward);
    float3 R = normalize(cross(forward, camUp)); // Right
    float3 U = normalize(cross(R, forward));    // Up
    
    // In D3D NDC: x in [-1, 1], y in [-1, 1] (y=-1 is bottom, y=1 is top)
    // launchIndex.y=0 is top, so uv.y=0 -> ndc.y=-1.
    // We want uv.y=0 to map to y_view = +f_inv (Top).
    float y_view = (-ndc.y) * f_inv;
    float x_view = ndc.x * aspect * f_inv;
    
    float3 dir = normalize(x_view * R + y_view * U + forward);

    RayDesc ray;
    ray.Origin = camPos;
    ray.Direction = dir;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;

    RayPayload payload;
    payload.t = -1.0;
    payload.packedColor1 = 0u;
    PayloadSetColor(payload, float3(0.0, 0.0, 0.0));
    payload.packedNormal = PackNormalOctahedron(float3(0.0, 1.0, 0.0));
    payload.packedAlbedo = PackPayloadAlbedo(float3(0.0, 0.0, 0.0));
    payload.packedSurface = PackPayloadSurface(1.0, 0.0, 0.0, 0.0);
    payload.surface = float4(1.0, 0.0, 0.0, 0.0);
    payload.packedIorType = PackPayloadIorType(1.0, RAY_TYPE_PRIMARY, false, 1.0);
    payload.packedTransmission = PackPayloadTransmissionColor(float3(1.0, 1.0, 1.0));
    payload.packedSpecular = PackPayloadSpecularColor(float3(1.0, 1.0, 1.0));

#ifdef RAYGEN_DEBUG
    // Debug mode: output UV gradient to verify ray generation and output copy
    g_output[launchIndex.xy] = float4(uv.x, uv.y, 0.0, 1.0);
    return;
#endif

    TraceRay(g_accel, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);

    // Write to output. launchIndex.y=0 is the top dispatch, matching Row 0 (Top) of RT.
    g_output[launchIndex.xy] = float4(PayloadGetColor(payload), 1.0);
}
