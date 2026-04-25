// shaders/raytracing/common.hlsli
// Common definitions shared across all raytracing shaders

#ifndef RAYTRACING_COMMON_H
#define RAYTRACING_COMMON_H

#include "../random_lib.hlsl"

#ifndef SHADER_ENABLE_DEBUG
#define SHADER_ENABLE_DEBUG 1
#endif

static const float PI = 3.14159265359;

// Fast x^5 for Schlick Fresnel (avoids transcendental pow)
inline float pow5(float x) { float x2 = x * x; return x2 * x2 * x; }

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
RWTexture2D<float> g_linearDepth : register(u15);
RWTexture2D<float4> g_specularAlbedo : register(u16);
RWTexture2D<float> g_specHitDistance : register(u17);
RWTexture2D<float2> g_specularMotionVectors : register(u18);
RWTexture2D<float4> g_transmissionAccumulation : register(u19);
RWTexture2D<float> g_transmissionVariance : register(u20);

// === Shader instrumentation counters (debug) ===
static const uint SHADER_COUNTER_TRACE_RAYS = 0;
static const uint SHADER_COUNTER_SHADOW_TRACES = 1;
static const uint SHADER_COUNTER_SPECULAR_TRACES = 2;
static const uint SHADER_COUNTER_INDEX_LOADS = 3;      // index buffer loads
static const uint SHADER_COUNTER_VERTEX_FETCHES = 4;   // vertex loads
static const uint SHADER_COUNTER_TEXTURE_SAMPLES = 5;  // texture.SampleLevel calls
static const uint SHADER_COUNTER_RESERVOIR_READS = 6;  // reservoir reads
static const uint SHADER_COUNTER_RESERVOIR_WRITES = 7; // reservoir writes
static const uint SHADER_COUNTER_SPATIAL_NEIGHBOR_READS = 8;
static const uint SHADER_COUNTER_ENV_SAMPLES = 9;
static const uint SHADER_COUNTER_COUNT = 16; // allocated counters

// GPU-writable counters buffer (read back by host)
RWStructuredBuffer<uint> g_shaderCounters : register(u24);

#if SHADER_ENABLE_DEBUG
#define SHADER_DEBUG_MODE debugMode
#define SHADER_DEBUG_VIS_MODE debugVisualizationMode
#define SHADER_COUNTER_ADD(counterIndex, value) InterlockedAdd(g_shaderCounters[(counterIndex)], (value))
#else
#define SHADER_DEBUG_MODE 0.0
#define SHADER_DEBUG_VIS_MODE 0.0
#define SHADER_COUNTER_ADD(counterIndex, value) ((void)0)
#endif

// Texture array - fixed large size to avoid overlap issues with other registers
Texture2D textures[2048] : register(t1);
// Environment Map (Latitude-Longitude) - Moved to Space 1 to avoid conflicts
Texture2D envMap : register(t0, space1);
Texture2D<float4> envConditionalCdf : register(t1, space1);
Texture2D<float4> envMarginalCdf : register(t2, space1);
Texture2D bakedClouds : register(t12, space2); // Pre-baked lat-long cloud texture (rgb + transmittance)
SamplerState linearSampler : register(s0);

inline float2 DirectionToUV(float3 dir) {
    float2 uv;
    uv.x = atan2(dir.x, dir.z) / (2.0 * PI) + 0.5;
    uv.y = acos(clamp(dir.y, -1.0, 1.0)) / PI;
    return uv;
}

inline float3 UVToDirection(float2 uv) {
    float phi = (uv.x - 0.5) * 2.0 * PI;
    float theta = uv.y * PI;
    float sinTheta = sin(theta);

    float3 dir;
    dir.x = sinTheta * sin(phi);
    dir.y = cos(theta);
    dir.z = sinTheta * cos(phi);
    return normalize(dir);
}

inline float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    return F0 + (max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0) - F0) * pow5(saturate(1.0 - cosTheta));
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
    float cloudRenderingEnabled;
    float iblRotationDegrees;
    // 1 = compute env map CDF / pdf in solid-angle measure (luminance*sin(theta)).
    // 0 = use raw texel luminance (area) which is incorrect but useful for
    // comparisons/debugging.
    float sampleEnvSolidAngle;
    float exportRendering;
    float dxrProceduralSkyBoost;
    float tonemapAoIntensity;
    float tonemapAoRadiusMeters;
    float tonemapAoMode;
    float triPlanarWorldRotationDegrees;
}

inline float GetDxrProceduralSkyBoost()
{
    return max(dxrProceduralSkyBoost, 0.0);
}

inline float2 DirectionToUVRotated(float3 dir) {
    float2 uv = DirectionToUV(dir);
    uv.x = frac(uv.x + (iblRotationDegrees / 360.0));
    return uv;
}

// Material flags (bit-packed in MaterialData.pbrParams_flags.w as uint bits).
static const uint MATERIAL_FLAG_ALPHA_TESTED = 1u << 0;
static const uint MATERIAL_FLAG_THIN_WALLED  = 1u << 1;
static const uint MATERIAL_FLAG_TRANSLUCENT  = 1u << 2;
static const uint MATERIAL_FLAG_TRI_PLANAR   = 1u << 3;
static const uint MATERIAL_FLAG_UV_TRANSFORM = 1u << 4;
static const uint MATERIAL_FLAG_GLASS        = 1u << 5;
static const uint MATERIAL_FLAG_DOUBLE_SIDED = 1u << 6;
static const uint MATERIAL_FLAG_INVERT_ROUGHNESS = 1u << 7;
static const uint MATERIAL_FLAG_HAS_OPACITY_TEXTURE = 1u << 8;
static const uint MATERIAL_FLAG_HAS_SPECULAR_COLOR = 1u << 9;
static const uint MATERIAL_FLAG_HAS_VOLUME = 1u << 10;
static const uint MATERIAL_FLAG_HAS_COAT_NORMAL = 1u << 11;

struct MaterialData
{
    float4 baseColor_opacity;   // rgb + opacity
    float4 emissive_ior;        // rgb + ior
    float4 pbrParams_flags;     // x=metalness, y=roughness, z=transmission, w=flags (asfloat)
    uint4  packedTextures;      // 8x 16-bit indices packed as pairs
};

struct MaterialExtraData
{
    float4 coatLayerParams;     // x=coatWeight, y=coatRoughness, z=thinWalled, w=translucency
    float4 uvTransform;         // xy=uvScale, zw=uvOffset
    float4 triPlanarParams;     // x=enabled, y=scale, z=sharpness, w=normalStrength
    float4 mappingVariationParams; // x=mode, y=offsetJitter, z=randomRotation, w=colorVariation
    float4 triPlanarRotationParams; // xyz=materialRotationDegrees, w=stochasticMirror
    float4 shadingParams;       // x=emissiveIntensity, y=specWeight, z=alphaCutoff, w=isGrass
    float4 transmissionColor;   // rgb=tinted transmission color
    float4 textureWeight0;      // x=baseColor, y=packedSurface, z=metalness, w=roughnessGloss
    float4 textureWeight1;      // x=normal, y=occlusion, z=emissive, w=opacity
    uint4  extraPackedTextures; // x=coatNormal
    float4 volumeParams;        // x=thickness, y=attenuationDistance, z=thicknessTexAmount, w=coatIor
    float4 specularColor;       // rgb=specularColor, a=specularColorTexAmount
    float4 sheenColor;          // rgb=sheenColor
    float4 lobeParams;          // x=coatNormalAmount, y=anisotropy, z=anisoRotationDeg, w=sheenWeight
};

inline int UnpackTextureIndexLow(uint packedPair)
{
    uint v = packedPair & 0xFFFFu;
    return (v == 0xFFFFu) ? -1 : (int)v;
}

inline int UnpackTextureIndexHigh(uint packedPair)
{
    uint v = (packedPair >> 16) & 0xFFFFu;
    return (v == 0xFFFFu) ? -1 : (int)v;
}

// Use SRVs for materials in DXR to support multi-material indexing via InstanceID.
StructuredBuffer<MaterialData> materials : register(t2049);
StructuredBuffer<MaterialExtraData> materialExtras : register(t4099);

struct MeshData {
    int materialIndex;
    int vbIndex;
    int ibIndex;
    int pad;
};
StructuredBuffer<MeshData> meshData : register(t4098);
struct FGrassPatch
{
    float3 position;
    float scale;
    float3 normal;
    float yawRadians;
    float2 emitterUv;
    uint colorVariation;
    uint packedData;
};
StructuredBuffer<FGrassPatch> grassBlades : register(t4100);
cbuffer GrassRtParams : register(b11)
{
    uint grassTlasStartIndex;
}

struct Vertex {
    float3 position;
    float3 normal;
    float4 tangent;
    float2 uv;
};

// Arrays of buffers for per-instance vertex/index data
StructuredBuffer<Vertex> vertices[1024] : register(t2050);
Buffer<uint> indices[1024] : register(t3074);

#define RAY_TYPE_PRIMARY    0
#define RAY_TYPE_REFLECTION 1
#define RAY_TYPE_REFRACTION 2
#define RAY_TYPE_DIFFUSE    3
#define RAY_TYPE_SHADOW     4
#define RAY_TYPE_GI_EVAL    5

struct RayPayload
{
    float t;              // Hit distance (-1 for miss)
    uint packedColor0;    // R,G as fp16
    uint packedColor1;    // B as fp16 (low 16 bits)
    uint packedNormal;    // Octahedral packed normal
    uint packedAlbedo;    // 3x8 UNORM base color
    uint packedSurface;   // 4x8 UNORM: roughness/metallic/transmission/translucency
    uint packedIorType;   // 16-bit half IOR + 8-bit rayType + thin-walled bit
    uint packedTransmission; // 3x8 UNORM transmission color
    uint packedSpecular;  // 3x8 UNORM specular color
};

inline uint PackNormalOctahedron(float3 n)
{
    n = normalize(n);
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    float2 p = n.xy;
    if (n.z < 0.0) {
        float2 signNotZero = float2((p.x >= 0.0) ? 1.0 : -1.0,
                                    (p.y >= 0.0) ? 1.0 : -1.0);
        p = (1.0 - abs(p.yx)) * signNotZero;
    }
    uint2 enc = (uint2)round(saturate(p * 0.5 + 0.5) * 65535.0);
    return (enc.y << 16) | enc.x;
}

inline float3 UnpackNormalOctahedron(uint packed)
{
    float2 p = float2((packed & 0xFFFFu) / 65535.0,
                      ((packed >> 16) & 0xFFFFu) / 65535.0) * 2.0 - 1.0;
    float3 n = float3(p.x, p.y, 1.0 - abs(p.x) - abs(p.y));
    if (n.z < 0.0) {
        float2 signNotZero = float2((n.x >= 0.0) ? 1.0 : -1.0,
                                    (n.y >= 0.0) ? 1.0 : -1.0);
        n.xy = (1.0 - abs(n.yx)) * signNotZero;
    }
    return normalize(n);
}

inline void PayloadSetColor(inout RayPayload p, float3 c)
{
    uint3 h = uint3(f32tof16(max(c, 0.0)));
    p.packedColor0 = (h.x & 0xFFFFu) | ((h.y & 0xFFFFu) << 16);
    p.packedColor1 = (p.packedColor1 & 0xFFFF0000u) | (h.z & 0xFFFFu);
}

inline float3 PayloadGetColor(RayPayload p)
{
    uint hr = p.packedColor0 & 0xFFFFu;
    uint hg = (p.packedColor0 >> 16) & 0xFFFFu;
    uint hb = p.packedColor1 & 0xFFFFu;
    return max(float3(f16tof32(hr), f16tof32(hg), f16tof32(hb)), 0.0);
}

inline void PayloadSetCoatRoughness(inout RayPayload p, float coatRoughness)
{
    uint h = f32tof16(saturate(coatRoughness)) & 0xFFFFu;
    p.packedColor1 = (p.packedColor1 & 0x0000FFFFu) | (h << 16);
}

inline float PayloadGetCoatRoughness(RayPayload p)
{
    uint h = (p.packedColor1 >> 16) & 0xFFFFu;
    return saturate(f16tof32(h));
}

inline uint PackPayloadAlbedo(float3 c)
{
    uint3 q = (uint3)round(saturate(c) * 255.0);
    return (q.x) | (q.y << 8) | (q.z << 16);
}

inline uint PackPayloadAlbedoCoat(float3 c, float coatWeight)
{
    uint3 q = (uint3)round(saturate(c) * 255.0);
    uint coat = (uint)round(saturate(coatWeight) * 255.0);
    return (q.x) | (q.y << 8) | (q.z << 16) | (coat << 24);
}

inline float3 UnpackPayloadAlbedo(uint packed)
{
    float3 q = float3(
        packed & 0xFFu,
        (packed >> 8) & 0xFFu,
        (packed >> 16) & 0xFFu);
    return q / 255.0;
}

inline float UnpackPayloadCoatWeight(uint packed)
{
    return ((packed >> 24) & 0xFFu) / 255.0;
}

inline uint PackPayloadSurface(float roughness, float metallic,
                               float transmission, float translucency)
{
    uint4 q = (uint4)round(saturate(float4(roughness, metallic, transmission, translucency)) * 255.0);
    return (q.x) | (q.y << 8) | (q.z << 16) | (q.w << 24);
}

inline float4 UnpackPayloadSurface(uint packed)
{
    float4 q = float4(
        (packed) & 0xFFu,
        (packed >> 8) & 0xFFu,
        (packed >> 16) & 0xFFu,
        (packed >> 24) & 0xFFu);
    return q / 255.0;
}

inline uint PackPayloadIorType(float ior, uint rayType, bool thinWalled, float specularWeight)
{
    uint hIor = f32tof16(clamp(ior, 1.0, 3.0)) & 0xFFFFu;
    uint thin = thinWalled ? (1u << 24) : 0u;
    uint spec = (((uint)round(saturate(specularWeight) * 127.0)) & 0x7Fu) << 25;
    return hIor | ((rayType & 0xFFu) << 16) | thin | spec;
}

inline float UnpackPayloadIor(uint packed)
{
    return max(1.0, f16tof32(packed & 0xFFFFu));
}

inline uint UnpackPayloadRayType(uint packed)
{
    return (packed >> 16) & 0xFFu;
}

inline bool UnpackPayloadThinWalled(uint packed)
{
    return (packed & (1u << 24)) != 0;
}

inline float UnpackPayloadSpecularWeight(uint packed)
{
    return ((packed >> 25) & 0x7Fu) / 127.0;
}

inline uint PackPayloadTransmissionColor(float3 c)
{
    return PackPayloadAlbedo(c);
}

inline float3 UnpackPayloadTransmissionColor(uint packed)
{
    return UnpackPayloadAlbedo(packed);
}

inline uint PackPayloadSpecularColorThickness(float3 c, float thickness)
{
    uint3 q = (uint3)round(saturate(c) * 255.0);
    uint t = (uint)round(sqrt(saturate(thickness)) * 255.0);
    return (q.x) | (q.y << 8) | (q.z << 16) | (t << 24);
}

inline uint PackPayloadSpecularColor(float3 c)
{
    return PackPayloadSpecularColorThickness(c, 0.0);
}

inline float3 UnpackPayloadSpecularColor(uint packed)
{
    return UnpackPayloadAlbedo(packed);
}

inline float UnpackPayloadThickness(uint packed)
{
    float t = ((packed >> 24) & 0xFFu) / 255.0;
    return t * t;
}

#endif // RAYTRACING_COMMON_H
