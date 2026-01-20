// shaders/raytracing.hlsl

RaytracingAccelerationStructure g_accel : register(t0);
RWTexture2D<float4> g_output : register(u0);

struct RayPayload
{
    float4 color;
};

[shader("raygeneration")]
void RayGen()
{
    uint3 launchIndex = DispatchRaysIndex();
    uint3 launchDim = DispatchRaysDimensions();

    float2 uv = float2(launchIndex.xy) / float2(launchDim.xy);
    // Simple perspective projection setup (to be improved with Camera CB)
    float aspect = (float)launchDim.x / (float)launchDim.y;
    float2 d = uv * 2.0 - 1.0;
    d.x *= aspect;

    RayDesc ray;
    ray.Origin = float3(0, 0, 2.0); // Simple camera pos
    ray.Direction = normalize(float3(d.x, -d.y, -1.0));
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    
    RayPayload payload;
    payload.color = float4(0, 0, 0, 1);
    
    TraceRay(g_accel, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
    
    g_output[launchIndex.xy] = payload.color;
}

[shader("miss")]
void Miss(inout RayPayload payload)
{
    payload.color = float4(0.2, 0.2, 0.2, 1.0); // Dark grey background
}

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    float3 barycentrics = float3(1.0 - attr.barycentrics.x - attr.barycentrics.y, attr.barycentrics.x, attr.barycentrics.y);
    payload.color = float4(barycentrics, 1.0);
}
