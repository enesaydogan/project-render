// shaders/raytracing/common.hlsli
// Common definitions shared across all raytracing shaders

#ifndef RAYTRACING_COMMON_H
#define RAYTRACING_COMMON_H

#include "../random_lib.hlsl"

static const float PI = 3.14159265359;

inline float3 sRGBToLinear(float3 sRGB) {
    return pow(max(sRGB, 0.0), 2.2);
}

inline float3 LinearToSRGB(float3 color) {
    return pow(max(color, 0.0), 1.0/2.2);
}

RaytracingAccelerationStructure g_accel : register(t0);
RWTexture2D<float4> g_output : register(u0);
RWTexture2D<float4> g_accumulation : register(u1);

// Streamline / DLSS inputs (u10+)
RWTexture2D<float> g_depth : register(u10);
RWTexture2D<float2> g_motionVectors : register(u11);
RWTexture2D<float4> g_albedoOut : register(u12);
RWTexture2D<float4> g_normalRoughnessOut : register(u13);
RWTexture2D<float4> g_specularAlbedo : register(u16);
RWTexture2D<float> g_specHitDistance : register(u17);
RWTexture2D<float2> g_specularMotionVectors : register(u18);

// Texture array - fixed large size to avoid overlap issues with other registers
Texture2D textures[2048] : register(t1);
// Environment Map (Latitude-Longitude) - Moved to Space 1 to avoid conflicts
Texture2D envMap : register(t0, space1);
SamplerState linearSampler : register(s0);

inline float2 DirectionToUV(float3 dir) {
    float2 uv;
    uv.x = atan2(dir.x, dir.z) / (2.0 * 3.14159265359) + 0.5;
    uv.y = acos(clamp(dir.y, -1.0, 1.0)) / 3.14159265359;
    return uv;
}

inline float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    return F0 + (max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

inline float3 ToneMap(float3 x) {
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

cbuffer Camera : register(b0)
{
    float3 camPos;
    float debugMode;
    float3 camForward;
    float jitterX;
    float3 camUp;
    float jitterY;
    float fov;
    float aspect;
    float nearZ;
    float farZ;
    float intensity;
    float globalFrameCount; // Monotonic frame count for RNG
    float lightCount;
    float maxSpecularBounces;
    float maxRefractiveBounces;
    float maxGIBounces;
    float maxSPP;
    float accumulationCount; // Count since last reset (0 if no accumulation)

    // Global Lighting
    float4 lightDir; // xyz = direction towards light
    float4 lightColor; // rgb + intensity in .w
    float4 ambientColor; // rgb + weight in .w

    // --- Streamline / DLSS history support ---
    float3 prevPos;
    float prevValid;
    float3 prevForward;
    float dlssEnabled;
    float3 prevUp;
    float dlssRayReconstruction;
    float prevFov;
    float prevAspect;
    float prevNearZ;
    float prevFarZ;
    float noiseThreshold;
    float useAdaptiveSampling;
    float debugVisualizationMode; // 0=None, 1=NoiseMap
    float _padNew;
}

struct MaterialData
{
    float4 diffuseColor;
    float4 reflectionColor;     // w = reflectionGlossiness
    float4 refractionColor;     // w = refractionGlossiness
    float4 emissiveColor;       // w = ior
    int4 textureIndices;        // x=diffuse, y=reflect, z=normal, w=refract
    int4 emissiveAndPad;        // x=emissive, y=occlusion, z=metalRough
    float4 extraParams;         // x=metalness, y=emissiveIntensity
    float4 archvizParams0;      // x=clearcoat, y=clearcoatRoughness, z=thinWalled, w=translucency
    float4 uvTransform;         // xy=uvScale, zw=uvOffset
    float4 triPlanarParams;     // x=enabled, y=scale, z=sharpness, w=normalStrength
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
    float3 color;     // Final computed color (for legacy/simple paths)
    float t;          // Hit distance (-1 for miss)
    float3 normal;    // World space normal
    float3 position;  // World space position
    float3 albedo;    // Material albedo
    float3 emissive;  // Emissive color
    float3 refractionColor;
    float ior;
    float roughness;
    float metalness;
    float thinWalled;     // 0/1 for thin glass/leaves
    float translucency;   // [0..1] diffuse-like transmission
    uint matIndex;
    uint rayDepth;        // 0 = primary, >0 = secondary
};

struct PathPayload
{
    float3 accumulatedColor;
    float3 throughput;
    float3 origin;
    float3 direction;
    bool active;
    RNG rng;
};

#endif // RAYTRACING_COMMON_H