// shaders/raytracing/common.hlsli
// Common definitions shared across all raytracing shaders

#ifndef RAYTRACING_COMMON_H
#define RAYTRACING_COMMON_H

#include "../random_lib.hlsl"

#ifndef SHADER_ENABLE_DEBUG
#define SHADER_ENABLE_DEBUG 0
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
RWTexture2D<float4> g_reservoir0 : register(u2);
RWTexture2D<float4> g_reservoir1 : register(u3);
RWTexture2D<float4> g_gi_reservoir_a0 : register(u4);
RWTexture2D<float4> g_gi_reservoir_a1 : register(u5);
RWTexture2D<float4> g_gi_reservoir_a2 : register(u6);
RWTexture2D<float4> g_gi_reservoir_b0 : register(u7);
RWTexture2D<float4> g_gi_reservoir_b1 : register(u8);
RWTexture2D<float4> g_gi_reservoir_b2 : register(u9);

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
RWTexture2D<float> g_variance : register(u22);
RWStructuredBuffer<uint> g_wavefrontShadowContribution : register(u23);
RWTexture2D<float4> g_oidnAlbedoGuideOut : register(u34);
RWTexture2D<float4> g_oidnNormalRoughnessGuideOut : register(u35);

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
// ReGIR sample-side counters: tally what actually happens when shaders try to
// pull a light from the grid. These are ALWAYS on (not gated by
// SHADER_ENABLE_DEBUG) — the whole point is to verify the integration is
// firing in release builds where users see "no visible difference".
static const uint SHADER_COUNTER_REGIR_SAMPLE_HIT = 10;        // pick succeeded
static const uint SHADER_COUNTER_REGIR_SAMPLE_OOB = 11;        // worldPos outside grid
static const uint SHADER_COUNTER_REGIR_SAMPLE_NOCAND = 12;     // cell had no valid slots
static const uint SHADER_COUNTER_REGIR_SAMPLE_CLAMPED = 13;    // inversePdf hit domainCap
static const uint SHADER_COUNTER_REGIR_SAMPLER_CREATE = 14;    // WavefrontCreateLightSampler calls
static const uint SHADER_COUNTER_REGIR_SAMPLER_MODE = 15;      // sampler chose ReGIR mode
static const uint SHADER_COUNTER_REGIR_FLAT_NO_FEATURE = 16;   // runtime REGIR feature bit off
static const uint SHADER_COUNTER_REGIR_FLAT_NO_CELLS = 17;     // g_regirParams.totalCells was 0
static const uint SHADER_COUNTER_REGIR_MAX_TOTAL_CELLS = 19;   // max totalCells seen by shader
static const uint SHADER_COUNTER_REGIR_MAX_LIGHTS = 20;        // max local light count seen by shader
static const uint SHADER_COUNTER_COUNT = 24; // allocated counters

// GPU-writable counters buffer (read back by host)
RWStructuredBuffer<uint> g_shaderCounters : register(u24);

#define SHADER_DEBUG_MODE debugMode
#define SHADER_DEBUG_VIS_MODE debugVisualizationMode

#if SHADER_ENABLE_DEBUG
#define SHADER_COUNTER_ADD(counterIndex, value) InterlockedAdd(g_shaderCounters[(counterIndex)], (value))
#else
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
    float dxrFeatureFlags;
    float verticalTiltCorrection;
    float projectionMode;
}

static const float CAMERA_PROJECTION_PERSPECTIVE = 0.0;
static const float CAMERA_PROJECTION_SPHERICAL_360 = 1.0;

inline void BuildPerspectiveCameraBasis(float3 inputForward, float3 inputUp,
                                        float enableVerticalCorrection,
                                        out float3 projectionForward,
                                        out float3 projectionRight,
                                        out float3 projectionUp,
                                        out float verticalCenterShift)
{
    float3 forwardDir = normalize(inputForward);
    projectionRight = normalize(cross(forwardDir, inputUp));
    projectionUp = normalize(cross(projectionRight, forwardDir));
    projectionForward = forwardDir;
    verticalCenterShift = 0.0;

    if (enableVerticalCorrection <= 0.5) {
        return;
    }

    const float3 worldUp = float3(0.0, 1.0, 0.0);
    float3 levelForward = forwardDir - worldUp * dot(forwardDir, worldUp);
    float levelLengthSq = dot(levelForward, levelForward);
    if (levelLengthSq <= 1.0e-6) {
        return;
    }

    projectionForward = levelForward * rsqrt(levelLengthSq);
    projectionRight = normalize(cross(projectionForward, worldUp));
    projectionUp = normalize(cross(projectionRight, projectionForward));
    verticalCenterShift =
        clamp(dot(forwardDir, projectionUp) /
                  max(dot(forwardDir, projectionForward), 0.025),
              -40.0, 40.0);
}

inline float3 BuildPerspectiveCameraDirection(float2 uv, float3 inputForward,
                                              float3 inputUp,
                                              float verticalCorrection,
                                              float verticalFovDegrees,
                                              float cameraAspect)
{
    float3 projectionForward;
    float3 projectionRight;
    float3 projectionUp;
    float verticalCenterShift;
    BuildPerspectiveCameraBasis(inputForward, inputUp, verticalCorrection,
                                projectionForward, projectionRight,
                                projectionUp, verticalCenterShift);

    float2 ndc = uv * 2.0 - 1.0;
    float fInv = tan(radians(verticalFovDegrees) * 0.5);
    float yView = (-ndc.y) * fInv + verticalCenterShift;
    float xView = ndc.x * cameraAspect * fInv;
    return normalize(xView * projectionRight + yView * projectionUp +
                     projectionForward);
}

inline float3 BuildSphericalCameraDirection(float2 uv, float3 inputForward,
                                            float3 inputUp)
{
    float3 forwardDir = normalize(inputForward);
    float3 rightDir = normalize(cross(forwardDir, inputUp));
    float3 upDir = normalize(cross(rightDir, forwardDir));
    float azimuth = (uv.x - 0.5) * (2.0 * PI);
    float elevation = (0.5 - saturate(uv.y)) * PI;
    float sinAzimuth;
    float cosAzimuth;
    float sinElevation;
    float cosElevation;
    sincos(azimuth, sinAzimuth, cosAzimuth);
    sincos(elevation, sinElevation, cosElevation);
    float3 horizonDir = cosAzimuth * forwardDir + sinAzimuth * rightDir;
    return normalize(horizonDir * cosElevation + upDir * sinElevation);
}

inline float3 BuildCameraPrimaryDirection(float2 uv)
{
    if (projectionMode >= CAMERA_PROJECTION_SPHERICAL_360 - 0.5) {
        return BuildSphericalCameraDirection(uv, camForward, camUp);
    }
    return BuildPerspectiveCameraDirection(uv, camForward, camUp,
                                           verticalTiltCorrection, fov,
                                           aspect);
}

static const uint DXR_FEATURE_AOV_OUTPUT = 1u << 0;
static const uint DXR_FEATURE_PRIMARY_GUIDE = 1u << 1;
static const uint DXR_FEATURE_CLAY_PRESERVE_TRANSPARENCY = 1u << 2;
static const uint DXR_FEATURE_CLAY_PRESERVE_EMISSION = 1u << 3;
static const uint DXR_FEATURE_REGIR_ENABLED = 1u << 8;
// Mirrors kDxrFeatureDlssSpecProbe in camera.h. Gates the spec-probe
// RayQuery in wavefront_resolve_primary_cs.hlsl.
static const uint DXR_FEATURE_DLSS_SPEC_PROBE = 1u << 9;

inline bool DxrFeatureEnabled(uint feature)
{
    return (((uint)dxrFeatureFlags) & feature) != 0u;
}

inline float GetDxrProceduralSkyBoost()
{
    return max(dxrProceduralSkyBoost, 0.0);
}

inline float GetDxrIndirectIblBoost()
{
    return max(iblIndirectBoost, 0.0);
}

inline bool WavefrontDirectionalLightActive()
{
    return lightColor.w > 0.0 && any(lightColor.rgb > 1.0e-6);
}

inline float2 DirectionToUVRotated(float3 dir) {
    float2 uv = DirectionToUV(dir);
    uv.x = frac(uv.x + (iblRotationDegrees / 360.0));
    return uv;
}

#include "../lights_lib.hlsl"

StructuredBuffer<Light> g_lights : register(t5000);

// ReGIR resources are always part of the wavefront shader ABI. The runtime
// feature bit decides whether the sampler uses them; source/build-time defines
// must not decide whether ReGIR exists.
#include "../regir_lib.hlsl"
RWStructuredBuffer<ReGIRCellReservoir> g_regirCells : register(u36);
StructuredBuffer<ReGIRLightBound> g_regirLightBounds : register(t5001);

// Emissive mesh proxy data (sentinel lightIndex 0x80000000 | proxyIndex).
struct EmissiveProxyData
{
    float3 center;
    float  radius;
    float3 radiance;
    uint   triangleStart;
    uint   triangleCount;
    float  totalArea;
    uint   materialIndex;
    uint   pad0;
};
StructuredBuffer<EmissiveProxyData> g_emissiveProxyData : register(t5003);

struct EmissiveTriangleData
{
    float3 p0;
    float  cumulativeArea;
    float3 p1;
    float  area;
    float3 p2;
    float  pad0;
    float3 normal;
    float  pad1;
    float2 uv0;
    float2 uv1;
    float2 uv2;
    float2 pad2;
};
StructuredBuffer<EmissiveTriangleData> g_emissiveTriangleData : register(t5004);

cbuffer ReGIRParams : register(b11, space3)
{
    ReGIRConstants g_regirParams;
};

uint WavefrontGetEmissiveProxyCount()
{
    return g_regirParams.proxyCount;
}

// ReGIR sampling functions (require g_regirCells, declared above)

// Stochastically jitter the lookup position by up to +/-0.5 cell extents so a
// receiver near a cell boundary effectively samples from a 2x2x2 neighborhood
// over accumulation. Removes the visible cell-boundary discontinuity in light
// selection without the cost of 8 lookups per shading point.
uint ReGIR_WorldPosToCellJittered(float3 worldPos, inout RNG rng,
                                  ReGIRConstants params)
{
    float3 jitter = (float3(next_float(rng), next_float(rng),
                            next_float(rng)) - 0.5) *
                    params.cellSize * clamp(params.cellJitterScale, 0.0, 2.0);
    return ReGIR_WorldPosToCell(worldPos + jitter, params);
}

uint ReGIR_SampleCandidateWeighted(float3 worldPos, inout RNG rng,
                                   ReGIRConstants params,
                                   out float sampleWeight)
{
    sampleWeight = 0.0;
    uint cellIdx = ReGIR_WorldPosToCellJittered(worldPos, rng, params);
    if (cellIdx == 0xFFFFFFFFu) {
        InterlockedAdd(g_shaderCounters[SHADER_COUNTER_REGIR_SAMPLE_OOB], 1u);
        return 0xFFFFFFFFu;
    }

    uint base = ReGIR_CellBaseIndex(cellIdx, params);

    // Single-pass UNIFORM reservoir sample across valid slots. Each slot is an
    // i.i.d. RIS sample of the same proposal distribution.
    uint   selectedIdx = 0xFFFFFFFFu;
    float  selectedW = 0.0;
    float  selectedTarget = 0.0;
    uint   validCount = 0u;
    for (uint i = 0u; i < params.candidatesPerCell; ++i) {
        ReGIRCellReservoir slot = g_regirCells[base + i];
        if (slot.lightIndex == 0xFFFFFFFFu ||
            slot.weight <= 0.0 || slot.W <= 0.0)
            continue;
        ++validCount;
        // Equal-weight reservoir sample: each valid slot picked with prob 1/n.
        if (next_float(rng) * (float)validCount < 1.0) {
            selectedIdx = slot.lightIndex;
            selectedW = slot.W;
            selectedTarget = slot.weight;
        }
    }

    if (selectedIdx == 0xFFFFFFFFu) {
        InterlockedAdd(g_shaderCounters[SHADER_COUNTER_REGIR_SAMPLE_NOCAND], 1u);
        return 0xFFFFFFFFu;
    }

    // The reservoir stores W as the target-weight sum for the cell. Once a
    // candidate is selected with probability target / W, the unbiased discrete
    // light-sampling weight is W / target. M describes how many proposals built
    // the reservoir, not the selected-light PDF.
    float denom = max(selectedTarget, 1.0e-12);
    float inversePdf = selectedW / denom;
    // Firefly cap: it only kicks in for pathological picks where
    // a tiny selected target produces an outlier ratio. No lower clamp — a
    // sample whose target is above average should legitimately weight less
    // than the uniform light count in the estimator.
    const float domainCap =
        max((float)max(params.lightBoundCount, validCount), 1.0);
    bool finite = isfinite(inversePdf) && inversePdf > 0.0;
    if (finite && inversePdf > domainCap) {
        InterlockedAdd(g_shaderCounters[SHADER_COUNTER_REGIR_SAMPLE_CLAMPED], 1u);
    }
    sampleWeight = finite ? min(inversePdf, domainCap) : 0.0;
    InterlockedAdd(g_shaderCounters[SHADER_COUNTER_REGIR_SAMPLE_HIT], 1u);
    return selectedIdx;
}

uint ReGIR_SampleCandidate(float3 worldPos, inout RNG rng,
                           ReGIRConstants params)
{
    float sampleWeight = 0.0;
    return ReGIR_SampleCandidateWeighted(worldPos, rng, params,
                                         sampleWeight);
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
static const uint MATERIAL_FLAG_PARALLAX_MAPPED = 1u << 12;

struct MaterialData
{
    float4 baseColor_opacity;   // rgb + opacity
    float4 emissive_ior;        // rgb + refraction IOR
    float4 pbrParams_flags;     // x=metalness, y=roughness, z=transmission, w=flags (asfloat)
    uint4  packedTextures;      // 8x 16-bit indices packed as pairs
};

struct MaterialExtraData
{
    float4 coatLayerParams;     // x=coatWeight, y=coatRoughness, z=thinWalled, w=translucency
    float4 uvTransform;         // xy=uvScale, zw=uvOffset
    float4 uvRotationParams;    // x=regular UV rotation, y=flip normal-map Y
    float4 triPlanarParams;     // x=enabled, y=scale, z=sharpness, w=normalStrength
    float4 mappingVariationParams; // x=mode, y=offsetJitter, z=randomRotation, w=colorVariation
    float4 triPlanarRotationParams; // xyz=materialRotationDegrees, w=stochasticMirror
    float4 shadingParams;       // x=emissiveIntensity, y=specWeight, z=alphaCutoff, w=isGrass
    float4 transmissionColor;   // rgb=tinted transmission color
    float4 textureWeight0;      // x=baseColor, y=packedSurface, z=metalness, w=roughnessGloss
    float4 textureWeight1;      // x=normal, y=occlusion, z=emissive, w=opacity
    uint4  extraPackedTextures; // x=coatNormal/parallaxDepth
    float4 volumeParams;        // x=thickness, y=attenuationDistance, z=thicknessTexAmount, w=coatIor
    float4 specularColor;       // rgb=specularColor, a=specularColorTexAmount
    float4 sheenColor;          // rgb=sheenColor
    float4 lobeParams;          // x=coatNormalAmount, y=anisotropy, z=anisoRotationDeg, w=sheenWeight
    float4 parallaxParams;      // x=heightDepth, y=mode, z=roomDepth, w=windowAspect
    float4 parallaxTransform;   // xy=uvScale, zw=uvOffset
    float4 parallaxOptions;     // x=renderWindowBoxOnBackFace, y=reflectionIor
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

static const uint WAVEFRONT_ABI_VERSION = 9u;
static const uint WAVEFRONT_PATH_STATE_DWORDS = 12u;
static const uint WAVEFRONT_HIT_RECORD_DWORDS = 35u;
static const uint WAVEFRONT_SHADOW_TASK_DWORDS = 12u;
static const uint WAVEFRONT_DISPATCH_ARGS_DWORDS = 4u;
static const uint WAVEFRONT_QUEUE_PATH_A = 0u;
static const uint WAVEFRONT_QUEUE_PRIMARY_ACTIVE = 1u;
static const uint WAVEFRONT_QUEUE_PRIMARY_HIT = 2u;
static const uint WAVEFRONT_QUEUE_PRIMARY_MISS = 3u;
static const uint WAVEFRONT_QUEUE_PATH_B = 4u;
static const uint WAVEFRONT_QUEUE_SHADOW = 5u;
static const uint WAVEFRONT_QUEUE_COUNTER_COUNT = 16u;

struct WavefrontHitRecord
{
    float hitT;
    uint pixelIndex;
    uint packedColor0;
    uint packedColor1;
    uint packedNormal;
    uint packedAlbedo;
    uint packedIorType;
    uint packedReflectionIor;
    uint packedTransmission;
    uint packedSpecular;
    uint packedState;
    uint reserved;
    float4 surface;

// DLSS-RR guide data stays in the hit record so both are published and
// consumed as one queue item across material-bin scheduling.
    float3 guideOrigin;
    uint guidePackedState;
    float3 guideDirection;
    float guideHitT;
    uint guidePackedNormal;
    uint guidePackedAlbedo;
    uint guidePackedIorType;
    uint guidePackedReflectionIor;
    uint guidePackedTransmission;
    uint guidePackedSpecular;
    float4 guideSurface;
    uint packedGeomNormal;
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
static const uint WAVEFRONT_RESOLVE_FLAG_FAST_GI = 0x20000u;
static const uint WAVEFRONT_RESOLVE_FLAG_THIN_SECONDARY_SHADOWS = 0x40000u;
static const uint WAVEFRONT_RESOLVE_FLAG_DEFER_ACCUMULATION = 0x80000u;
static const uint WAVEFRONT_BACKEND_PARITY = 1u;
static const uint WAVEFRONT_BACKEND_OPTIMIZED = 2u;
static const uint WAVEFRONT_LIGHT_SAMPLE_DIRECTIONAL = 0u;
static const uint WAVEFRONT_LIGHT_SAMPLE_FLAT = 1u;
static const uint WAVEFRONT_LIGHT_SAMPLE_ENV = 2u;
static const uint WAVEFRONT_LIGHT_SAMPLER_FLAT = 0u;
static const uint WAVEFRONT_LIGHT_SAMPLER_REGIR = 1u;

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
    uint packedNormal;    // Octahedral packed shading normal
    uint packedGeomNormal; // Octahedral packed geometric face normal
    uint packedAlbedo;    // 3x8 UNORM base color
    uint packedIorType;   // 16-bit half refraction IOR + rayType/thin/specWeight
    uint packedReflectionIor; // 16-bit half reflection IOR
    uint packedTransmission; // 3x8 UNORM transmission color
    uint packedSpecular;  // 3x8 UNORM specular color
    float4 surface;       // roughness/metallic/transmission/translucency
    uint packedParallaxSelfShadow; // 8-bit sun self-shadow for wavefront direct lighting
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

// Spawned secondary/shadow TMin after Wächter–Binder origin offset. A 2 mm
// world-space TMin punches through interior corners and thin window frames;
// the integer origin offset already separates the ray from its originating
// primitive, so TMin only has to cover residual ulp error.
static const float kSpawnRayTMin = 1.0e-5f;

// Wächter and Binder, "A Fast and Robust Method for Avoiding Self-Intersection",
// JCGT 6(1), 2017. Scale-independent integer offset along the geometric normal.
inline float3 OffsetRayOrigin(float3 p, float3 n)
{
    const float origin = 1.0 / 32.0;
    const float floatScale = 1.0 / 65536.0;
    const float intScale = 256.0;

    int3 ofI = int3(intScale * n.x, intScale * n.y, intScale * n.z);
    float3 pI = float3(
        asfloat(asint(p.x) + ((p.x < 0.0) ? -ofI.x : ofI.x)),
        asfloat(asint(p.y) + ((p.y < 0.0) ? -ofI.y : ofI.y)),
        asfloat(asint(p.z) + ((p.z < 0.0) ? -ofI.z : ofI.z)));

    return float3(
        abs(p.x) < origin ? p.x + floatScale * n.x : pI.x,
        abs(p.y) < origin ? p.y + floatScale * n.y : pI.y,
        abs(p.z) < origin ? p.z + floatScale * n.z : pI.z);
}

inline float3 ComputeWorldGeometricNormal(float3 p0, float3 p1, float3 p2)
{
    float3 ng = cross(p1 - p0, p2 - p0);
    float lenSq = dot(ng, ng);
    return (lenSq > 1.0e-20) ? ng * rsqrt(lenSq) : float3(0.0, 1.0, 0.0);
}

inline float3 WorldGeometricNormalFromObjectVerts(float3x4 objectToWorld,
                                                 float3 p0, float3 p1, float3 p2)
{
    float3 wp0 = mul(objectToWorld, float4(p0, 1.0)).xyz;
    float3 wp1 = mul(objectToWorld, float4(p1, 1.0)).xyz;
    float3 wp2 = mul(objectToWorld, float4(p2, 1.0)).xyz;
    return ComputeWorldGeometricNormal(wp0, wp1, wp2);
}

// Offset p onto the hemisphere of `direction` using the geometric normal.
// Continuation, reflection, transmission, and shadow rays all go through here
// so grazing rays cannot skip the adjacent wall at a concave corner.
inline float3 SpawnRayOrigin(float3 p, float3 geomNormal, float3 direction)
{
    float nLenSq = dot(geomNormal, geomNormal);
    float3 n = (nLenSq > 1.0e-12) ? geomNormal * rsqrt(nLenSq)
                                  : float3(0.0, 1.0, 0.0);
    float dLenSq = dot(direction, direction);
    if (dLenSq > 1.0e-12 && dot(n, direction) < 0.0) {
        n = -n;
    }
    return OffsetRayOrigin(p, n);
}

inline float SpawnRayTMax(float maxDistance)
{
    return max(kSpawnRayTMin, maxDistance - kSpawnRayTMin);
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

inline float4 MakePayloadSurface(float roughness, float metallic,
                                 float transmission, float translucency)
{
    return saturate(float4(roughness, metallic, transmission, translucency));
}

inline float4 WavefrontHitRecordSurface(WavefrontHitRecord record)
{
    return saturate(record.surface);
}

inline float4 WavefrontHitRecordGuideSurface(WavefrontHitRecord record)
{
    return saturate(record.guideSurface);
}

inline uint WavefrontClassifyMaterialBinFromSurface(float4 surface,
                                                    uint packedIorType,
                                                    uint packedColor0,
                                                    uint packedColor1)
{
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

inline uint PackWavefrontParallaxSelfShadow(float selfShadow)
{
    return (uint)round(saturate(selfShadow) * 255.0);
}

inline uint WavefrontApplyParallaxSelfShadowToSortKey(uint sortKey,
                                                      float selfShadow)
{
    return (sortKey & 0x00FFFFFFu) |
           (PackWavefrontParallaxSelfShadow(selfShadow) << 24);
}

inline float WavefrontGetParallaxSelfShadowFromSortKey(uint sortKey)
{
    return ((sortKey >> 24) & 0xFFu) / 255.0;
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
    uint numLights = WavefrontGetAvailableLightCount();
    sampler.availableLights = numLights;
    InterlockedAdd(g_shaderCounters[SHADER_COUNTER_REGIR_SAMPLER_CREATE], 1u);
    InterlockedMax(g_shaderCounters[SHADER_COUNTER_REGIR_MAX_LIGHTS], numLights);
    InterlockedMax(g_shaderCounters[SHADER_COUNTER_REGIR_MAX_TOTAL_CELLS],
                   g_regirParams.totalCells);
    if (DxrFeatureEnabled(DXR_FEATURE_REGIR_ENABLED) &&
        g_regirParams.totalCells > 0u) {
        InterlockedAdd(g_shaderCounters[SHADER_COUNTER_REGIR_SAMPLER_MODE], 1u);
        sampler.mode = WAVEFRONT_LIGHT_SAMPLER_REGIR;
    } else {
        if (!DxrFeatureEnabled(DXR_FEATURE_REGIR_ENABLED)) {
            InterlockedAdd(g_shaderCounters[SHADER_COUNTER_REGIR_FLAT_NO_FEATURE], 1u);
        }
        if (g_regirParams.totalCells == 0u) {
            InterlockedAdd(g_shaderCounters[SHADER_COUNTER_REGIR_FLAT_NO_CELLS], 1u);
        }
        sampler.mode = WAVEFRONT_LIGHT_SAMPLER_FLAT;
    }
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
    sample.radiance = WavefrontDirectionalLightActive()
                          ? lightColor.rgb * lightColor.w * sampleWeight
                          : float3(0.0, 0.0, 0.0);
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

inline bool WavefrontIsEmissiveProxyLightIndex(uint lightIndex)
{
    return lightIndex != 0xFFFFFFFFu && ((lightIndex & 0x80000000u) != 0u);
}

inline float3 WavefrontBlendTextureRgb(float3 sampleValue, float amount)
{
    return lerp(float3(1.0, 1.0, 1.0), sampleValue, saturate(amount));
}

inline uint WavefrontHashFloat3(float3 value, uint seed)
{
    uint3 bits = asuint(value);
    uint h = seed ^ 0x9E3779B9u;
    h ^= bits.x + 0x85EBCA6Bu + (h << 6) + (h >> 2);
    h ^= bits.y + 0xC2B2AE35u + (h << 6) + (h >> 2);
    h ^= bits.z + 0x27D4EB2Du + (h << 6) + (h >> 2);
    return h | 1u;
}

inline uint WavefrontSampleEmissiveTriangleIndex(EmissiveProxyData proxy,
                                                 float xi)
{
    uint lo = proxy.triangleStart;
    uint hi = proxy.triangleStart + proxy.triangleCount - 1u;
    float target = saturate(xi) * max(proxy.totalArea, 1.0e-6);
    [loop]
    for (uint iter = 0u; iter < 24u && lo < hi; ++iter) {
        uint mid = (lo + hi) >> 1;
        if (g_emissiveTriangleData[mid].cumulativeArea < target)
            lo = mid + 1u;
        else
            hi = mid;
    }
    return lo;
}

inline float3 WavefrontEvaluateEmissiveProxyRadiance(EmissiveProxyData proxy,
                                                     float2 uv)
{
    float3 radiance = proxy.radiance;
    if (proxy.materialIndex != 0xFFFFFFFFu) {
        MaterialData material = materials[proxy.materialIndex];
        MaterialExtraData materialExtra = materialExtras[proxy.materialIndex];
        int texEmis = UnpackTextureIndexLow(material.packedTextures.z);
        if (texEmis >= 0) {
            uint texSlot = NonUniformResourceIndex((uint)texEmis);
            float3 tex = sRGBToLinear(textures[texSlot].SampleLevel(
                linearSampler, uv, 0.0).rgb);
            radiance *= WavefrontBlendTextureRgb(
                tex, materialExtra.textureWeight1.z);
        }
    }
    return radiance;
}

inline WavefrontLightSample WavefrontSampleEmissiveProxyLight(
    float3 surfacePos,
    uint packedProxyIndex,
    float sampleWeight,
    inout RNG rng)
{
    WavefrontLightSample sample;
    uint proxyIdx = packedProxyIndex & 0x7FFFFFFFu;
    // Guard against stale proxy sentinels surviving a partial rebuild. If the
    // index is past the current proxy buffer, return a zero-radiance sample
    // so the downstream shadow ray is cheap and contributes nothing.
    if (proxyIdx >= WavefrontGetEmissiveProxyCount()) {
        sample.direction = float3(0.0, 1.0, 0.0);
        sample.maxDistance = 0.0;
        sample.radiance = float3(0.0, 0.0, 0.0);
        sample.packedLightIndex =
            WavefrontPackLightSampleMetadata(WAVEFRONT_LIGHT_SAMPLE_FLAT, 0xFFFFFFFFu);
        return sample;
    }
    EmissiveProxyData proxy = g_emissiveProxyData[proxyIdx];
    if (proxy.triangleCount > 0u && proxy.totalArea > 1.0e-6) {
        uint triIndex = WavefrontSampleEmissiveTriangleIndex(
            proxy, next_float(rng));
        EmissiveTriangleData tri = g_emissiveTriangleData[triIndex];
        float2 baryXi = next_float2(rng);
        float su = sqrt(saturate(baryXi.x));
        float b0 = 1.0 - su;
        float b1 = baryXi.y * su;
        float b2 = 1.0 - b0 - b1;
        float3 samplePos = tri.p0 * b0 + tri.p1 * b1 + tri.p2 * b2;
        float2 uv = tri.uv0 * b0 + tri.uv1 * b1 + tri.uv2 * b2;
        float3 toSample = samplePos - surfacePos;
        float distSq = dot(toSample, toSample);
        float dist = sqrt(max(distSq, 1.0e-8));
        float3 L = toSample / max(dist, 1.0e-4);
        float3 Nl = normalize(tri.normal);
        float cosLight = abs(dot(Nl, -L));
        float3 radiance = WavefrontEvaluateEmissiveProxyRadiance(proxy, uv);
        sample.direction = L;
        sample.maxDistance = dist;
        sample.radiance =
            radiance * (cosLight * proxy.totalArea) /
            max(distSq, 1.0e-6) * sampleWeight;
        sample.packedLightIndex =
            WavefrontPackLightSampleMetadata(WAVEFRONT_LIGHT_SAMPLE_FLAT, 0xFFFFFFFFu);
        return sample;
    }

    float3 toProxy = proxy.center - surfacePos;
    float dist = length(toProxy);
    float3 L = dist > 1e-4 ? toProxy / dist : float3(0.0, 1.0, 0.0);
    sample.direction = L;
    sample.maxDistance = dist;
    float attenuation = 1.0 / max(dist * dist, proxy.radius * proxy.radius * 0.25);
    sample.radiance = proxy.radiance * attenuation * sampleWeight;
    sample.packedLightIndex =
        WavefrontPackLightSampleMetadata(WAVEFRONT_LIGHT_SAMPLE_FLAT, 0xFFFFFFFFu);
    return sample;
}

inline WavefrontLightSample WavefrontSampleEmissiveProxyLight(
    float3 surfacePos,
    uint packedProxyIndex,
    float sampleWeight)
{
    RNG rng;
    rng.state = WavefrontHashFloat3(surfacePos, packedProxyIndex);
    return WavefrontSampleEmissiveProxyLight(surfacePos, packedProxyIndex,
                                             sampleWeight, rng);
}

inline WavefrontLightSample WavefrontSampleReGIRLight(
    float3 surfacePos,
    float sampleWeight,
    inout RNG rng)
{
    WavefrontLightSample sample;
    const uint numLights = WavefrontGetAvailableLightCount();

    float regirSampleWeight = 0.0;
    uint lightIndex = ReGIR_SampleCandidateWeighted(
        surfacePos, rng, g_regirParams, regirSampleWeight);

    if (lightIndex == 0xFFFFFFFFu) {
        if (numLights == 0u) {
            sample.direction = float3(0.0, 1.0, 0.0);
            sample.maxDistance = 0.0;
            sample.radiance = float3(0.0, 0.0, 0.0);
            sample.packedLightIndex =
                WavefrontPackLightSampleMetadata(WAVEFRONT_LIGHT_SAMPLE_FLAT, 0u);
            return sample;
        }
        lightIndex = next_uint(rng) % numLights;
        regirSampleWeight = (float)numLights;
    }

    const float effectiveSampleWeight =
        sampleWeight * max(regirSampleWeight, 1.0);

    // Handle emissive proxy sentinel
    if (WavefrontIsEmissiveProxyLightIndex(lightIndex)) {
        return WavefrontSampleEmissiveProxyLight(
            surfacePos, lightIndex, effectiveSampleWeight, rng);
    }

    if (lightIndex >= numLights) {
        // Fallback: random light from flat list
        if (numLights == 0u) {
            sample.direction = float3(0.0, 1.0, 0.0);
            sample.maxDistance = 0.0;
            sample.radiance = float3(0.0, 0.0, 0.0);
            sample.packedLightIndex =
                WavefrontPackLightSampleMetadata(WAVEFRONT_LIGHT_SAMPLE_FLAT, 0u);
            return sample;
        }
        lightIndex = next_uint(rng) % numLights;
        regirSampleWeight = (float)numLights;
    }

    return WavefrontSampleFlatLight(
        surfacePos, lightIndex,
        sampleWeight * max(regirSampleWeight, 1.0), rng);
}

inline WavefrontLightSample WavefrontSampleDirectLight(
    WavefrontLightSamplerContext sampler,
    float3 surfacePos,
    inout RNG rng)
{
    const uint numLights = sampler.availableLights;
    const bool directionalActive = WavefrontDirectionalLightActive();
    const bool useReGIR = (sampler.mode == WAVEFRONT_LIGHT_SAMPLER_REGIR);
    if (numLights == 0u && useReGIR) {
        return WavefrontSampleReGIRLight(surfacePos, 1.0, rng);
    }

    if (numLights == 0u) {
        if (directionalActive) {
            return WavefrontSampleDirectionalLight(1.0);
        }
        return WavefrontSampleFlatLight(surfacePos, 0u, 1.0, rng);
    }

    if (directionalActive && next_float(rng) < 0.5) {
        return WavefrontSampleDirectionalLight((numLights > 0u) ? 2.0 : 1.0);
    }

    const float localSampleWeight = directionalActive ? 2.0 : 1.0;
    if (sampler.mode == WAVEFRONT_LIGHT_SAMPLER_REGIR) {
        return WavefrontSampleReGIRLight(surfacePos, localSampleWeight, rng);
    }

    const uint lightIndex = next_uint(rng) % numLights;
    return WavefrontSampleFlatLight(surfacePos, lightIndex,
                                    localSampleWeight * (float)numLights, rng);
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
    float cloudLod = min(10.0, envLod + 0.5);
    float2 uv = DirectionToUVRotated(normalize(direction));
    float3 color = envMap.SampleLevel(linearSampler, uv, envLod).rgb *
                   GetDxrProceduralSkyBoost();

    if (cloudRenderingEnabled > 0.5f) {
        float4 baked = bakedClouds.SampleLevel(linearSampler, uv, cloudLod);
        baked.a = saturate(baked.a);
        baked.rgb = max(baked.rgb, 0.0);
        float opacity = 1.0f - baked.a;
        float denseCore = pow(saturate(opacity), 2.2f);
        float skyLeak = 0.035f * denseCore;
        float3 cloudColor = color * (baked.a + skyLeak) + baked.rgb;
        cloudColor += color * (0.006f * denseCore);
        color = clamp(cloudColor, 0.0f, 100000.0f);
    }

    return color;
}

inline float3 WavefrontEvaluateIndirectEnvironmentRadiance(float3 direction,
                                                           float3 surfacePos)
{
    return WavefrontEvaluateEnvironmentRadiance(direction, surfacePos) *
           GetDxrIndirectIblBoost();
}

inline float3 WavefrontEvaluateShadowTaskRadiance(uint packedLightIndex,
                                                  float3 surfacePos,
                                                  float3 direction)
{
    const uint lightType = WavefrontGetLightSampleType(packedLightIndex);
    if (lightType == WAVEFRONT_LIGHT_SAMPLE_ENV) {
        return WavefrontEvaluateIndirectEnvironmentRadiance(direction,
                                                           surfacePos);
    }
    return float3(0.0, 0.0, 0.0);
}

inline uint PackPayloadIor(float ior)
{
    return f32tof16(clamp(ior, 1.0, 10.0)) & 0xFFFFu;
}

inline uint PackPayloadIorType(float ior, uint rayType, bool thinWalled, float specularWeight)
{
    uint hIor = PackPayloadIor(ior);
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
    p.packedGeomNormal = PackNormalOctahedron(float3(0.0, 1.0, 0.0));
    p.packedAlbedo = PackPayloadAlbedo(float3(0.0, 0.0, 0.0));
    p.surface = MakePayloadSurface(1.0, 0.0, 0.0, 0.0);
    p.packedIorType = PackPayloadIorType(1.0, rayType, false, 1.0);
    p.packedReflectionIor = PackPayloadIor(1.0);
    p.packedTransmission =
        PackPayloadTransmissionColor(float3(1.0, 1.0, 1.0));
    p.packedSpecular = PackPayloadSpecularColor(float3(1.0, 1.0, 1.0));
    p.packedParallaxSelfShadow = PackWavefrontParallaxSelfShadow(1.0);
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
