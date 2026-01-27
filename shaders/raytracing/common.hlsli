// shaders/raytracing/common.hlsli
// Common definitions shared across all raytracing shaders

#ifndef RAYTRACING_COMMON_H
#define RAYTRACING_COMMON_H

RaytracingAccelerationStructure g_accel : register(t0);
RWTexture2D<float4> g_output : register(u0);

// Texture array - fixed large size to avoid overlap issues with other registers
Texture2D textures[2048] : register(t1);
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
    float _pad3;
    float _pad4, _pad5;

    // Global Lighting
    float4 lightDir; // xyz = direction towards light
    float4 lightColor; // rgb + intensity in .w
    float4 ambientColor; // rgb + weight in .w
}

struct MaterialData
{
    float4 baseColorFactor;
    float4 params1; // x=metallic, y=roughness, z=workflow, w=unused
    float4 specular; // rgb specular for spec-gloss workflow
    float4 emissiveFactor; // rgb emissive
    int4 textureIndices; // x=baseColor, y=metallicRoughness, z=normal, w=occlusion
    int4 emissiveAndPad; // x=emissiveTexIndex, yzw=padding
};

// Use an SRV for materials in DXR to support multi-material indexing via InstanceID
StructuredBuffer<MaterialData> materials : register(t2049);

struct MeshData {
    int materialIndex;
    int vbIndex;
    int ibIndex;
    int pad;
};
StructuredBuffer<MeshData> meshData : register(t4098);

struct Vertex {
    float3 position;
    float3 normal;
    float4 tangent;
    float2 uv;
};

// Arrays of buffers for per-instance vertex/index data
StructuredBuffer<Vertex> vertices[1024] : register(t2050);
Buffer<uint> indices[1024] : register(t3074);

struct RayPayload
{
    float4 color;
};

#endif // RAYTRACING_COMMON_H