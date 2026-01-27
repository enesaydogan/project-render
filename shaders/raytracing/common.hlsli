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
    float fov;
    float aspect;
    float nearZ;
    float farZ;
    float intensity;
}

struct MaterialData
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

// Use an SRV for materials in DXR to support multi-material indexing via InstanceID
StructuredBuffer<MaterialData> materials : register(t17);

struct Vertex {
    float3 position;
    float3 normal;
    float4 tangent;
    float2 uv;
};

// Offset other buffers to avoid overlap with textures and materials
StructuredBuffer<Vertex> vertices : register(t18);
StructuredBuffer<uint> indices : register(t19);

struct RayPayload
{
    float4 color;
};

#endif // RAYTRACING_COMMON_H