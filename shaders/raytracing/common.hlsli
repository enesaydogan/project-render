// shaders/raytracing/common.hlsli
// Common definitions shared across all raytracing shaders

#ifndef RAYTRACING_COMMON_H
#define RAYTRACING_COMMON_H

RaytracingAccelerationStructure g_accel : register(t0);
RWTexture2D<float4> g_output : register(u0);

// Texture array - we'll bind multiple textures in a descriptor table
Texture2D textures[8] : register(t1);
SamplerState linearSampler : register(s0);

cbuffer Camera : register(b0)
{
    float3 camPos;
    float _pad0;
    float3 camForward;
    float _pad1;
    float3 camUp;
    float _pad2;
    float camParams[5]; // fov, aspect, znear, zfar, intensity
}

cbuffer Material : register(b1)
{
    float4 baseColorFactor;
    float4 params1; // x=metallic, y=roughness, z=workflow, w=unused
    float4 specular; // rgb specular for spec-gloss workflow
    float4 emissiveFactor; // rgb emissive
    int4 textureIndices; // x=baseColor, y=metallicRoughness, z=normal, w=occlusion
    int4 emissiveAndPad; // x=emissiveTexIndex, yzw=padding
    float4 lightDir; // Direction of the light
    float4 lightColor; // Color of the light
};

struct Vertex {
    float3 position;
    float3 normal;
    float4 tangent;
    float2 uv;
};

StructuredBuffer<Vertex> vertices : register(t9);
StructuredBuffer<uint> indices : register(t10);

struct RayPayload
{
    float4 color;
};

#endif // RAYTRACING_COMMON_H