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
RWStructuredBuffer<uint> g_wavefrontShadowContribution : register(u23);

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
    float debugVisualizationMode; // 0=None, 1=NoiseMap, 2=ClayMaterialOverride
    float cloudRenderingEnabled;
    float iblRotationDegrees;
    // 1 = compute env map CDF / pdf in solid-angle measure (luminance*sin(theta)).
    // 0 = use raw texel luminance (area) which is incorrect but useful for
    // comparisons/debugging.
    float sampleEnvSolidAngle;
    float exportRendering;
    float dxrProceduralSkyBoost;
    float iblIndirectBoost;
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

#include "../lights_lib.hlsl"

StructuredBuffer<Light> g_lights : register(t5000);

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

inline uint WavefrontGetPathRayType(uint packedState)
{
    return packedState & 0xFFu;
}

inline uint WavefrontGetSpecularBounceCount(uint packedState)
{
    return (packedState >> 8) & 0xFFu;
}

inline uint WavefrontGetRefractiveBounceCount(uint packedState)
{
    return (packedState >> 16) & 0xFFu;
}

inline uint WavefrontGetDiffuseBounceCount(uint packedState)
{
    return (packedState >> 24) & 0xFFu;
}

inline uint WavefrontBuildPackedState(uint rayType,
                                      uint specularBounceCount,
                                      uint refractiveBounceCount,
                                      uint diffuseBounceCount)
{
    return (rayType & 0xFFu) |
           ((specularBounceCount & 0xFFu) << 8) |
           ((refractiveBounceCount & 0xFFu) << 16) |
           ((diffuseBounceCount & 0xFFu) << 24);
}

inline bool WavefrontHasBounceBudget(uint packedState,
                                     uint nextRayType,
                                     uint maxSpecularBounceCount,
                                     uint maxRefractiveBounceCount,
                                     uint maxDiffuseBounceCount)
{
    if (nextRayType == RAY_TYPE_REFLECTION) {
        return WavefrontGetSpecularBounceCount(packedState) < maxSpecularBounceCount;
    }
    if (nextRayType == RAY_TYPE_REFRACTION) {
        return WavefrontGetRefractiveBounceCount(packedState) < maxRefractiveBounceCount;
    }
    if (nextRayType == RAY_TYPE_DIFFUSE) {
        return WavefrontGetDiffuseBounceCount(packedState) < maxDiffuseBounceCount;
    }
    return false;
}

inline uint WavefrontAdvancePackedState(uint packedState, uint nextRayType)
{
    uint specularBounceCount = WavefrontGetSpecularBounceCount(packedState);
    uint refractiveBounceCount = WavefrontGetRefractiveBounceCount(packedState);
    uint diffuseBounceCount = WavefrontGetDiffuseBounceCount(packedState);

    if (nextRayType == RAY_TYPE_REFLECTION) {
        specularBounceCount = min(specularBounceCount + 1u, 255u);
    } else if (nextRayType == RAY_TYPE_REFRACTION) {
        refractiveBounceCount = min(refractiveBounceCount + 1u, 255u);
    } else if (nextRayType == RAY_TYPE_DIFFUSE) {
        diffuseBounceCount = min(diffuseBounceCount + 1u, 255u);
    }

    return WavefrontBuildPackedState(nextRayType,
                                     specularBounceCount,
                                     refractiveBounceCount,
                                     diffuseBounceCount);
}

struct WavefrontPathState
{
    float3 origin;
    uint pixelIndex;
    float3 direction;
    uint rngState;
    float3 throughput;
    uint packedState;
};

static const uint WAVEFRONT_ABI_VERSION = 3u;
static const uint WAVEFRONT_PATH_STATE_DWORDS = 12u;
static const uint WAVEFRONT_HIT_RECORD_DWORDS = 28u;
static const uint WAVEFRONT_SHADOW_TASK_DWORDS = 12u;
static const uint WAVEFRONT_DISPATCH_ARGS_DWORDS = 4u;
static const uint WAVEFRONT_QUEUE_PATH_A = 0u;
static const uint WAVEFRONT_QUEUE_PRIMARY_ACTIVE = 1u;
static const uint WAVEFRONT_QUEUE_PRIMARY_HIT = 2u;
static const uint WAVEFRONT_QUEUE_PRIMARY_MISS = 3u;
static const uint WAVEFRONT_QUEUE_PATH_B = 4u;
static const uint WAVEFRONT_QUEUE_SHADOW = 5u;

struct WavefrontHitRecord
{
    float hitT;
    uint pixelIndex;
    uint packedColor0;
    uint packedColor1;
    uint packedNormal;
    uint packedAlbedo;
    uint packedSurface;
    uint packedIorType;
    uint packedTransmission;
    uint packedSpecular;
    uint packedState;
    uint reserved;
    float3 guideOrigin;
    uint guidePackedState;
    float3 guideDirection;
    float guideHitT;
    uint guidePackedNormal;
    uint guidePackedAlbedo;
    uint guidePackedSurface;
    uint guidePackedIorType;
    uint guidePackedTransmission;
    uint guidePackedSpecular;
    uint guideReserved0;
    uint guideReserved1;
};

static const uint WAVEFRONT_HIT_STATE_MISS = 0x80000000u;
static const uint WAVEFRONT_GUIDE_STATE_MISS = 0x80000000u;
static const uint WAVEFRONT_GUIDE_STATE_THROUGH_TRANSMISSION = 0x40000000u;
static const uint WAVEFRONT_MATERIAL_BIN_DIFFUSE = 0u;
static const uint WAVEFRONT_MATERIAL_BIN_GLOSSY_DIELECTRIC = 1u;
static const uint WAVEFRONT_MATERIAL_BIN_CONDUCTOR = 2u;
static const uint WAVEFRONT_MATERIAL_BIN_DELTA_REFLECTION = 3u;
static const uint WAVEFRONT_MATERIAL_BIN_REFRACTION = 4u;
static const uint WAVEFRONT_MATERIAL_BIN_EMISSIVE = 5u;
static const uint WAVEFRONT_MATERIAL_BIN_TRANSLUCENT = 6u;
static const uint WAVEFRONT_MATERIAL_BIN_COUNT = 7u;
static const uint WAVEFRONT_PRIMARY_MATERIAL_BIN_STATS_BASE = 32u;
static const uint WAVEFRONT_SECONDARY_MATERIAL_BIN_STATS_BASE = 40u;

struct WavefrontShadowTask
{
    float3 origin;
    float maxDistance;
    float3 direction;
    uint packedLightIndex;
    float3 throughput;
    uint packedState;
};

struct WavefrontDispatchArgs
{
    uint groupCountX;
    uint groupCountY;
    uint groupCountZ;
    uint activeCount;
};

RWStructuredBuffer<uint> g_wavefrontQueueCounters : register(u25);
RWStructuredBuffer<WavefrontPathState> g_wavefrontPathQueueA : register(u26);
RWStructuredBuffer<WavefrontPathState> g_wavefrontPathQueueB : register(u27);
RWStructuredBuffer<WavefrontHitRecord> g_wavefrontHitQueue : register(u28);
RWStructuredBuffer<WavefrontShadowTask> g_wavefrontShadowQueue : register(u29);
RWStructuredBuffer<WavefrontDispatchArgs> g_wavefrontDispatchArgs : register(u30);
RWStructuredBuffer<uint> g_wavefrontStats : register(u31);
RWStructuredBuffer<uint4> g_wavefrontReserved : register(u32);
RWStructuredBuffer<uint> g_wavefrontMaterialBinIndices : register(u33);

static const uint WAVEFRONT_RESERVED_SECONDARY_DISPATCH_CONFIG_INDEX = 6u;
static const uint WAVEFRONT_QUEUE_FLAG_SOURCE_IS_A = 0x1u;
static const uint WAVEFRONT_QUEUE_FLAG_FILTER_DIFFUSE = 0x2u;
static const uint WAVEFRONT_QUEUE_FLAG_FILTER_SPECULAR = 0x4u;
static const uint WAVEFRONT_QUEUE_FLAG_USE_MATERIAL_BIN_LIST = 0x8u;
static const uint WAVEFRONT_QUEUE_FLAG_MISS_ONLY = 0x10u;
static const uint WAVEFRONT_MATERIAL_BIN_COUNTER_BASE = 6u;
static const uint WAVEFRONT_QUEUE_FLAG_MATERIAL_BIN_SHIFT = 8u;
static const uint WAVEFRONT_RESOLVE_FLAG_PRIMARY_SURFACE_ONLY = 0x10000u;
static const uint WAVEFRONT_LIGHT_SAMPLE_DIRECTIONAL = 0u;
static const uint WAVEFRONT_LIGHT_SAMPLE_FLAT = 1u;
static const uint WAVEFRONT_LIGHT_SAMPLE_ENV = 2u;
static const uint WAVEFRONT_LIGHT_SAMPLER_FLAT = 0u;

struct WavefrontLightSample
{
    float3 direction;
    float maxDistance;
    float3 radiance;
    uint packedLightIndex;
};

struct WavefrontLightSamplerContext
{
    uint mode;
    uint availableLights;
};

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

inline float3 UnpackPayloadColorWords(uint packedColor0, uint packedColor1)
{
    uint hr = packedColor0 & 0xFFFFu;
    uint hg = (packedColor0 >> 16) & 0xFFFFu;
    uint hb = packedColor1 & 0xFFFFu;
    return max(float3(f16tof32(hr), f16tof32(hg), f16tof32(hb)), 0.0);
}

inline float3 WavefrontHitRecordGetColor(WavefrontHitRecord record)
{
    return UnpackPayloadColorWords(record.packedColor0, record.packedColor1);
}

inline bool WavefrontHitRecordIsMiss(WavefrontHitRecord record)
{
    return (record.packedState & WAVEFRONT_HIT_STATE_MISS) != 0u ||
           record.hitT < 0.0;
}

inline bool WavefrontHitRecordGuideIsMiss(WavefrontHitRecord record)
{
    return (record.guidePackedState & WAVEFRONT_GUIDE_STATE_MISS) != 0u ||
           record.guideHitT < 0.0;
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

inline uint WavefrontClassifyMaterialBin(uint packedSurface,
                                         uint packedIorType,
                                         uint packedColor0,
                                         uint packedColor1)
{
    float4 surface = UnpackPayloadSurface(packedSurface);
    float roughness = saturate(surface.x);
    float metallic = saturate(surface.y);
    float transmission = saturate(surface.z);
    float translucency = saturate(surface.w);
    float3 emissive = UnpackPayloadColorWords(packedColor0, packedColor1);
    bool hasEmissive = max(emissive.r, max(emissive.g, emissive.b)) > 1.0e-4;

    if (transmission > 0.05) {
        return WAVEFRONT_MATERIAL_BIN_REFRACTION;
    }
    if (hasEmissive) {
        return WAVEFRONT_MATERIAL_BIN_EMISSIVE;
    }
    if (translucency > 0.1) {
        return WAVEFRONT_MATERIAL_BIN_TRANSLUCENT;
    }
    if (roughness < 0.08 && metallic > 0.45) {
        return WAVEFRONT_MATERIAL_BIN_DELTA_REFLECTION;
    }
    if (metallic > 0.45) {
        return WAVEFRONT_MATERIAL_BIN_CONDUCTOR;
    }
    if (roughness < 0.3) {
        return WAVEFRONT_MATERIAL_BIN_GLOSSY_DIELECTRIC;
    }
    return WAVEFRONT_MATERIAL_BIN_DIFFUSE;
}

inline uint WavefrontPackMaterialSortKey(uint rayType,
                                         uint materialBin,
                                         uint flags)
{
    return ((rayType & 0xFFu) << 16) | ((materialBin & 0xFFu) << 8) |
           (flags & 0xFFu);
}

inline uint WavefrontGetMaterialBinFromSortKey(uint sortKey)
{
    return (sortKey >> 8) & 0xFFu;
}

inline uint WavefrontGetMaterialBinFromQueueFlags(uint flags)
{
    return (flags >> WAVEFRONT_QUEUE_FLAG_MATERIAL_BIN_SHIFT) & 0xFFu;
}

inline uint WavefrontBuildMaterialBinQueueFlags(uint materialBin)
{
    return WAVEFRONT_QUEUE_FLAG_USE_MATERIAL_BIN_LIST |
           ((materialBin & 0xFFu) << WAVEFRONT_QUEUE_FLAG_MATERIAL_BIN_SHIFT);
}

inline void WavefrontAccumulateMaterialBinStat(uint statsBase, uint sortKey)
{
    uint materialBin = WavefrontGetMaterialBinFromSortKey(sortKey);
    if (materialBin < WAVEFRONT_MATERIAL_BIN_COUNT) {
        uint previousValue = 0u;
        InterlockedAdd(g_wavefrontStats[statsBase + materialBin], 1u,
                       previousValue);
    }
}

inline void WavefrontCompactMaterialBinIndex(uint statsBase,
                                             uint sortKey,
                                             uint recordIndex)
{
    uint materialBin = WavefrontGetMaterialBinFromSortKey(sortKey);
    if (materialBin >= WAVEFRONT_MATERIAL_BIN_COUNT) {
        return;
    }

    uint binIndexCapacity = 0u;
    uint binIndexStride = 0u;
    g_wavefrontMaterialBinIndices.GetDimensions(binIndexCapacity,
                                                binIndexStride);
    const uint perBinCapacity = binIndexCapacity / WAVEFRONT_MATERIAL_BIN_COUNT;
    if (perBinCapacity == 0u) {
        return;
    }

    uint binOffset = 0u;
    InterlockedAdd(
        g_wavefrontQueueCounters[WAVEFRONT_MATERIAL_BIN_COUNTER_BASE +
                                 materialBin],
        1u, binOffset);
    uint previousValue = 0u;
    InterlockedAdd(g_wavefrontStats[statsBase + materialBin], 1u,
                   previousValue);
    if (binOffset < perBinCapacity) {
        g_wavefrontMaterialBinIndices[materialBin * perBinCapacity +
                                      binOffset] = recordIndex;
    } else {
        uint previousValue = 0u;
        InterlockedAdd(g_wavefrontStats[49], 1u, previousValue);
    }
}

inline uint WavefrontPackLightSampleMetadata(uint lightType, uint lightIndex)
{
    return ((lightType & 0xFFu) << 24) | (lightIndex & 0x00FFFFFFu);
}

inline uint WavefrontGetLightSampleType(uint packedLightIndex)
{
    return (packedLightIndex >> 24) & 0xFFu;
}

inline uint WavefrontGetLightSampleIndex(uint packedLightIndex)
{
    return packedLightIndex & 0x00FFFFFFu;
}

inline uint WavefrontGetAvailableLightCount()
{
    // The wavefront compute passes bind g_lights as a root SRV. On that path
    // the shader-visible element count is not a reliable source of truth, so
    // match the legacy tracer and use the CPU-authored camera constant.
    return (uint)max(lightCount, 0.0);
}

inline WavefrontLightSamplerContext WavefrontCreateLightSampler(float3 surfacePos)
{
    WavefrontLightSamplerContext sampler;
    sampler.mode = WAVEFRONT_LIGHT_SAMPLER_FLAT;
    sampler.availableLights = WavefrontGetAvailableLightCount();
    return sampler;
}

inline WavefrontLightSample WavefrontSampleDirectionalLight(float sampleWeight)
{
    WavefrontLightSample sample;
    sample.direction = normalize(lightDir.xyz);
    if (dot(sample.direction, sample.direction) < 1.0e-6) {
        sample.direction = float3(0.0, 1.0, 0.0);
    }
    sample.maxDistance = 1000.0;
    sample.radiance = lightColor.rgb * lightColor.w * sampleWeight;
    sample.packedLightIndex =
        WavefrontPackLightSampleMetadata(WAVEFRONT_LIGHT_SAMPLE_DIRECTIONAL, 0u);
    return sample;
}

inline bool WavefrontIsAreaLightType(uint lightType)
{
    return lightType == LIGHT_TYPE_AREA_RECT ||
           lightType == LIGHT_TYPE_AREA_DISK;
}

inline WavefrontLightSample WavefrontSampleFlatLight(float3 surfacePos,
                                                     uint lightIndex,
                                                     float sampleWeight)
{
    WavefrontLightSample sample;
    const uint availableLights = WavefrontGetAvailableLightCount();
    if (lightIndex >= availableLights) {
        sample.direction = float3(0.0, 1.0, 0.0);
        sample.maxDistance = 0.0;
        sample.radiance = float3(0.0, 0.0, 0.0);
        sample.packedLightIndex =
            WavefrontPackLightSampleMetadata(WAVEFRONT_LIGHT_SAMPLE_FLAT, 0u);
        return sample;
    }
    Light light = g_lights[lightIndex];
    LightSample lightSample = evaluate_light(light, surfacePos);
    sample.direction = lightSample.L;
    sample.maxDistance = lightSample.dist;
    sample.radiance = lightSample.radiance * sampleWeight;
    sample.packedLightIndex =
        WavefrontPackLightSampleMetadata(WAVEFRONT_LIGHT_SAMPLE_FLAT, lightIndex);
    return sample;
}

inline WavefrontLightSample WavefrontSampleFlatLight(float3 surfacePos,
                                                     uint lightIndex,
                                                     float sampleWeight,
                                                     inout RNG rng)
{
    WavefrontLightSample sample;
    const uint availableLights = WavefrontGetAvailableLightCount();
    if (lightIndex >= availableLights) {
        sample.direction = float3(0.0, 1.0, 0.0);
        sample.maxDistance = 0.0;
        sample.radiance = float3(0.0, 0.0, 0.0);
        sample.packedLightIndex =
            WavefrontPackLightSampleMetadata(WAVEFRONT_LIGHT_SAMPLE_FLAT, 0u);
        return sample;
    }

    Light light = g_lights[lightIndex];
    if (!WavefrontIsAreaLightType(light.type)) {
        return WavefrontSampleFlatLight(surfacePos, lightIndex, sampleWeight);
    }

    LightSample lightSample = sample_area_light(light, surfacePos,
                                                next_float2(rng));
    const float pdfInv = rcp(max(lightSample.pdf, 1.0e-6));
    sample.direction = lightSample.L;
    sample.maxDistance = lightSample.dist;
    sample.radiance = lightSample.radiance * sampleWeight * pdfInv;
    sample.packedLightIndex =
        WavefrontPackLightSampleMetadata(WAVEFRONT_LIGHT_SAMPLE_FLAT, lightIndex);
    return sample;
}

inline WavefrontLightSample WavefrontSampleFlatLightUnweighted(
    float3 surfacePos,
    uint lightIndex,
    inout RNG rng)
{
    WavefrontLightSample sample;
    const uint availableLights = WavefrontGetAvailableLightCount();
    if (lightIndex >= availableLights) {
        sample.direction = float3(0.0, 1.0, 0.0);
        sample.maxDistance = 0.0;
        sample.radiance = float3(0.0, 0.0, 0.0);
        sample.packedLightIndex =
            WavefrontPackLightSampleMetadata(WAVEFRONT_LIGHT_SAMPLE_FLAT, 0u);
        return sample;
    }

    Light light = g_lights[lightIndex];
    if (!WavefrontIsAreaLightType(light.type)) {
        return WavefrontSampleFlatLight(surfacePos, lightIndex, 1.0);
    }

    LightSample lightSample = sample_area_light(light, surfacePos,
                                                next_float2(rng));
    sample.direction = lightSample.L;
    sample.maxDistance = lightSample.dist;
    sample.radiance = lightSample.radiance;
    sample.packedLightIndex =
        WavefrontPackLightSampleMetadata(WAVEFRONT_LIGHT_SAMPLE_FLAT, lightIndex);
    return sample;
}

inline WavefrontLightSample WavefrontSampleDirectLight(
    WavefrontLightSamplerContext sampler,
    float3 surfacePos,
    inout RNG rng)
{
    const uint numLights = sampler.availableLights;
    if (numLights == 0u || next_float(rng) < 0.5) {
        return WavefrontSampleDirectionalLight((numLights > 0u) ? 2.0 : 1.0);
    }

    const uint lightIndex = next_uint(rng) % numLights;
    return WavefrontSampleFlatLight(surfacePos, lightIndex,
                                    2.0 * (float)numLights, rng);
}

inline WavefrontLightSample WavefrontSampleDirectLight(float3 surfacePos,
                                                       inout RNG rng)
{
    WavefrontLightSamplerContext sampler =
        WavefrontCreateLightSampler(surfacePos);
    return WavefrontSampleDirectLight(sampler, surfacePos, rng);
}

inline float3 WavefrontEvaluateEnvironmentRadiance(float3 direction,
                                                   float3 surfacePos)
{
    float pathDistance = max(length(surfacePos - camPos), 1.0e-3);
    float envLod = clamp(log2(pathDistance * 0.02) + 0.35, 0.0, 10.0);
    float2 uv = DirectionToUVRotated(normalize(direction));
    return envMap.SampleLevel(linearSampler, uv, envLod).rgb *
           GetDxrProceduralSkyBoost();
}

inline float3 WavefrontEvaluateShadowTaskRadiance(uint packedLightIndex,
                                                  float3 surfacePos,
                                                  float3 direction)
{
    const uint lightType = WavefrontGetLightSampleType(packedLightIndex);
    if (lightType == WAVEFRONT_LIGHT_SAMPLE_ENV) {
        return WavefrontEvaluateEnvironmentRadiance(direction, surfacePos);
    }
    return float3(0.0, 0.0, 0.0);
}

inline uint PackPayloadIorType(float ior, uint rayType, bool thinWalled, float specularWeight)
{
    uint hIor = f32tof16(clamp(ior, 1.0, 10.0)) & 0xFFFFu;
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

inline RayPayload InitRayPayload(uint rayType)
{
    RayPayload p;
    p.t = -1.0;
    p.packedColor1 = 0u;
    PayloadSetColor(p, float3(0.0, 0.0, 0.0));
    p.packedNormal = PackNormalOctahedron(float3(0.0, 1.0, 0.0));
    p.packedAlbedo = PackPayloadAlbedo(float3(0.0, 0.0, 0.0));
    p.packedSurface = PackPayloadSurface(1.0, 0.0, 0.0, 0.0);
    p.packedIorType = PackPayloadIorType(1.0, rayType, false, 1.0);
    p.packedTransmission =
        PackPayloadTransmissionColor(float3(1.0, 1.0, 1.0));
    p.packedSpecular = PackPayloadSpecularColor(float3(1.0, 1.0, 1.0));
    return p;
}

inline float DielectricF0FromIor(float ior)
{
    float safeIor = max(ior, 1.0 + 1e-4);
    float f0 = (safeIor - 1.0) / (safeIor + 1.0);
    return f0 * f0;
}

inline float GlassScatterAlpha(float roughness)
{
    float r = saturate(roughness);
    return r * r;
}

inline bool IsDeltaGlass(float roughness)
{
    return GlassScatterAlpha(roughness) <= 1.5e-6;
}

inline bool IsDeltaSpecular(float roughness)
{
    return GlassScatterAlpha(roughness) <= 1.5e-6;
}

inline bool ShouldResolveDeltaTransmission(float roughness,
                                           float transmission,
                                           float ior)
{
    if (!IsDeltaGlass(roughness)) {
        return false;
    }

    float f0 = DielectricF0FromIor(ior);
    float transmissionLobe = transmission * (1.0 - f0);
    float reflectionLobe = f0;
    return transmissionLobe >= reflectionLobe;
}

inline bool IsPrimaryThinGlassFastPath(float roughness,
                                       float transmission,
                                       float ior,
                                       bool thinWalled)
{
    return thinWalled &&
           ShouldResolveDeltaTransmission(roughness, transmission, ior);
}

inline bool ShouldSimplifySecondaryMaterial(uint rayType)
{
    return rayType == RAY_TYPE_DIFFUSE || rayType == RAY_TYPE_GI_EVAL;
}

inline float EffectiveArchGlassThickness(float materialThickness)
{
    return max(materialThickness, 0.04);
}

inline bool RefractDeterministic(float3 V, float3 N, float ior, out float3 L)
{
    float cosTheta = dot(V, N);
    float eta = cosTheta > 0.0 ? (1.0 / ior) : ior;
    float3 outwardN = cosTheta > 0.0 ? N : -N;
    cosTheta = abs(cosTheta);

    float sin2ThetaI = max(0.0, 1.0 - cosTheta * cosTheta);
    float sin2ThetaT = eta * eta * sin2ThetaI;
    if (sin2ThetaT >= 1.0) {
        L = reflect(-V, outwardN);
        return false;
    }

    float cosThetaT = sqrt(1.0 - sin2ThetaT);
    L = normalize(eta * (-V) + (eta * cosTheta - cosThetaT) * outwardN);
    return true;
}

#endif // RAYTRACING_COMMON_H
