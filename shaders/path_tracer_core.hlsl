// shaders/path_tracer_core.hlsl
// Main Path Tracing RayGen shader with Accumulation and ReSTIR DI

#include "raytracing/common.hlsli"
#include "random_lib.hlsl"
#include "lights_lib.hlsl"
#include "restir_lib.hlsl"

// Additional resources for ReSTIR
StructuredBuffer<Light> g_lights : register(t5000);
// Matches DXR_HEAP_VARIANCE_UAV_OFFSET (u22) in dxr_renderer.cpp.
RWTexture2D<float> g_variance : register(u22);
RWTexture2D<float4> g_reservoir0 : register(u2);
RWTexture2D<float4> g_reservoir1 : register(u3);
RWTexture2D<float4> g_gi_reservoir_a0 : register(u4);
RWTexture2D<float4> g_gi_reservoir_a1 : register(u5);
RWTexture2D<float4> g_gi_reservoir_a2 : register(u6);
RWTexture2D<float4> g_gi_reservoir_b0 : register(u7);
RWTexture2D<float4> g_gi_reservoir_b1 : register(u8);
RWTexture2D<float4> g_gi_reservoir_b2 : register(u9);

#include "brdf_lib.hlsl"

// Cosine-weighted cone sampling around a direction (very small, stable approximation)
float3 SampleCone(float3 dir, float cosThetaMax, float2 u)
{
    float z = lerp(cosThetaMax, 1.0, u.x);
    float r = sqrt(max(0.0, 1.0 - z * z));
    float phi = 2.0 * PI * u.y;
    float x = r * cos(phi);
    float y = r * sin(phi);

    float3 up = abs(dir.z) < 0.999 ? float3(0,0,1) : float3(1,0,0);
    float3 T = normalize(cross(up, dir));
    float3 B = cross(dir, T);
    return normalize(T * x + B * y + dir * z);
}

float TraceAoHemisphereVisibility(float3 P, float3 hemisphereNormal,
                                  float radius, uint sampleCount,
                                  inout RNG rng)
{
    if (sampleCount == 0 || radius <= 1.0e-4) {
        return 1.0;
    }

    float3 N = normalize(hemisphereNormal);
    float eps = max(0.0001, radius * 0.02);
    float visibleCount = 0.0;

    [loop]
    for (uint sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
        float2 u = next_float2(rng);
        float3 dir = normalize(align_to_normal(sample_hemisphere_cosine(u), N));

        RayDesc aoRay;
        aoRay.Origin = P + N * eps;
        aoRay.Direction = dir;
        aoRay.TMin = eps;
        aoRay.TMax = radius;

        RayPayload aoPayload;
        aoPayload.t = 1.0;
        aoPayload.packedColor1 = 0u;
        PayloadSetColor(aoPayload, float3(0.0, 0.0, 0.0));
        aoPayload.packedNormal = PackNormalOctahedron(float3(0.0, 1.0, 0.0));
        aoPayload.packedAlbedo = PackPayloadAlbedo(float3(0.0, 0.0, 0.0));
        aoPayload.packedSurface = PackPayloadSurface(1.0, 0.0, 0.0, 0.0);
        aoPayload.packedIorType =
            PackPayloadIorType(1.0, RAY_TYPE_SHADOW, false, 1.0);
        aoPayload.packedTransmission =
            PackPayloadTransmissionColor(float3(1.0, 1.0, 1.0));
        aoPayload.packedSpecular = PackPayloadSpecularColor(float3(1.0, 1.0, 1.0));
        TraceRay(g_accel,
                 RAY_FLAG_CULL_BACK_FACING_TRIANGLES,
                 0xFF, 0, 0, 0, aoRay, aoPayload);
        SHADER_COUNTER_ADD(SHADER_COUNTER_SHADOW_TRACES, 1);

        // Use a distance-normalized visibility contribution so AO behaves
        // as a smooth function of ray distance, not just a binary hit/no-hit.
        // This gives better control in low radius values and avoids "whole mesh"
        // blackening when the radius is relatively large.
        if (aoPayload.t < 0.0) {
            visibleCount += 1.0;
        } else {
            float hitDist = saturate(aoPayload.t / radius);
            // Non-linear falloff so nearby hits don't instantly plunge to black
            visibleCount += hitDist * hitDist;
        }
    }

    return visibleCount / sampleCount;
}

float ComputePrimaryRayTracedAo(float3 P, float3 N, float radius,
                                uint aoMode, float aoIntensity,
                                inout RNG rng)
{
    if (aoIntensity <= 1.0e-4 || radius <= 1.0e-4) {
        return 1.0;
    }

    // UI value is in mm and converted to actual meters in CPU.
    // The exact physical simulation radius is used.

    float3 normal = normalize(N);
    if (dot(normal, normal) <= 1.0e-8) {
        return 1.0;
    }

    const uint kAoSamplesPerHemisphere = 4;
    float visibility = 1.0;
    if (aoMode == 0) {
        visibility = TraceAoHemisphereVisibility(P, -normal, radius,
                                                 kAoSamplesPerHemisphere, rng);
    } else if (aoMode == 1) {
        visibility = TraceAoHemisphereVisibility(P, normal, radius,
                                                 kAoSamplesPerHemisphere, rng);
    } else {
        float outwardVisibility =
            TraceAoHemisphereVisibility(P, normal, radius,
                                        kAoSamplesPerHemisphere, rng);
        float inwardVisibility =
            TraceAoHemisphereVisibility(P, -normal, radius,
                                        kAoSamplesPerHemisphere, rng);
        visibility = min(outwardVisibility, inwardVisibility);
    }

    float occlusion = saturate(1.0 - visibility);
    return saturate(1.0 - occlusion * aoIntensity);
}

float3 ComputeSurfaceF0(float3 albedo, float metallic, float ior,
                        float specularWeight, float3 specularColor)
{
    float f0s = (ior - 1.0) / (ior + 1.0);
    f0s = f0s * f0s;
    float3 dielectricF0 =
        float3(f0s, f0s, f0s) * saturate(specularWeight) *
        saturate(specularColor);
    return lerp(dielectricF0, albedo, metallic);
}

float3 EvaluateCoatSpecular(float3 N, float3 V, float3 L, float coatRoughness)
{
    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(dot(N, V));
    if (NdotL <= 0.0 || NdotV <= 0.0) {
        return float3(0.0, 0.0, 0.0);
    }

    float3 H = normalize(L + V);
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));
    float3 F0c = float3(0.04, 0.04, 0.04);
    float3 Fc = F_Schlick(VdotH, F0c);
    float Dc = D_GGX(NdotH, coatRoughness);
    float Vc = V_SmithCorrelated(NdotV, NdotL, coatRoughness);
    return Dc * Vc * Fc;
}

float3 EvaluateSurfaceBrdf(float3 N, float3 V, float3 L,
                           float3 albedo, float3 diffuseAlbedo,
                           float metallic, float roughness,
                           float ior, float specularWeight,
                           float3 specularColor,
                           float coatWeight, float coatRoughness)
{
    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(dot(N, V));
    if (NdotL <= 0.0 || NdotV <= 0.0) {
        return float3(0.0, 0.0, 0.0);
    }

    float3 H = normalize(L + V);
    float VdotH = saturate(dot(V, H));
    float NdotH = saturate(dot(N, H));
    float3 F0 = ComputeSurfaceF0(albedo, metallic, ior, specularWeight,
                                 specularColor);
    float3 F = F_Schlick(VdotH, F0);
    float3 spec = D_GGX(NdotH, roughness) * V_SmithCorrelated(NdotV, NdotL, roughness) * F;
    float3 baseBrdf = (diffuseAlbedo / PI) * (1.0 - F) + spec;

    float clearcoat = saturate(coatWeight);
    if (clearcoat <= 0.0) {
        return baseBrdf;
    }

    float3 coatBrdf = EvaluateCoatSpecular(N, V, L, coatRoughness);
    return baseBrdf * (1.0 - clearcoat) + coatBrdf * clearcoat;
}

float DielectricF0FromIor(float ior)
{
    float safeIor = max(ior, 1.0 + 1e-4);
    float f0 = (safeIor - 1.0) / (safeIor + 1.0);
    return f0 * f0;
}

float GlassScatterAlpha(float roughness)
{
    float r = saturate(roughness);
    return r * r;
}

bool IsDeltaGlass(float roughness)
{
    return GlassScatterAlpha(roughness) <= 1.5e-6;
}

bool IsDeltaSpecular(float roughness)
{
    return GlassScatterAlpha(roughness) <= 1.5e-6;
}

bool ShouldResolveDeltaTransmission(float roughness, float transmission,
                                    float ior)
{
    if (!IsDeltaGlass(roughness)) {
        return false;
    }

    // Only true delta glass uses the clear-window resolve path. Rough glass,
    // even slightly rough glass, must stay on the transmission sampler so there
    // is no hidden material threshold in the roughness slider.
    float f0 = DielectricF0FromIor(ior);
    float transmissionLobe = transmission * (1.0 - f0);
    float reflectionLobe = f0;
    return transmissionLobe >= reflectionLobe;
}

bool RefractDeterministic(float3 V, float3 N, float ior, out float3 L)
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

bool SampleRoughDielectric(float3 V, float3 N, float roughness, float ior,
                           float2 uMicrofacet, float uBranch,
                           out float3 L, out bool refracted)
{
    float3 M = SampleGGX(uMicrofacet, N, roughness);
    if (dot(V, M) <= 0.0) {
        L = normalize(-N);
        refracted = true;
        return false;
    }

    float cosTheta = saturate(dot(V, M));
    float F = FresnelDielectric(dot(V, M), ior);
    if (uBranch < F) {
        L = normalize(reflect(-V, M));
        if (dot(L, N) <= 1.0e-5) {
            L = reflect(-V, N);
        }
        refracted = false;
        return true;
    }

    if (!RefractDeterministic(V, M, ior, L)) {
        L = normalize(reflect(-V, M));
        if (dot(L, N) <= 1.0e-5) {
            L = reflect(-V, N);
        }
        refracted = false;
        return true;
    }

    if (dot(L, N) >= -1.0e-5) {
        L = normalize(-N);
    }
    refracted = true;
    return true;
}

float3 SampleThinGlassTransmission(float3 V, float roughness, float2 u)
{
    return normalize(-V);
}

void StabilizeSpecularSample(float3 N, float3 V,
                             inout float3 H, inout float3 L,
                             out float NdotL, out float NdotH,
                             out float VdotH)
{
    L = normalize(L);
    NdotL = saturate(dot(N, L));
    NdotH = saturate(dot(N, H));
    VdotH = saturate(dot(V, H));

    if (NdotL <= 1.0e-5 || NdotH <= 1.0e-5 || VdotH <= 1.0e-5) {
        L = normalize(reflect(-V, N));
        H = normalize(V + L);
        NdotL = saturate(dot(N, L));
        NdotH = saturate(dot(N, H));
        VdotH = saturate(dot(V, H));
    }
}

void ComputeLobeProbabilities(float3 N, float3 V,
                              float3 albedo,
                              float metallic, float transmission,
                              float translucency,
                              float ior, float specularWeight,
                              float3 specularColor,
                              float coatWeight,
                              out float coatProb,
                              out float specProb,
                              out float diffProb,
                              out float transProb,
                              out float totalProb)
{
    float3 F0 = ComputeSurfaceF0(albedo, metallic, ior, specularWeight,
                                 specularColor);
    float3 F = F_Schlick(saturate(dot(N, V)), F0);
    float clearcoat = saturate(coatWeight);
    float coatF = F_Schlick(saturate(dot(N, V)), float3(0.04, 0.04, 0.04)).x;

    coatProb = clearcoat * coatF;
    specProb = (1.0 - clearcoat) * max(F.x, max(F.y, F.z));

    float baseDiffProb = (1.0 - clearcoat) * (1.0 - specProb) * (1.0 - metallic) * (1.0 - transmission);
    transProb = baseDiffProb * saturate(translucency);
    diffProb = max(0.0, baseDiffProb - transProb);
    totalProb = max(1e-6, coatProb + specProb + diffProb + transProb);
}

GI_Reservoir unpack_gi_reservoir(float4 d0, float4 d1, float4 d2) {
    GI_Reservoir r;
    r.hitPos = d0.xyz;
    r.radiance = d1.xyz;
    r.w_sum = d2.x;
    r.M = asuint(d2.y);
    r.W = d2.z;
    return r;
}

void pack_gi_reservoir(GI_Reservoir r, out float4 d0, out float4 d1, out float4 d2) {
    d0 = float4(r.hitPos, 0.0);
    d1 = float4(r.radiance, 0.0);
    d2 = float4(r.w_sum, asfloat(r.M), r.W, 0.0);
}

// target PDF for ReSTIR DI (luminance of lit surface) - now in restir_lib.hlsl

Reservoir unpack_reservoir(float4 data) {
    Reservoir r;
    r.lightIndex = asuint(data.x);
    // Defensive: if hardware normalized the NaN bits, reset to Sun index
    if (r.lightIndex == 0x7FC00000) r.lightIndex = 0xFFFFFFFF;
    r.w_sum = data.y;
    r.M = asuint(data.z);
    r.W = data.w;
    // Extra safety: clamp state
    if (isnan(r.w_sum) || isinf(r.w_sum)) r.w_sum = 0.0;
    if (isnan(r.W) || isinf(r.W)) r.W = 0.0;
    return r;
}

float4 pack_reservoir(Reservoir r) {
    return float4(asfloat(r.lightIndex), r.w_sum, asfloat(r.M), r.W);
}

float halton(uint index, uint base)
{
    float f = 1.0;
    float r = 0.0;
    while (index > 0)
    {
        f /= (float)base;
        r += f * (float)(index % base);
        index /= base;
    }
    return r;
}

RayPayload InitRayPayload(uint rayType)
{
    RayPayload p;
    p.t = -1.0;
    p.packedColor1 = 0u;
    PayloadSetColor(p, float3(0.0, 0.0, 0.0));
    p.packedNormal = PackNormalOctahedron(float3(0.0, 1.0, 0.0));
    p.packedAlbedo = PackPayloadAlbedo(float3(0.0, 0.0, 0.0));
    p.packedSurface = PackPayloadSurface(1.0, 0.0, 0.0, 0.0);
    p.packedIorType = PackPayloadIorType(1.0, rayType, false, 1.0);
    p.packedTransmission = PackPayloadTransmissionColor(float3(1.0, 1.0, 1.0));
    p.packedSpecular = PackPayloadSpecularColor(float3(1.0, 1.0, 1.0));
    return p;
}

float3 TraceGlassReflectionRadiance(float3 P, float3 V, float3 N,
                                    float3 albedo, float metallic,
                                    float ior, float specularWeight,
                                    float3 specularColor)
{
    float3 reflectionDir = normalize(reflect(-V, N));

    RayDesc reflectionRay;
    reflectionRay.Origin = P + reflectionDir * 0.002;
    reflectionRay.Direction = reflectionDir;
    reflectionRay.TMin = 0.002;
    reflectionRay.TMax = 10000.0;

    RayPayload reflectionPayload = InitRayPayload(RAY_TYPE_GI_EVAL);
    TraceRay(g_accel, RAY_FLAG_NONE, 0xFF, 0, 0, 0,
             reflectionRay, reflectionPayload);
    SHADER_COUNTER_ADD(SHADER_COUNTER_TRACE_RAYS, 1);
    SHADER_COUNTER_ADD(SHADER_COUNTER_SPECULAR_TRACES, 1);

    float cosTheta = abs(dot(V, N));
    float dielectricF = FresnelDielectric(cosTheta, ior) *
                        saturate(specularWeight);
    float3 dielectricReflection = dielectricF * saturate(specularColor);
    float3 metalReflection =
        F_Schlick(cosTheta, saturate(albedo));
    float3 reflectionWeight =
        lerp(dielectricReflection, metalReflection, saturate(metallic));

    return PayloadGetColor(reflectionPayload) * reflectionWeight;
}

[shader("raygeneration")]
void RayGen()
{
    uint3 launchIndex = DispatchRaysIndex();
    uint3 launchDim = DispatchRaysDimensions();
    uint frame = (uint)globalFrameCount;
    uint accumFrame = (uint)accumulationCount;
    // Keep per-frame reservoir ping-pong deterministic even for pixels that
    // early-out due to adaptive sampling.
    bool flip = (frame % 2) == 1;
    const uint kAdaptiveStartSpp = 16u;
    const uint kAdaptiveMinPerPixelSpp = 16u;
    const float kAdaptiveRelScale = 0.90;
    const float kAdaptiveEdgeRelScale = 0.55;
    const float kAdaptiveAbsSemFloor = 5e-4;
    const float kAdaptiveAbsSemScale = 0.0125;
    const float kAdaptiveEdgeAbsSemScale = 0.0055;
    const float kAdaptiveMinKeepProb = 0.10;
    const float kAdaptiveEdgeMinKeepProb = 0.22;
    const float kAdaptiveEdgeContrastThreshold = 0.012;
    const float kAdaptiveMinExpectedRatio = 0.15;
    const float kAdaptiveLagKeepScale = 1.00;
    const bool exportAdaptiveMode = (exportRendering > 0.5);
    // Artistic control: boost environment contribution to scene lighting
    // (DI/GI transport) without making the visible sky dome brighter.
    const float kEnvLightingBoost = 3.0;
    const bool debugViewActive = (SHADER_DEBUG_MODE > 0.0) || (SHADER_DEBUG_VIS_MODE == 1.0);

    const float2 kInvalidMvec = float2(-1e6, -1e6);
    float2 currScreen = float2(launchIndex.xy) + 0.5;
    float2 screenDim = float2(launchDim.xy);
    float2 screenMin = float2(-0.5, -0.5);
    float2 screenMax = screenDim + float2(0.5, 0.5);

    RNG rng = init_rng(launchIndex.xy, frame);

    // Use jitter from Camera CB (calculated on CPU to match DLSS)
    float2 jitter = float2(jitterX, jitterY);
    float2 uv = (float2(launchIndex.xy) + 0.5 + jitter) / float2(launchDim.xy);
    float2 ndc = uv * 2.0 - 1.0;

    // Non-jittered pixel center for motion vectors / sky reprojection.
    float2 uvCenter = (float2(launchIndex.xy) + 0.5) / float2(launchDim.xy);
    float2 ndcCenter = uvCenter * 2.0 - 1.0;

    float f_inv = tan(radians(fov) * 0.5);
    float3 forward = normalize(camForward);
    float3 R = normalize(cross(forward, camUp));
    float3 U = normalize(cross(R, forward));
    
    float y_view = (-ndc.y) * f_inv;
    float x_view = ndc.x * aspect * f_inv;
    
    float3 rayDir = normalize(x_view * R + y_view * U + forward);
    float3 rayOrigin = camPos;

    // Center ray direction (no jitter).
    float y_view_center = (-ndcCenter.y) * f_inv;
    float x_view_center = ndcCenter.x * aspect * f_inv;
    float3 rayDirCenter = normalize(x_view_center * R + y_view_center * U + forward);

    float3 accumulatedColor = float3(0, 0, 0);
    float3 throughput = float3(1, 1, 1);
    float3 primaryGlassReflection = float3(0, 0, 0);
    // True when the primary hit is a transmissive (glass/refractive) surface.
    bool primaryIsRefractive = false;

    // Primary hit info for DLSS inputs
    bool primaryHit = false;
    float3 primaryPos = float3(0, 0, 0);
    float3 primaryNormal = float3(0, 1, 0);
    float3 primaryAlbedo = float3(0, 0, 0);
    float primaryRoughness = 1.0;
    float primaryViewZ = -1.0;
    float3 primarySpecAlbedo = float3(0, 0, 0);
    float primarySpecHitDist = -1.0;
    float primaryTonemapAoFactor = 1.0;

    int specularBounces = 0;
    int refractiveBounces = 0;
    int giBounces = 0;
    uint currentRayType = RAY_TYPE_PRIMARY;
    float prevPdf = 1.0;
    bool prevIsDelta = false;

    float3 primaryGuideOrigin = camPos;
    float3 primaryGuideDir = rayDirCenter;
    uint primaryGuideRayType = RAY_TYPE_PRIMARY;
    RayPayload primaryGuidePayload = InitRayPayload(RAY_TYPE_PRIMARY);
    bool primaryGuideResolved = false;
    bool primaryGuideThroughTransmission = false;

    RayPayload pretracedPrimaryPayload = InitRayPayload(RAY_TYPE_PRIMARY);
    bool hasPretracedPrimaryPayload = false;
    const int maxPrimaryDeltaSteps = 8;

    for (int deltaStep = 0; deltaStep < maxPrimaryDeltaSteps; ++deltaStep)
    {
        RayDesc guideResolveRay;
        guideResolveRay.Origin = primaryGuideOrigin;
        guideResolveRay.Direction = primaryGuideDir;
        guideResolveRay.TMin = 0.002;
        guideResolveRay.TMax = 10000.0;

        RayPayload guideResolvePayload = InitRayPayload(primaryGuideRayType);
        TraceRay(g_accel, RAY_FLAG_NONE, 0xFF, 0, 0, 0, guideResolveRay, guideResolvePayload);
        SHADER_COUNTER_ADD(SHADER_COUNTER_TRACE_RAYS, 1);

        if (guideResolvePayload.t < 0.0) {
            primaryGuidePayload = guideResolvePayload;
            primaryGuideResolved = true;
            break;
        }

        float4 guideSurface = UnpackPayloadSurface(guideResolvePayload.packedSurface);
        float guideRoughness = max(0.001, guideSurface.x);
        float guideTransmission = guideSurface.z;
        float guideIor = UnpackPayloadIor(guideResolvePayload.packedIorType);
        bool guideThinWalled = UnpackPayloadThinWalled(guideResolvePayload.packedIorType);

        if (!ShouldResolveDeltaTransmission(guideRoughness, guideTransmission,
                                            guideIor)) {
            primaryGuidePayload = guideResolvePayload;
            primaryGuideResolved = true;
            break;
        }

        float3 guideP = primaryGuideOrigin + primaryGuideDir * guideResolvePayload.t;
        float3 guideNextDir = primaryGuideDir;
        if (!guideThinWalled) {
            float3 guideN = UnpackNormalOctahedron(guideResolvePayload.packedNormal);
            float3 guideV = -primaryGuideDir;
            if (!RefractDeterministic(guideV, guideN, guideIor, guideNextDir)) {
                primaryGuidePayload = guideResolvePayload;
                primaryGuideResolved = true;
                break;
            }
        }

        primaryGuideOrigin = guideP + guideNextDir * 0.002;
        primaryGuideDir = guideNextDir;
        primaryGuideRayType = RAY_TYPE_REFRACTION;
        primaryGuideThroughTransmission = true;
    }

    for (int deltaStep = 0; deltaStep < maxPrimaryDeltaSteps; ++deltaStep)
    {
        RayDesc primaryResolveRay;
        primaryResolveRay.Origin = rayOrigin;
        primaryResolveRay.Direction = rayDir;
        primaryResolveRay.TMin = 0.002;
        primaryResolveRay.TMax = 10000.0;

        RayPayload primaryResolvePayload = InitRayPayload(currentRayType);
        TraceRay(g_accel, RAY_FLAG_NONE, 0xFF, 0, 0, 0, primaryResolveRay, primaryResolvePayload);
        SHADER_COUNTER_ADD(SHADER_COUNTER_TRACE_RAYS, 1);

        if (primaryResolvePayload.t < 0.0) {
            pretracedPrimaryPayload = primaryResolvePayload;
            hasPretracedPrimaryPayload = true;
            break;
        }

        float4 resolveSurface = UnpackPayloadSurface(primaryResolvePayload.packedSurface);
        float resolveRoughness = max(0.001, resolveSurface.x);
        float resolveTransmission = resolveSurface.z;
        float resolveIor = UnpackPayloadIor(primaryResolvePayload.packedIorType);
        float3 resolveTransmissionColor =
            UnpackPayloadTransmissionColor(primaryResolvePayload.packedTransmission);
        bool resolveThinWalled = UnpackPayloadThinWalled(primaryResolvePayload.packedIorType);

        if (!ShouldResolveDeltaTransmission(resolveRoughness, resolveTransmission,
                                            resolveIor)) {
            pretracedPrimaryPayload = primaryResolvePayload;
            hasPretracedPrimaryPayload = true;
            break;
        }

        if (refractiveBounces >= (int)maxRefractiveBounces) {
            pretracedPrimaryPayload = primaryResolvePayload;
            hasPretracedPrimaryPayload = true;
            break;
        }

        float3 resolveP = rayOrigin + rayDir * primaryResolvePayload.t;
        float3 resolveN = UnpackNormalOctahedron(primaryResolvePayload.packedNormal);
        float3 resolveV = -rayDir;
        float resolveMetallic = resolveSurface.y;
        float resolveSpecularWeight =
            UnpackPayloadSpecularWeight(primaryResolvePayload.packedIorType);
        float3 resolveAlbedo =
            UnpackPayloadAlbedo(primaryResolvePayload.packedAlbedo);
        float3 resolveSpecularColor =
            UnpackPayloadSpecularColor(primaryResolvePayload.packedSpecular);
        float3 resolveReflection =
            throughput * TraceGlassReflectionRadiance(resolveP, resolveV,
                                                      resolveN, resolveAlbedo,
                                                      resolveMetallic,
                                                      resolveIor,
                                                      resolveSpecularWeight,
                                                      resolveSpecularColor);
        accumulatedColor += resolveReflection;
        primaryGlassReflection += resolveReflection;

        throughput *= max(resolveTransmissionColor, float3(0.0, 0.0, 0.0)) * resolveTransmission;
        float3 resolveNextDir = rayDir;
        if (!resolveThinWalled) {
            if (!RefractDeterministic(resolveV, resolveN, resolveIor, resolveNextDir)) {
                pretracedPrimaryPayload = primaryResolvePayload;
                hasPretracedPrimaryPayload = true;
                break;
            }
        }

        rayOrigin = resolveP + resolveNextDir * 0.002;
        rayDir = resolveNextDir;
        currentRayType = RAY_TYPE_REFRACTION;
        refractiveBounces++;
        prevPdf = 1.0;
        prevIsDelta = true;
    }

    if (!primaryGuideResolved) {
        RayDesc guideResolveRay;
        guideResolveRay.Origin = primaryGuideOrigin;
        guideResolveRay.Direction = primaryGuideDir;
        guideResolveRay.TMin = 0.002;
        guideResolveRay.TMax = 10000.0;
        primaryGuidePayload = InitRayPayload(primaryGuideRayType);
        TraceRay(g_accel, RAY_FLAG_NONE, 0xFF, 0, 0, 0, guideResolveRay, primaryGuidePayload);
        SHADER_COUNTER_ADD(SHADER_COUNTER_TRACE_RAYS, 1);
        primaryGuideResolved = true;
    }

    if (!hasPretracedPrimaryPayload) {
        RayDesc primaryResolveRay;
        primaryResolveRay.Origin = rayOrigin;
        primaryResolveRay.Direction = rayDir;
        primaryResolveRay.TMin = 0.002;
        primaryResolveRay.TMax = 10000.0;
        pretracedPrimaryPayload = InitRayPayload(currentRayType);
        TraceRay(g_accel, RAY_FLAG_NONE, 0xFF, 0, 0, 0, primaryResolveRay, pretracedPrimaryPayload);
        SHADER_COUNTER_ADD(SHADER_COUNTER_TRACE_RAYS, 1);
    }

    for (int bounce = 0; bounce < 32; ++bounce) 
    {
        RayPayload payload;
        if (bounce == 0) {
            payload = pretracedPrimaryPayload;
        } else {
            RayDesc ray;
            ray.Origin = rayOrigin;
            ray.Direction = rayDir;
            ray.TMin = 0.002;
            ray.TMax = 10000.0;

            payload = InitRayPayload(currentRayType);
            TraceRay(g_accel, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
            SHADER_COUNTER_ADD(SHADER_COUNTER_TRACE_RAYS, 1);
        }

        float3 payloadColor = PayloadGetColor(payload);
        float3 payloadAlbedo = UnpackPayloadAlbedo(payload.packedAlbedo);
        float payloadCoatWeight = UnpackPayloadCoatWeight(payload.packedAlbedo);
        float4 payloadSurface = UnpackPayloadSurface(payload.packedSurface);
        float payloadRoughness = max(0.001, payloadSurface.x);
        float payloadMetalness = payloadSurface.y;
        float payloadTransmission = payloadSurface.z;
        float payloadTranslucency = payloadSurface.w;
        float payloadCoatRoughness = max(0.001, PayloadGetCoatRoughness(payload));
        float payloadIor = UnpackPayloadIor(payload.packedIorType);
        float payloadSpecularWeight = UnpackPayloadSpecularWeight(payload.packedIorType);
        float3 payloadTransmissionColor = UnpackPayloadTransmissionColor(payload.packedTransmission);
        float3 payloadSpecularColor = UnpackPayloadSpecularColor(payload.packedSpecular);
        bool payloadThinWalled = UnpackPayloadThinWalled(payload.packedIorType);

        if (bounce == 0) {
            RayPayload guidePayload = payload;
            if (primaryGuideResolved) {
                guidePayload = primaryGuidePayload;
            }
            float3 guideAlbedo = UnpackPayloadAlbedo(guidePayload.packedAlbedo);
            float guideCoatWeight = UnpackPayloadCoatWeight(guidePayload.packedAlbedo);
            float4 guideSurface = UnpackPayloadSurface(guidePayload.packedSurface);
            float guideRoughness = max(0.001, guideSurface.x);
            float guideMetalness = guideSurface.y;
            float guideTransmission = guideSurface.z;
            float guideTranslucency = guideSurface.w;
            float guideIor = UnpackPayloadIor(guidePayload.packedIorType);
            float guideSpecularWeight = UnpackPayloadSpecularWeight(guidePayload.packedIorType);
            float3 guideSpecularColor = UnpackPayloadSpecularColor(guidePayload.packedSpecular);

            if (guidePayload.t >= 0.0) {
                primaryHit = true;
                float3 guideOrigin = primaryGuideResolved ? primaryGuideOrigin : rayOrigin;
                float3 guideDir = primaryGuideResolved ? primaryGuideDir : rayDir;
                primaryPos = guideOrigin + guideDir * guidePayload.t;
                primaryNormal = UnpackNormalOctahedron(guidePayload.packedNormal);
                primaryAlbedo = guideAlbedo;
                primaryIsRefractive = primaryGuideThroughTransmission || (guideTransmission > 0.0f);
                primaryRoughness = guideRoughness;
                
                float3 toHit = primaryPos - camPos;
                primaryViewZ = dot(toHit, forward); 

                // Specular Albedo calculation for DLSS-RR
                float3 F0_primary = ComputeSurfaceF0(guideAlbedo, guideMetalness,
                                                    guideIor, guideSpecularWeight,
                                                    guideSpecularColor);
                float NdotV_primary = saturate(dot(primaryNormal, -guideDir));
                primarySpecAlbedo = EnvBRDFApprox2(F0_primary, primaryRoughness * primaryRoughness, NdotV_primary);

                // Trace a dedicated specular reflection ray to get hit distance for DLSS-RR.
                if (!primaryIsRefractive &&
                    (dlssRayReconstruction > 0.5) &&
                    max(primarySpecAlbedo.r, max(primarySpecAlbedo.g, primarySpecAlbedo.b)) > 0.01) {
                    float3 R_spec = reflect(guideDir, primaryNormal);
                    RayDesc specHitRay;
                    specHitRay.Origin = primaryPos + primaryNormal * 0.002;
                    specHitRay.Direction = R_spec;
                    specHitRay.TMin = 0.002;
                    specHitRay.TMax = 1000.0;
                    RayPayload specHitPayload = InitRayPayload(RAY_TYPE_REFLECTION);
                    TraceRay(g_accel, RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES, 0xFF, 0, 0, 0, specHitRay, specHitPayload);
                    SHADER_COUNTER_ADD(SHADER_COUNTER_SPECULAR_TRACES, 1);
                    primarySpecHitDist = (specHitPayload.t > 0) ? specHitPayload.t : 1000.0;
                } else {
                    primarySpecHitDist = 0.0;
                }

                if (!primaryIsRefractive &&
                    tonemapAoIntensity > 1.0e-4 &&
                    tonemapAoRadiusMeters > 1.0e-4) {
                    uint aoMode = (uint)clamp(tonemapAoMode, 0.0, 2.0);
                    primaryTonemapAoFactor = ComputePrimaryRayTracedAo(
                        primaryPos, primaryNormal, tonemapAoRadiusMeters,
                        aoMode, tonemapAoIntensity, rng);
                }
            }

            // --- G-Buffer & Motion Vector Writing (Mandatory for every frame) ---
            if (!primaryHit || primaryViewZ <= 0.0) {
                g_depth[launchIndex.xy] = (dlssRayReconstruction > 0.5) ? farZ : 1.0;
                g_linearDepth[launchIndex.xy] = farZ;
                float2 mvecSky = float2(0.0, 0.0);
                if (prevValid > 0.5) {
                    float3 guideSkyDir = primaryGuideResolved ? primaryGuideDir : rayDirCenter;
                    float3 forwardP = normalize(prevForward);
                    float3 Rp = normalize(cross(forwardP, prevUp));
                    float3 Up = normalize(cross(Rp, forwardP));
                    float f_inv_p = tan(radians(prevFov) * 0.5);
                    float vxP = dot(guideSkyDir, Rp);
                    float vyP = dot(guideSkyDir, Up);
                    float vzP = dot(guideSkyDir, forwardP);
                    if (vzP > 0.001) {
                        float ndcXP = vxP / (vzP * prevAspect * f_inv_p);
                        float ndcYP = -vyP / (vzP * f_inv_p);
                        float2 prevScreen = (float2(ndcXP, ndcYP) * 0.5 + 0.5) * float2(launchDim.xy);
                        if (any(prevScreen < screenMin) || any(prevScreen > screenMax)) mvecSky = kInvalidMvec;
                        else mvecSky = prevScreen - currScreen;
                    }
                }
                g_motionVectors[launchIndex.xy] = mvecSky;
                g_albedoOut[launchIndex.xy] = float4(0.0, 0.0, 0.0, 1.0);
                g_normalRoughnessOut[launchIndex.xy] = float4(0.0, 1.0, 0.0, 1.0);
                g_specularAlbedo[launchIndex.xy] = float4(0.0, 0.0, 0.0, 1.0);
                g_specHitDistance[launchIndex.xy] = 0.0;
                g_specularMotionVectors[launchIndex.xy] = mvecSky;
            } else {
                g_linearDepth[launchIndex.xy] = primaryViewZ;
                if (dlssRayReconstruction > 0.5) g_depth[launchIndex.xy] = primaryViewZ;
                else {
                    float nearZc = nearZ;
                    float farZc = farZ;
                    float A = farZc / (farZc - nearZc);
                    float B = (-nearZc * farZc) / (farZc - nearZc);
                    float ndcZ = A + (B / primaryViewZ);
                    g_depth[launchIndex.xy] = saturate(ndcZ);
                }
                float2 mvec = kInvalidMvec;
                float2 specMvec = kInvalidMvec;
                if (prevValid > 0.5) {
                    float3 forwardP = normalize(prevForward);
                    float3 Rp = normalize(cross(forwardP, prevUp));
                    float3 Up = normalize(cross(Rp, forwardP));
                    float f_inv_p = tan(radians(prevFov) * 0.5);
                    float3 relP = primaryPos - prevPos;
                    float vxP = dot(relP, Rp);
                    float vyP = dot(relP, Up);
                    float vzP = dot(relP, forwardP);
                    if (vzP > 0.001) {
                        float ndcXP = vxP / (vzP * prevAspect * f_inv_p);
                        float ndcYP = -vyP / (vzP * f_inv_p);
                        float2 prevScreen = (float2(ndcXP, ndcYP) * 0.5 + 0.5) * float2(launchDim.xy);
                        if (any(prevScreen < screenMin) || any(prevScreen > screenMax)) mvec = kInvalidMvec;
                        else mvec = prevScreen - currScreen;
                    }
                    if (any(primarySpecAlbedo > 0.0)) specMvec = mvec;
                }
                g_motionVectors[launchIndex.xy] = mvec;
                g_specularMotionVectors[launchIndex.xy] = specMvec;
                g_albedoOut[launchIndex.xy] =
                    float4(primaryAlbedo, primaryTonemapAoFactor);
                g_normalRoughnessOut[launchIndex.xy] = float4(normalize(primaryNormal), primaryRoughness);
                g_specularAlbedo[launchIndex.xy] = float4(primarySpecAlbedo, 1.0);
                g_specHitDistance[launchIndex.xy] = primarySpecHitDist;
            }

            // --- Adaptive Sampling Early Exit (Now after G-buffers are fresh) ---
            if (!debugViewActive && maxSPP > 0.0 && accumFrame >= (uint)maxSPP) {
                float4 total = g_accumulation[launchIndex.xy];
                if (total.a > 0.0) {
                    float3 meanColor = total.rgb / total.a;
                    g_output[launchIndex.xy] = float4(meanColor, 1.0);
                }
                return;
            }

            if (accumFrame > kAdaptiveStartSpp && useAdaptiveSampling > 0.5) {
                float4 acc = g_accumulation[launchIndex.xy];
                float accM2 = g_variance[launchIndex.xy];
                if (acc.a > (float)kAdaptiveMinPerPixelSpp) {
                    float n_acc = acc.a;
                    float3 meanColor = acc.rgb / n_acc;
                    float meanLum = dot(meanColor, float3(0.2126, 0.7152, 0.0722));
                    float sem = sqrt(max(0.0, accM2)) / n_acc;
                    float noise = sem / (max(0.01, meanLum) + 0.001);
                    float edgeContrast = 0.0;
                    if (launchIndex.x > 0) {
                        float4 a = g_accumulation[launchIndex.xy + uint2(-1, 0)];
                        if (a.a > 1.0) edgeContrast = max(edgeContrast, abs(dot(a.rgb/a.a, float3(0.2126,0.7152,0.0722)) - meanLum));
                    }
                    if (launchIndex.x + 1 < launchDim.x) {
                        float4 a = g_accumulation[launchIndex.xy + uint2(1, 0)];
                        if (a.a > 1.0) edgeContrast = max(edgeContrast, abs(dot(a.rgb/a.a, float3(0.2126,0.7152,0.0722)) - meanLum));
                    }
                    if (launchIndex.y > 0) {
                        float4 a = g_accumulation[launchIndex.xy + uint2(0, -1)];
                        if (a.a > 1.0) edgeContrast = max(edgeContrast, abs(dot(a.rgb/a.a, float3(0.2126,0.7152,0.0722)) - meanLum));
                    }
                    if (launchIndex.y + 1 < launchDim.y) {
                        float4 a = g_accumulation[launchIndex.xy + uint2(0, 1)];
                        if (a.a > 1.0) edgeContrast = max(edgeContrast, abs(dot(a.rgb/a.a, float3(0.2126,0.7152,0.0722)) - meanLum));
                    }
                    bool isEdgeRegion = edgeContrast > kAdaptiveEdgeContrastThreshold;
                    float relThreshold = max(0.001, noiseThreshold * (isEdgeRegion ? kAdaptiveEdgeRelScale : kAdaptiveRelScale));
                    float absSemThreshold = max(kAdaptiveAbsSemFloor, noiseThreshold * (isEdgeRegion ? kAdaptiveEdgeAbsSemScale : kAdaptiveAbsSemScale));
                    if (noise < relThreshold || sem < absSemThreshold) {
                                 bool keepSampling = true;
                                 if (n_acc < (float)accumFrame * kAdaptiveMinExpectedRatio) {
                                     keepSampling = true;
                                 } else if (exportAdaptiveMode) {
                                     keepSampling = false;
                                 } else {
                                     RNG adaptiveRng = init_rng(launchIndex.xy + uint2(0x9e37u, 0x7f4au), frame ^ 0xA511E9B3u);
                                     float proximity = min(saturate(noise / relThreshold), saturate(sem / absSemThreshold));
                                     float keepProb = max(isEdgeRegion ? kAdaptiveEdgeMinKeepProb : kAdaptiveMinKeepProb, proximity);
                                     keepProb = max(keepProb, saturate(((float)accumFrame - n_acc) / (float)accumFrame) * kAdaptiveLagKeepScale);
                                     keepSampling = next_float(adaptiveRng) <= keepProb;
                                 }

                                 if (!keepSampling) {
                            if (flip) {
                                g_reservoir1[launchIndex.xy] = g_reservoir0[launchIndex.xy];
                                g_gi_reservoir_a0[launchIndex.xy] = g_gi_reservoir_b0[launchIndex.xy];
                                g_gi_reservoir_a1[launchIndex.xy] = g_gi_reservoir_b1[launchIndex.xy];
                                g_gi_reservoir_a2[launchIndex.xy] = g_gi_reservoir_b2[launchIndex.xy];
                            } else {
                                g_reservoir0[launchIndex.xy] = g_reservoir1[launchIndex.xy];
                                g_gi_reservoir_b0[launchIndex.xy] = g_gi_reservoir_a0[launchIndex.xy];
                                g_gi_reservoir_b1[launchIndex.xy] = g_gi_reservoir_a1[launchIndex.xy];
                                g_gi_reservoir_b2[launchIndex.xy] = g_gi_reservoir_a2[launchIndex.xy];
                            }
                            SHADER_COUNTER_ADD(SHADER_COUNTER_RESERVOIR_WRITES, 1);
                            if (SHADER_DEBUG_VIS_MODE == 1.0) g_output[launchIndex.xy] = float4(0.0, 1.0, 0.0, 1.0);
                            else g_output[launchIndex.xy] = float4(meanColor, 1.0);
                            return;
                         }
                    }
                }
            }
        }

        if (payload.t < 0.0) {
            // Miss: add sky color and terminate
            // RR is very sensitive to sky shimmer. For the primary ray, sample
            // the environment using a non-jittered ray direction.
            float3 missColor = payloadColor;
            if (bounce == 0 && dlssRayReconstruction > 0.5) {
                float2 skyUv = DirectionToUVRotated(rayDirCenter);
                // Slight mip bias helps remove residual HDRI aliasing that shows up
                // as shimmer, especially along silhouettes.
                float rrSkyLod = clamp(log2(max(length(rayOrigin - camPos), 1e-3) * 0.02), 0.0, 10.0);

                float3 rrSky = envMap.SampleLevel(linearSampler, skyUv, rrSkyLod).rgb *
                               GetDxrProceduralSkyBoost() * intensity;
                float3 rrColor = rrSky;

                // Keep sun disc behavior consistent with miss/skybox shading.
                float3 L = normalize(lightDir.xyz);
                float cosTheta = dot(normalize(rayDirCenter), L);
                float cosSunRadius = cos(lightDir.w);
                if (cosTheta > cosSunRadius) {
                    float sunSolidAngle = 2.0f * PI * (1.0f - cosSunRadius);
                    float3 sunRadiance = (lightColor.rgb * lightColor.w) / max(sunSolidAngle, 1e-7f);
                    const float dxrSunDiscMatchGain = 1.12f;
                    rrColor = sunRadiance * intensity * dxrSunDiscMatchGain;
                }

                if (cloudRenderingEnabled > 0.5f) {
                    float4 baked = bakedClouds.SampleLevel(linearSampler, skyUv, rrSkyLod);
                    baked.a = saturate(baked.a);
                    baked.rgb = max(baked.rgb, 0.0);
                    float opacity = 1.0f - baked.a;
                    float denseCore = pow(saturate(opacity), 2.2f);
                    float skyLeak = 0.10f * denseCore;
                    missColor = baked.rgb + rrColor * (baked.a + skyLeak);
                    missColor += rrColor * (0.025f * denseCore);
                    missColor = clamp(missColor, 0.0f, 100000.0f);
                } else {
                    missColor = rrColor;
                }
            }
            
            if (bounce > 0 && bounce <= 2 && !prevIsDelta) {
                float pdfLight = evaluate_env_map_pdf(envConditionalCdf, envMarginalCdf, rayDir);
                float misW = (prevPdf * prevPdf) / (prevPdf * prevPdf + pdfLight * pdfLight + 1e-12);
                missColor *= misW;
            }

            // Secondary misses are indirect environment transport seen through
            // BRDF paths. Boost those only so models receive stronger sky GI
            // while the primary visible sky remains unchanged.
            if (bounce > 0 && currentRayType == RAY_TYPE_DIFFUSE) {
                missColor *= kEnvLightingBoost;
            }
            
            // If ReSTIR GI is enabled, it already evaluated the first diffuse bounce.
            // Avoid double-counting by ignoring the main path tracer's first diffuse bounce.
            if (!(bounce == 1 && maxGIBounces > 0.0 && currentRayType == RAY_TYPE_DIFFUSE)) {
                accumulatedColor += throughput * missColor;
            }

            // On first bounce miss, update reservoir to empty
            if (bounce == 0) {
                float4 res_data = pack_reservoir(init_reservoir());
                if (flip) { g_reservoir1[launchIndex.xy] = res_data; SHADER_COUNTER_ADD(SHADER_COUNTER_RESERVOIR_WRITES, 1); }
                else      { g_reservoir0[launchIndex.xy] = res_data; SHADER_COUNTER_ADD(SHADER_COUNTER_RESERVOIR_WRITES, 1); }
            }
            break;
        }

        // Legacy material/cloud debug modes (1..16) visualize primary-hit payloads.
        // New accumulation diagnostics (17+) must run full path-tracing flow.
        if (SHADER_DEBUG_MODE > 0.0 && SHADER_DEBUG_MODE <= 16.0) {
            accumulatedColor = payloadColor;
            break;
        }

        float3 N = UnpackNormalOctahedron(payload.packedNormal);
        float3 P = rayOrigin + rayDir * payload.t;
        float3 V = -rayDir;
        float roughness = payloadRoughness;
        float metallic = payloadMetalness;
        float transmission = saturate(payloadTransmission);
        float3 diffuseAlbedo = payloadAlbedo * (1.0 - metallic) * (1.0 - transmission);

        // 1. Direct Lighting (Next Event Estimation + ReSTIR for 1st bounce)
        float3 directLighting = float3(0, 0, 0);
        float3 indirectLighting = float3(0, 0, 0);
        
        if (bounce == 0) {
            // --- ReSTIR DI Logic for Primary Hit ---
            Reservoir res = init_reservoir();
            
            // A. Initial Candidate Sampling
            // Sample Sun (Directional)
            {
                LightSample ls = evaluate_directional_light(lightDir.xyz, lightColor.rgb, lightColor.w);
                float NdotL = saturate(dot(N, ls.L));

                float3 brdf = EvaluateSurfaceBrdf(N, V, ls.L,
                                                  payloadAlbedo, diffuseAlbedo,
                                                  metallic, roughness,
                                                  payloadIor, payloadSpecularWeight,
                                                  payloadSpecularColor,
                                                  payloadCoatWeight, payloadCoatRoughness);

                float p_target = calculate_p_target(ls.radiance, payloadAlbedo, brdf, NdotL);
                update_reservoir(res, 0xFFFFFFFF, p_target, rng);
            }

            // Sample random local light
            uint numLights = (uint)lightCount;
            if (numLights > 0) {
                uint lightIdx = next_uint(rng) % numLights;
                Light l = g_lights[lightIdx];
                
                LightSample ls;
                if (l.type == LIGHT_TYPE_AREA_RECT || l.type == LIGHT_TYPE_AREA_DISK) {
                    ls = sample_area_light(l, P, next_float2(rng));
                } else {
                    ls = evaluate_light(l, P);
                }
                
                float NdotL = saturate(dot(N, ls.L));
                float3 brdf = EvaluateSurfaceBrdf(N, V, ls.L,
                                                  payloadAlbedo, diffuseAlbedo,
                                                  metallic, roughness,
                                                  payloadIor, payloadSpecularWeight,
                                                  payloadSpecularColor,
                                                  payloadCoatWeight, payloadCoatRoughness);

                float p_target = calculate_p_target(ls.radiance, payloadAlbedo, brdf, NdotL) * (float)numLights;
                if (l.type == LIGHT_TYPE_AREA_RECT || l.type == LIGHT_TYPE_AREA_DISK) {
                     p_target *= (1.0 / max(1e-6, ls.pdf));
                }

                update_reservoir(res, lightIdx, p_target, rng);
            }

            // C. Final ReSTIR DI Shading & Finalization
            float3 L_final = float3(0.0, 1.0, 0.0);
            float dist_final = 1e10;
            float3 radiance_final = float3(0.0, 0.0, 0.0);

            if (res.lightIndex == 0xFFFFFFFF) {
                L_final = normalize(lightDir.xyz);
                dist_final = 1000.0;
                radiance_final = lightColor.rgb * lightColor.w;
            } else if (res.lightIndex < numLights) {
                Light l = g_lights[res.lightIndex];
                LightSample ls;
                if (l.type == LIGHT_TYPE_AREA_RECT || l.type == LIGHT_TYPE_AREA_DISK) {
                    // For now, re-sample. In a production ReSTIR, we'd store the sample point.
                    ls = sample_area_light(l, P, next_float2(rng)); 
                } else {
                    ls = evaluate_light(l, P);
                }
                L_final = ls.L;
                dist_final = ls.dist;
                radiance_final = ls.radiance;
            } else {
                radiance_final = float3(0,0,0);
            }

            float NdotL_final = saturate(dot(N, L_final));
            float3 H_f = normalize(L_final + V);
            float3 brdf_f = EvaluateSurfaceBrdf(N, V, L_final,
                                               payloadAlbedo, diffuseAlbedo,
                                               metallic, roughness,
                                               payloadIor, payloadSpecularWeight,
                                               payloadSpecularColor,
                                               payloadCoatWeight, payloadCoatRoughness);

            float p_target_final = calculate_p_target(radiance_final, payloadAlbedo, brdf_f, NdotL_final);
            
            finalize_reservoir(res, p_target_final);

            float4 packed_res = pack_reservoir(res);
            if (flip) { g_reservoir1[launchIndex.xy] = packed_res; SHADER_COUNTER_ADD(SHADER_COUNTER_RESERVOIR_WRITES, 1); }
            else      { g_reservoir0[launchIndex.xy] = packed_res; SHADER_COUNTER_ADD(SHADER_COUNTER_RESERVOIR_WRITES, 1); }

            // D. Apply Visibility
            if (p_target_final > 0.0 && isfinite(dist_final)) {
                RayDesc shadowRay;
                shadowRay.Origin = P + N * 0.002;
                
                if (res.lightIndex == 0xFFFFFFFF && lightDir.w > 0.0) {
                     shadowRay.Direction = SampleCone(L_final, cos(lightDir.w), next_float2(rng));
                } else {
                     shadowRay.Direction = L_final;
                }
                shadowRay.TMin = 0.001;
                shadowRay.TMax = max(0.001, dist_final - 0.002);

                RayQuery<RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q;
                q.TraceRayInline(g_accel, RAY_FLAG_NONE, 0xFF, shadowRay);

                // Drain the query so non-opaque candidates (e.g. glass) do not
                // early-out visibility before opaque blockers behind them.
                while (q.Proceed()) {}
                
                if (q.CommittedStatus() == COMMITTED_NOTHING) {
                    float pdfLight = (res.W > 0.0) ? (1.0 / res.W) : 0.0;

                    float coatProb = 0.0;
                    float specProb = 0.0;
                    float diffProb = 0.0;
                    float transProb = 0.0;
                    float totalProb = 1.0;
                    ComputeLobeProbabilities(N, V, payloadAlbedo,
                                             metallic, transmission,
                                             payloadTranslucency,
                                             payloadIor, payloadSpecularWeight,
                                             payloadSpecularColor,
                                             payloadCoatWeight,
                                             coatProb, specProb, diffProb, transProb, totalProb);

                    float NdotH_final = saturate(dot(N, H_f));
                    float VdotH_final = saturate(dot(V, H_f));

                    float pdfCoat = 0.0;
                    if (coatProb > 0.0 && NdotH_final > 0.0 && VdotH_final > 0.0) {
                        pdfCoat = (PDF_GGX(NdotH_final, VdotH_final, payloadCoatRoughness) * coatProb) / totalProb;
                    }
                    float pdfSpec = 0.0;
                    if (specProb > 0.0 && NdotH_final > 0.0 && VdotH_final > 0.0) {
                        pdfSpec = (PDF_GGX(NdotH_final, VdotH_final, roughness) * specProb) / totalProb;
                    }
                    float pdfDiff = 0.0;
                    if (diffProb > 0.0 && NdotL_final > 0.0) {
                        pdfDiff = (PDF_Lambert(NdotL_final) * diffProb) / totalProb;
                    }
                    
                    float pdfBrdf = pdfCoat + pdfSpec + pdfDiff;
                    float misW = (pdfLight * pdfLight) / (pdfLight * pdfLight + pdfBrdf * pdfBrdf + 1e-12);
                    
                    directLighting = radiance_final * brdf_f * NdotL_final * res.W * misW;
                }
            }

            // --- ReSTIR GI (Indirect Illumination) ---
            // Respect GI bounce budget: 0 means disable indirect GI entirely.
            if (maxGIBounces > 0.0) {
                GI_Reservoir gi_res = init_gi_reservoir();
                // A. Initial Candidate
                {
                    float3 nextDir_gi; float pdf_gi; float3 f_brdf_gi; float2 u_gi = next_float2(rng);
                    
                    // ReSTIR GI only handles diffuse. Specular and refraction are handled by the main path tracer.
                    nextDir_gi = SampleLambert(u_gi, N);
                    float NdotL = saturate(dot(N, nextDir_gi));
                    pdf_gi = PDF_Lambert(NdotL);
                    f_brdf_gi = (diffuseAlbedo / PI);
                    
                    if (pdf_gi > 0.0) {
                        RayDesc giRay; giRay.Origin = P + N * 0.002; giRay.Direction = nextDir_gi;
                        giRay.TMin = 0.0001; giRay.TMax = 1000.0;
                        RayPayload giPayload = InitRayPayload(RAY_TYPE_GI_EVAL);
                        TraceRay(g_accel, RAY_FLAG_NONE, 0xFF, 0, 0, 0, giRay, giPayload);
                        SHADER_COUNTER_ADD(SHADER_COUNTER_TRACE_RAYS, 1);
                        // Include sky/clouds in GI radiance
                        float3 giColor = PayloadGetColor(giPayload);
                        float3 radiance = giColor;
                        float3 hitPos = (giPayload.t > 0.0)
                            ? (giRay.Origin + giRay.Direction * giPayload.t)
                            : (P + nextDir_gi * 1000.0);
                        
                        float p_target = length(radiance * f_brdf_gi * saturate(dot(N, nextDir_gi)));
                        // Clamp weight to prevent fireflies from rare but bright background samples
                        float ris_weight = min(p_target / max(1e-5, pdf_gi), 1e5);
                        update_gi_reservoir(gi_res, hitPos, radiance, ris_weight, rng);
                    }
                }
                // Temporal/spatial GI reuse moved to dedicated compute pass
                // (restir_gi_spatial_cs.hlsl). RayGen keeps initial candidate
                // generation and per-frame visibility evaluation.
                // Finalize GI
                float3 L_gi_final = normalize(gi_res.hitPos - P);
                float3 brdf_gi_final = (diffuseAlbedo / PI);
                float p_target_final_gi = length(gi_res.radiance * brdf_gi_final * saturate(dot(N, L_gi_final)));
                finalize_gi_reservoir(gi_res, p_target_final_gi);
                float4 out_d0, out_d1, out_d2; pack_gi_reservoir(gi_res, out_d0, out_d1, out_d2);
                if (flip) { g_gi_reservoir_a0[launchIndex.xy] = out_d0; g_gi_reservoir_a1[launchIndex.xy] = out_d1; g_gi_reservoir_a2[launchIndex.xy] = out_d2; }
                else      { g_gi_reservoir_b0[launchIndex.xy] = out_d0; g_gi_reservoir_b1[launchIndex.xy] = out_d1; g_gi_reservoir_b2[launchIndex.xy] = out_d2; }
                
                if (gi_res.W > 0.0) {
                    // Visibility test for GI reconnection
                    RayDesc giVisRay; giVisRay.Origin = P + N * 0.002; giVisRay.Direction = L_gi_final;
                    giVisRay.TMin = 0.001; giVisRay.TMax = max(0.001, distance(gi_res.hitPos, P) - 0.003);
                    
                    RayQuery<RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q_gi;
                    q_gi.TraceRayInline(g_accel, RAY_FLAG_NONE, 0xFF, giVisRay);
                    while (q_gi.Proceed()) {}
                    
                    SHADER_COUNTER_ADD(SHADER_COUNTER_TRACE_RAYS, 1);
                    if (q_gi.CommittedStatus() == COMMITTED_NOTHING) {
                        indirectLighting = gi_res.radiance * brdf_gi_final * saturate(dot(N, L_gi_final)) * gi_res.W;
                    }
                }
            } else {
                float4 zero4 = float4(0.0, 0.0, 0.0, 0.0);
                if (flip) {
                    g_gi_reservoir_a0[launchIndex.xy] = zero4;
                    g_gi_reservoir_a1[launchIndex.xy] = zero4;
                    g_gi_reservoir_a2[launchIndex.xy] = zero4;
                } else {
                    g_gi_reservoir_b0[launchIndex.xy] = zero4;
                    g_gi_reservoir_b1[launchIndex.xy] = zero4;
                    g_gi_reservoir_b2[launchIndex.xy] = zero4;
                }
            }
        } 
        else 
        {
            // Simple NEE for subsequent bounces
            uint numLights = (uint)lightCount;
            float3 L_nee;
            float3 radiance_nee;
            float dist_nee;
            float neeWeight = 1.0; // PDF compensation factor
            if (numLights == 0 || next_float(rng) < 0.5) {
                // Sample Sun (probability = numLights > 0 ? 0.5 : 1.0)
                L_nee = normalize(lightDir.xyz);
                radiance_nee = lightColor.rgb * lightColor.w;
                dist_nee = 1000.0;
                neeWeight = (numLights > 0) ? 2.0 : 1.0;
            } else {
                // Sample random local light (probability = 0.5 * 1/numLights)
                uint lightIdx = next_uint(rng) % numLights;
                Light l = g_lights[lightIdx];
                LightSample ls;
                if (l.type == LIGHT_TYPE_AREA_RECT || l.type == LIGHT_TYPE_AREA_DISK) {
                    ls = sample_area_light(l, P, next_float2(rng));
                } else {
                    ls = evaluate_light(l, P);
                }
                L_nee = ls.L;
                dist_nee = ls.dist;
                radiance_nee = ls.radiance * (float)numLights;
                neeWeight = 2.0;
                if (l.type == LIGHT_TYPE_AREA_RECT || l.type == LIGHT_TYPE_AREA_DISK) {
                    radiance_nee *= (1.0 / max(1e-6, ls.pdf));
                }
            }

            float NdotL_nee = saturate(dot(N, L_nee));
            if (NdotL_nee > 0 && isfinite(dist_nee)) {
                RayDesc shadowRay;
                shadowRay.Origin = P + N * 0.002;
                shadowRay.Direction = L_nee;
                shadowRay.TMin = 0.001;
                shadowRay.TMax = max(0.001, dist_nee - 0.001);
                
                RayQuery<RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q_nee;
                q_nee.TraceRayInline(g_accel, RAY_FLAG_NONE, 0xFF, shadowRay);
                while (q_nee.Proceed()) {}
                
                SHADER_COUNTER_ADD(SHADER_COUNTER_TRACE_RAYS, 1);
                SHADER_COUNTER_ADD(SHADER_COUNTER_SHADOW_TRACES, 1);
                if (q_nee.CommittedStatus() == COMMITTED_NOTHING) {
                     float3 brdf = EvaluateSurfaceBrdf(N, V, L_nee,
                                                payloadAlbedo, diffuseAlbedo,
                                                metallic, roughness,
                                                payloadIor, payloadSpecularWeight,
                                                payloadSpecularColor,
                                                payloadCoatWeight, payloadCoatRoughness);
                     
                     // Trial 2: MIS for NEE
                     float distanceSquared = dist_nee * dist_nee + 1.0;
                     float pdfLight = distanceSquared / (max(0.001, NdotL_nee)); // Approximation of point light PDF, actually delta lights can't easily do MIS.
                     // Since we only have delta point lights and directional sun, MIS with BRDF is degenerate.
                     // But for robustness if area lights are added, we set misW = 1.0 for delta lights.
                     float misW = 1.0; 
                     
                     directLighting = brdf * radiance_nee * NdotL_nee * neeWeight * misW;
                }
            }
        }

        // Environment NEE (importance sampled lat-long CDF)
        // Skip for deep bounces: in indoor scenes, env shadow rays almost always hit
        // walls/ceiling. BRDF-sampled misses still capture env light unweighted.
        if (bounce <= 1)
        {
            float envSampleLod = clamp(log2(max(length(P - camPos), 1e-3) * 0.02) + ((bounce > 0) ? 0.35 : 0.0), 0.0, 10.0);
            LightSample envLs = sample_env_map(envMap, envConditionalCdf, envMarginalCdf, linearSampler, rng, envSampleLod);
            SHADER_COUNTER_ADD(SHADER_COUNTER_ENV_SAMPLES, 1);

            float NdotL_env = saturate(dot(N, envLs.L));
            if (envLs.pdf > 1e-8 && NdotL_env > 0.0) {
                RayDesc envShadowRay;
                envShadowRay.Origin = P + N * 0.002;
                envShadowRay.Direction = envLs.L;
                envShadowRay.TMin = 0.001;
                envShadowRay.TMax = 10000.0;

                RayQuery<RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q_env;
                q_env.TraceRayInline(g_accel, RAY_FLAG_NONE, 0xFF, envShadowRay);
                while (q_env.Proceed()) {}
                
                SHADER_COUNTER_ADD(SHADER_COUNTER_TRACE_RAYS, 1);
                SHADER_COUNTER_ADD(SHADER_COUNTER_SHADOW_TRACES, 1);

                if (q_env.CommittedStatus() == COMMITTED_NOTHING) {
                    float3 H_env = normalize(envLs.L + V);
                    float3 brdf_env = EvaluateSurfaceBrdf(N, V, envLs.L,
                                                          payloadAlbedo, diffuseAlbedo,
                                                          metallic, roughness,
                                                          payloadIor, payloadSpecularWeight,
                                                          payloadSpecularColor,
                                                          payloadCoatWeight, payloadCoatRoughness);

                    // MIS: compare environment-light sampling PDF against
                    // the BRDF sampling PDF for this same direction.
                    float coatProbEnv = 0.0;
                    float specProbEnv = 0.0;
                    float diffProbEnv = 0.0;
                    float transProbEnv = 0.0;
                    float totalProbEnv = 1.0;
                    ComputeLobeProbabilities(N, V, payloadAlbedo,
                                             metallic, transmission,
                                             payloadTranslucency,
                                             payloadIor, payloadSpecularWeight,
                                             payloadSpecularColor,
                                             payloadCoatWeight,
                                             coatProbEnv, specProbEnv, diffProbEnv, transProbEnv, totalProbEnv);

                    float NdotH_env = saturate(dot(N, H_env));
                    float VdotH_env = saturate(dot(V, H_env));
                    float pdfCoatEnv = 0.0;
                    if (coatProbEnv > 0.0 && NdotH_env > 0.0 && VdotH_env > 0.0) {
                        pdfCoatEnv =
                            (PDF_GGX(NdotH_env, VdotH_env, payloadCoatRoughness) * coatProbEnv) /
                            totalProbEnv;
                    }
                    float pdfSpecEnv = 0.0;
                    if (specProbEnv > 0.0 && NdotH_env > 0.0 && VdotH_env > 0.0) {
                        pdfSpecEnv =
                            (PDF_GGX(NdotH_env, VdotH_env, roughness) * specProbEnv) /
                            totalProbEnv;
                    }
                    float pdfDiffEnv = 0.0;
                    if (diffProbEnv > 0.0) {
                        pdfDiffEnv = (PDF_Lambert(NdotL_env) * diffProbEnv) / totalProbEnv;
                    }
                    float pdfBrdfEnv = max(0.0, pdfCoatEnv + pdfSpecEnv + pdfDiffEnv);

                    float pdfLightEnv = max(1e-8, envLs.pdf);
                    float misW = (pdfLightEnv * pdfLightEnv) /
                                 (pdfLightEnv * pdfLightEnv +
                                  pdfBrdfEnv * pdfBrdfEnv + 1e-12);

                    // Keep environment NEE exposure-consistent with miss/sky
                    // shading paths, which already apply camera intensity.
                    float3 envRadiance = envLs.radiance * intensity;
                    float3 envContrib = brdf_env * envRadiance *
                                        (NdotL_env / pdfLightEnv) * misW;
                    envContrib *= kEnvLightingBoost;
                    directLighting += envContrib;
                }
            }
        } // end env NEE (bounce <= 1)

        // If ReSTIR GI is enabled, it already evaluated the first diffuse bounce.
        // Avoid double-counting by ignoring the main path tracer's first diffuse bounce.
        if (bounce == 1 && maxGIBounces > 0.0 && currentRayType == RAY_TYPE_DIFFUSE) {
            directLighting = float3(0, 0, 0);
            payloadColor = float3(0, 0, 0);
        }

        float3 surfaceLightingContribution =
            throughput * (directLighting + indirectLighting);
        accumulatedColor += surfaceLightingContribution + throughput * payloadColor;

        // 2. Indirect Lighting Ray Generation
        float3 nextDir;
        float pdf;
        float3 f_brdf;
        float2 u = float2(next_float(rng), next_float(rng));

        // Refraction / Glass logic
        bool isRefractive = payloadTransmission > 0.0;
        if (isRefractive) {
            float3 glassL;
            bool refracted = false;
            bool glassIsDelta = IsDeltaGlass(roughness);

            // Thin-walled mode: window glass approximation (no bending).
            // For clear architectural window glass, stochastic Fresnel branch
            // selection creates visible salt-and-pepper noise because adjacent
            // pixels randomly flip between reflection and transmission. Treat
            // that primary path as deterministic transmission instead.
            bool deterministicThinGlass =
                payloadThinWalled &&
                (bounce == 0) &&
                (payloadTransmission > 0.0);
            if (deterministicThinGlass) {
                refracted = true;
                glassL = SampleThinGlassTransmission(V, roughness, u);
            } else if (glassIsDelta && payloadThinWalled) {
                float cosTheta = abs(dot(V, N));
                float F = FresnelDielectric(cosTheta, payloadIor);
                if (u.x < F) {
                    refracted = false;
                    glassL = reflect(-V, N);
                } else {
                    refracted = true;
                    glassL = rayDir; // straight-through
                }
            } else if (glassIsDelta) {
                refracted = SampleGlass(V, N, payloadIor, u, glassL);
            } else {
                SampleRoughDielectric(V, N, roughness, payloadIor,
                                      u, next_float(rng),
                                      glassL, refracted);
            }

            if (refracted) {
                if (refractiveBounces >= (int)maxRefractiveBounces) break;
                if (bounce == 0) {
                    if (deterministicThinGlass) {
                        float3 reflectedColor =
                            TraceGlassReflectionRadiance(P, V, N,
                                                         payloadAlbedo,
                                                         metallic,
                                                         payloadIor,
                                                         payloadSpecularWeight,
                                                         payloadSpecularColor);
                        primaryGlassReflection =
                            max(surfaceLightingContribution + reflectedColor,
                                float3(0.0, 0.0, 0.0));
                        accumulatedColor += primaryGlassReflection;
                    }
                    accumulatedColor -= surfaceLightingContribution;
                }
                refractiveBounces++;
                nextDir = glassL;
                f_brdf = max(payloadTransmissionColor, float3(0.0, 0.0, 0.0)) * payloadTransmission;
                currentRayType = RAY_TYPE_REFRACTION;
            } else {
                if (specularBounces >= (int)maxSpecularBounces) break;
                specularBounces++;
                nextDir = glassL;
                f_brdf = float3(1,1,1);
                currentRayType = RAY_TYPE_REFLECTION;
            }
            pdf = 1.0;
            rayOrigin = P + nextDir * 0.002; 
            // For glass, the cosine term and PDF often cancel out in simple path tracers,
            // but we'll manually update throughput here to ensure it's correct.
            throughput *= f_brdf;
            // Skip the standard PBR throughput update
            rayDir = nextDir;
            prevPdf = pdf;
            prevIsDelta = glassIsDelta;
            continue; 
        } else {
            // Metallic / Diffuse PBR sampling (+ optional diffuse translucency)
            float coatProb = 0.0;
            float specProb = 0.0;
            float diffProb = 0.0;
            float transProb = 0.0;
            float totalProb = 1.0;
            ComputeLobeProbabilities(N, V, payloadAlbedo,
                                     metallic, transmission,
                                     payloadTranslucency,
                                     payloadIor, payloadSpecularWeight,
                                     payloadSpecularColor,
                                     payloadCoatWeight,
                                     coatProb, specProb, diffProb, transProb, totalProb);

            float pick = next_float(rng) * totalProb;
            float cosineTerm = 1.0;
            bool sampledDelta = false;

            if (pick < coatProb) {
                if (specularBounces >= (int)maxSpecularBounces) break;
                specularBounces++;
                sampledDelta = IsDeltaSpecular(payloadCoatRoughness);

                float3 H = SampleGGX(u, N, payloadCoatRoughness);
                nextDir = reflect(-V, H);
                float NdotL;
                float NdotH;
                float VdotH;
                StabilizeSpecularSample(N, V, H, nextDir,
                                         NdotL, NdotH, VdotH);

                pdf = (PDF_GGX(NdotH, VdotH, payloadCoatRoughness) * coatProb) / totalProb;
                f_brdf = EvaluateCoatSpecular(N, V, nextDir, payloadCoatRoughness);
                rayOrigin = P + N * 0.002;
                cosineTerm = NdotL;
                currentRayType = RAY_TYPE_REFLECTION;
            } else if (pick < (coatProb + specProb)) {
                // Specular GGX
                if (specularBounces >= (int)maxSpecularBounces) break;
                specularBounces++;
                sampledDelta = IsDeltaSpecular(roughness);

                float3 H = SampleGGX(u, N, roughness);
                nextDir = reflect(-V, H);
                float NdotL;
                float NdotH;
                float VdotH;
                StabilizeSpecularSample(N, V, H, nextDir,
                                         NdotL, NdotH, VdotH);
                float3 F0 = ComputeSurfaceF0(payloadAlbedo, metallic, payloadIor,
                                             payloadSpecularWeight,
                                             payloadSpecularColor);

                pdf = (PDF_GGX(NdotH, VdotH, roughness) * specProb) / totalProb;
                f_brdf = D_GGX(NdotH, roughness) * V_SmithCorrelated(max(0.0, dot(N, V)), NdotL, roughness) * F_Schlick(VdotH, F0);
                rayOrigin = P + N * 0.002;
                cosineTerm = NdotL;
                currentRayType = RAY_TYPE_REFLECTION;
            } else if (pick < (coatProb + specProb + diffProb)) {
                // Diffuse Lambert
                if (giBounces >= (int)maxGIBounces) break;
                giBounces++;

                nextDir = SampleLambert(u, N);
                float NdotL = saturate(dot(N, nextDir));
                pdf = (PDF_Lambert(NdotL) * diffProb) / totalProb;
                f_brdf = diffuseAlbedo / PI;
                rayOrigin = P + N * 0.002;
                cosineTerm = NdotL;
                currentRayType = RAY_TYPE_DIFFUSE;
            } else {
                // Diffuse translucency (transmission) Lambert
                if (giBounces >= (int)maxGIBounces) break;
                giBounces++;

                nextDir = SampleLambert(u, -N);
                float NdotL_t = saturate(dot(-N, nextDir));
                pdf = (PDF_Lambert(NdotL_t) * transProb) / totalProb;
                f_brdf = (payloadAlbedo / PI) * (1.0 - metallic);
                rayOrigin = P - N * 0.002;
                cosineTerm = NdotL_t;
                currentRayType = RAY_TYPE_DIFFUSE;
            }

            if (!(pdf > 0.0)) break;
            throughput *= (f_brdf * cosineTerm) / pdf;
            rayDir = nextDir;
            prevPdf = pdf;
            prevIsDelta = sampledDelta;
            
            // Russian Roulette (Albedo-guided)
            if (bounce >= 2) {
                float maxThroughput = max(throughput.x, max(throughput.y, throughput.z));
                // Additionally factor in the current surface's theoretical max contribution (albedo + specular). 
                // A dark surface shouldn't spawn a ray just because previous throughput was high.
                float maxAlbedo = max(payloadAlbedo.x, max(payloadAlbedo.y, payloadAlbedo.z));
                float maxSpec = max(max(specProb, coatProb), 0.04);
                float surfaceMax = max(maxAlbedo, maxSpec);
                
                // Effective power remaining in the path.
                float P_rr = maxThroughput * surfaceMax;

                if (P_rr < 0.1) {
                    float p = max(0.05, P_rr);
                    if (next_float(rng) > p) {
                        break;
                    }
                    throughput /= p;
                }
            }

            continue;
        }
    }

    // Final result with aggressive firefly suppression for Archviz
    if (any(isnan(accumulatedColor)) || any(isinf(accumulatedColor))) accumulatedColor = float3(0,0,0);

    // Radiance must be non-negative. Clamp numerical underflow/instability.
    float3 finalColor = clamp(accumulatedColor, 0.0, 1000.0);
    if (any(isnan(finalColor)) || any(isinf(finalColor))) finalColor = float3(0, 0, 0);
    finalColor *= primaryTonemapAoFactor;

    // Write DLSS inputs

    // Debug: Motion Vectors (debug mode index = 8)
    // Visualizes g_motionVectors in pixel units. Yellow-ish means near-zero MV.
    if (SHADER_DEBUG_MODE == 8.0) {
        float2 mv = g_motionVectors[launchIndex.xy];
        // Handle our invalid sentinel.
        if (abs(mv.x) > 1e5 || abs(mv.y) > 1e5) {
            mv = float2(0.0, 0.0);
        }
        // Visualize in a fixed pixel scale so typical camera motion is visible.
        // 32 pixels = full scale.
        const float kMvPixelsForFullScale = 32.0;
        float2 mvVis = mv / kMvPixelsForFullScale;
        mvVis = clamp(mvVis, float2(-1.0, -1.0), float2(1.0, 1.0));
        float mag = saturate(length(mv) / kMvPixelsForFullScale);
        float3 col = float3(0.5 + 0.5 * mvVis.x, 0.5 + 0.5 * mvVis.y, mag);
        g_output[launchIndex.xy] = float4(col, 1.0);
        return;
    }

    // Debug: Specular Hit Distance (debug mode index = 9)
    if (SHADER_DEBUG_MODE == 9.0) {
        float d = g_specHitDistance[launchIndex.xy];
        float v = saturate(d / max(farZ, 1e-3));
        g_output[launchIndex.xy] = float4(v, v, v, 1.0);
        return;
    }

    // Debug: Specular Motion Vectors (debug mode index = 10)
    if (SHADER_DEBUG_MODE == 10.0) {
        float2 mv = g_specularMotionVectors[launchIndex.xy];
        // Handle our invalid sentinel.
        if (abs(mv.x) > 1e5 || abs(mv.y) > 1e5) {
            mv = float2(0.0, 0.0);
        }
        float2 mvNorm = mv / float2(launchDim.xy);
        float3 col = float3(0.5 + 0.5 * mvNorm.x, 0.5 + 0.5 * mvNorm.y,
                            saturate(length(mvNorm) * 2.0));
        g_output[launchIndex.xy] = float4(col, 1.0);
        return;
    }

    // Final NaN/Inf check and firefly limiting before accumulation.
    // Use luminance-preserving scaling instead of per-channel clipping so
    // bright sky/sun values do not desaturate toward gray/white.
    if (!any(isfinite(finalColor))) finalColor = float3(0,0,0);
    finalColor = max(finalColor, 0.0);

    const float3 kLumaWeights = float3(0.2126, 0.7152, 0.0722);
    const float kMaxSampleLuminance = 10000.0; //enes  10 x boost for the better sun
    float sampleLum = dot(finalColor, kLumaWeights);
    if (sampleLum > kMaxSampleLuminance) {
        finalColor *= (kMaxSampleLuminance / sampleLum);
    }

    float lum = dot(finalColor, kLumaWeights);
    if (!isfinite(lum)) lum = 0.0;
    bool historyRepairedThisFrame = false;

    if (accumFrame == 0) {
        g_accumulation[launchIndex.xy] = float4(finalColor, 1.0);
        g_variance[launchIndex.xy] = 0.0;
        g_output[launchIndex.xy] = float4(finalColor, 1.0);
    } else {
        float4 prev_accum = g_accumulation[launchIndex.xy];
        float n = prev_accum.a;
        float3 oldSum = prev_accum.rgb;

        bool invalidHistory = !isfinite(n) || (n < 1.0) || any(!isfinite(oldSum));
        if (invalidHistory) {
            historyRepairedThisFrame = true;
            n = 1.0;
            oldSum = finalColor;
        }

        float safeN = max(n, 1.0);
        float oldMeanLum = dot(oldSum / safeN, float3(0.2126, 0.7152, 0.0722));
        
        float next_n = safeN + 1.0;
        float3 nextSum = oldSum + finalColor;
        float3 nextMean = nextSum / next_n;
        float nextMeanLum = dot(nextMean, float3(0.2126, 0.7152, 0.0722));
        
        // Welford's Online Variance: M2_n = M2_{n-1} + (x - mu_{n-1})(x - mu_n)
        float prev_M2 = invalidHistory ? 0.0 : g_variance[launchIndex.xy];
        float next_M2 = prev_M2 + (lum - oldMeanLum) * (lum - nextMeanLum);
        if (isnan(next_M2) || isinf(next_M2)) next_M2 = 0.0;
        next_M2 = max(0.0, next_M2);
        
        g_accumulation[launchIndex.xy] = float4(nextSum, next_n);
        g_variance[launchIndex.xy] = next_M2;
        
        if (SHADER_DEBUG_VIS_MODE == 1.0) {
             // Standard Error of Mean: SEM = sqrt(next_M2) / next_n;
             float sem = sqrt(next_M2) / next_n;
             // Coefficient of Variation
             float noise = sem / (max(0.01, nextMeanLum) + 0.001); 
             
             // Visualize: 0% = Black, 20% = White (Scaled 5x)
             float vis = saturate(noise * 5.0);
             // Active (Noisy) pixels in Red-ish/Gray to contrast with Green converged pixels
             g_output[launchIndex.xy] = float4(vis, vis * 0.5, vis * 0.5, 1.0);
        } else {
            // DLSS Ray Reconstruction is a temporal denoiser and expects raw, jittered, non-accumulated 
            // single-frame inputs to perform its own temporal reconstruction. 
            // However, we still maintain g_accumulation in the background for noise estimation 
            // and adaptive sampling logic.
            if (dlssRayReconstruction > 0.5) {
                g_output[launchIndex.xy] = float4(finalColor, 1.0);
            } else {
                g_output[launchIndex.xy] = float4(nextMean, 1.0);
            }
        }
    }

    // Debug: Accumulation sample count N (debug mode index = 17)
    if (SHADER_DEBUG_MODE == 17.0) {
        float n = g_accumulation[launchIndex.xy].a;
        float maxN = max(maxSPP, 1.0);
        float v = saturate(n / maxN);
        // Blue->Cyan->White ramp for quick low/high N detection.
        float3 col = lerp(float3(0.0, 0.0, 0.35), float3(0.2, 0.9, 1.0), sqrt(v));
        g_output[launchIndex.xy] = float4(col, 1.0);
        return;
    }

    // Debug: History validity / corruption mask (debug mode index = 18)
    if (SHADER_DEBUG_MODE == 18.0) {
        float4 accDbg = g_accumulation[launchIndex.xy];
        float varDbg = g_variance[launchIndex.xy];
        float n = accDbg.a;
        bool invalid = (n < 0.0) || isnan(n) || isinf(n) ||
                       any(isnan(accDbg.rgb)) || any(isinf(accDbg.rgb)) ||
                       isnan(varDbg) || isinf(varDbg) || (varDbg < 0.0);
        // Red = invalid, green = valid and initialized, blue = n==0 (not yet accumulated).
        float3 col = invalid ? float3(1.0, 0.0, 0.0) : ((n <= 0.5) ? float3(0.0, 0.0, 1.0) : float3(0.0, 1.0, 0.0));
        g_output[launchIndex.xy] = float4(col, 1.0);
        return;
    }

    // Debug: Per-pixel estimated noise from history (debug mode index = 19)
    if (SHADER_DEBUG_MODE == 19.0) {
        float4 accDbg = g_accumulation[launchIndex.xy];
        float n = max(accDbg.a, 1.0);
        float meanLum = dot(accDbg.rgb / n, float3(0.2126, 0.7152, 0.0722));
        float m2 = max(0.0, g_variance[launchIndex.xy]);
        float sem = sqrt(m2) / n;
        float noise = sem / (max(0.01, meanLum) + 0.001);
        float v = saturate(noise * 5.0);
        // Black->Orange->Red heatmap.
        float3 col = lerp(float3(0.0, 0.0, 0.0), float3(1.0, 0.35, 0.0), v);
        g_output[launchIndex.xy] = float4(col, 1.0);
        return;
    }

    // Debug: Sample deficit vs expected history count (debug mode index = 20)
    if (SHADER_DEBUG_MODE == 20.0) {
        float n = g_accumulation[launchIndex.xy].a;
        float expectedN = max(1.0, accumFrame + 1.0);
        float deficit = max(0.0, expectedN - n);
        float v = saturate(deficit / expectedN);
        // Black = on-track, Magenta/White = lagging/reset pixels.
        float3 col = lerp(float3(0.0, 0.0, 0.0), float3(1.0, 0.0, 1.0), sqrt(v));
        g_output[launchIndex.xy] = float4(col, 1.0);
        return;
    }

    // Debug: Recent reset mask (debug mode index = 21)
    if (SHADER_DEBUG_MODE == 21.0) {
        float n = g_accumulation[launchIndex.xy].a;
        bool recentReset = (accumFrame > 4.0) && (n <= 2.0);
        // Yellow = history repaired this frame, Red = very low sample count.
        float3 col = historyRepairedThisFrame ? float3(1.0, 1.0, 0.0) :
                     (recentReset ? float3(1.0, 0.0, 0.0) : float3(0.0, 0.0, 0.0));
        g_output[launchIndex.xy] = float4(col, 1.0);
        return;
    }
}


