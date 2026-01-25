// shaders/raytracing/common.hlsli
// Common definitions shared across all raytracing shaders

#ifndef RAYTRACING_COMMON_H
#define RAYTRACING_COMMON_H

RaytracingAccelerationStructure g_accel : register(t0);
RWTexture2D<float4> g_output : register(u0);

// Texture array - we'll bind multiple textures in a descriptor table
Texture2D textures[16] : register(t1);
SamplerState linearSampler : register(s0);

cbuffer Camera : register(b0)
{
    float3 camPos;
    float _pad0;
    float3 camForward;
    float _pad1;
    float3 camUp;
    float _pad2;
    float4 camParams; // fov, aspect, znear, zfar
}

struct RayPayload
{
    float4 color;
};

#endif // RAYTRACING_COMMON_H