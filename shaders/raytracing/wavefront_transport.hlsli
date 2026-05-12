#ifndef WAVEFRONT_TRANSPORT_HLSLI
#define WAVEFRONT_TRANSPORT_HLSLI

#include "../brdf_lib.hlsl"
#include "wavefront_bsdf.hlsli"

static const float kWavefrontShadowContributionScale = 4096.0;
static const float kWavefrontShadowContributionInvScale =
    1.0 / kWavefrontShadowContributionScale;
static const float kWavefrontShadowContributionMax = 65504.0;

inline uint WavefrontEncodeShadowContribution(float value)
{
    float finiteValue = isfinite(value) ? value : 0.0;
    return (uint)min(max(finiteValue, 0.0) *
                         kWavefrontShadowContributionScale + 0.5,
                     kWavefrontShadowContributionMax *
                         kWavefrontShadowContributionScale);
}

inline void WavefrontAtomicAddShadowContribution(uint pixelIndex,
                                                 float3 contribution)
{
    uint baseIndex = pixelIndex * 4u;
    uint r = WavefrontEncodeShadowContribution(contribution.r);
    uint g = WavefrontEncodeShadowContribution(contribution.g);
    uint b = WavefrontEncodeShadowContribution(contribution.b);
    uint previousValue = 0u;
    if (r != 0u) {
        InterlockedAdd(g_wavefrontShadowContribution[baseIndex + 0u], r,
                       previousValue);
    }
    if (g != 0u) {
        InterlockedAdd(g_wavefrontShadowContribution[baseIndex + 1u], g,
                       previousValue);
    }
    if (b != 0u) {
        InterlockedAdd(g_wavefrontShadowContribution[baseIndex + 2u], b,
                       previousValue);
    }
}

inline float3 WavefrontSampleCone(float3 dir, float cosThetaMax, float2 u)
{
    float z = lerp(cosThetaMax, 1.0, u.x);
    float r = sqrt(max(0.0, 1.0 - z * z));
    float phi = 2.0 * PI * u.y;
    float x = r * cos(phi);
    float y = r * sin(phi);

    float3 up = abs(dir.z) < 0.999 ? float3(0.0, 0.0, 1.0)
                                   : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(up, dir));
    float3 bitangent = cross(dir, tangent);
    return normalize(tangent * x + bitangent * y + dir * z);
}

inline float3 WavefrontBuildSunVisibilityDirection(float3 direction,
                                                   inout RNG rng)
{
    float3 sunDirection = normalize(direction);
    if (lightDir.w > 0.0) {
        sunDirection = WavefrontSampleCone(sunDirection,
                                           cos(lightDir.w),
                                           next_float2(rng));
    }
    return sunDirection;
}

inline bool WavefrontTraceVisibility(float3 origin,
                                     float3 direction,
                                     float maxDistance)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = normalize(direction);
    ray.TMin = 0.001;
    ray.TMax = max(0.001, maxDistance - 0.002);

    RayQuery<RAY_FLAG_SKIP_CLOSEST_HIT_SHADER |
             RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
    query.TraceRayInline(g_accel, RAY_FLAG_NONE, 0xFF, ray);
    while (query.Proceed()) {}
    return query.CommittedStatus() == COMMITTED_NOTHING;
}

inline float3 WavefrontBuildShadowOrigin(float3 hitPos,
                                         float3 worldNormal,
                                         float3 lightDirection,
                                         float bias)
{
    float3 N = normalize(worldNormal);
    float3 L = normalize(lightDirection);
    if (dot(N, L) < 0.0) {
        N = -N;
    }
    return hitPos + N * bias;
}

inline float WavefrontDielectricF0FromIor(float ior)
{
    float safeIor = max(ior, 1.0 + 1e-4);
    float f0 = (safeIor - 1.0) / (safeIor + 1.0);
    return f0 * f0;
}

inline float3 ComputeWavefrontSurfaceF0(float3 albedo,
                                        float metallic,
                                        float ior,
                                        float specularWeight,
                                        float3 specularColor)
{
    float dielectricF0 = WavefrontDielectricF0FromIor(ior) *
                         saturate(specularWeight);
    float3 dielectric = dielectricF0.xxx * saturate(specularColor);
    return lerp(dielectric, saturate(albedo), saturate(metallic));
}

inline void ComputeWavefrontLobeProbabilities(float3 normal,
                                              float3 viewDir,
                                              float3 albedo,
                                              float metallic,
                                              float transmission,
                                              float translucency,
                                              float ior,
                                              float specularWeight,
                                              float3 specularColor,
                                              out float reflectionProb,
                                              out float diffuseProb,
                                              out float transmissionProb)
{
    WavefrontBsdfLobeProbabilities p =
        WavefrontBsdfComputeLobeProbabilities(
            normal, viewDir, albedo, metallic, transmission, translucency, ior,
            specularWeight, specularColor);
    reflectionProb = p.reflection;
    diffuseProb = p.diffuse;
    transmissionProb = p.transmission;
}

inline float3 ComputeWavefrontSpecularThroughput(float3 albedo,
                                                 float metallic,
                                                 float ior,
                                                 float specularWeight,
                                                 float3 specularColor,
                                                 float transmission)
{
    float3 F0 = ComputeWavefrontSurfaceF0(albedo, metallic, ior,
                                          specularWeight, specularColor);
    return saturate(F0) * (1.0 - 0.5 * saturate(transmission));
}

inline float ComputeWavefrontBrdfPdfForDirection(WavefrontHitRecord record,
                                                 float3 worldNormal,
                                                 float3 hitPos,
                                                 float3 lightDirection)
{
    float3 baseColor = UnpackPayloadAlbedo(record.packedAlbedo);
    float3 specularColor = UnpackPayloadSpecularColor(record.packedSpecular);
    float4 surface = WavefrontHitRecordSurface(record);
    float roughness = saturate(surface.x);
    float metallic = saturate(surface.y);
    float transmission = saturate(surface.z);
    float translucency = saturate(surface.w);
    float specularWeight = saturate(UnpackPayloadSpecularWeight(record.packedIorType));
    float ior = UnpackPayloadIor(record.packedIorType);

    float3 V = normalize(camPos - hitPos);
    float reflectionProb = 0.0;
    float diffuseProb = 0.0;
    float transmissionProb = 0.0;
    ComputeWavefrontLobeProbabilities(worldNormal, V,
                                      baseColor, metallic, transmission,
                                      translucency, ior, specularWeight,
                                      specularColor,
                                      reflectionProb, diffuseProb,
                                      transmissionProb);

    float3 L = normalize(lightDirection);
    float NdotL = saturate(dot(worldNormal, L));
    if (NdotL <= 0.0) {
        return 0.0;
    }

    float3 H = normalize(V + L);
    float NdotH = saturate(dot(worldNormal, H));
    float VdotH = saturate(dot(V, H));
    float pdfSpec = 0.0;
    if (reflectionProb > 0.0 && NdotH > 0.0 && VdotH > 0.0) {
        pdfSpec = PDF_GGX(NdotH, VdotH, max(roughness, 0.03)) * reflectionProb;
    }
    float pdfDiff = 0.0;
    if (diffuseProb > 0.0) {
        pdfDiff = PDF_Lambert(NdotL) * diffuseProb;
    }
    return max(0.0, pdfSpec + pdfDiff);
}

inline float ComputeWavefrontEnvironmentMisWeight(WavefrontHitRecord record,
                                                  float3 worldNormal,
                                                  float3 hitPos,
                                                  float3 lightDirection,
                                                  float lightPdf)
{
    float pdfLight = max(lightPdf, 1.0e-8);
    float pdfBrdf = ComputeWavefrontBrdfPdfForDirection(record,
                                                        worldNormal,
                                                        hitPos,
                                                        lightDirection);
    return (pdfLight * pdfLight) /
           (pdfLight * pdfLight + pdfBrdf * pdfBrdf + 1.0e-12);
}

inline float3 EvaluateWavefrontPrimaryPreview(
    WavefrontHitRecord record,
    float3 worldNormal,
    float3 hitPos)
{
    float3 baseColor = UnpackPayloadAlbedo(record.packedAlbedo);
    float3 specularColor = UnpackPayloadSpecularColor(record.packedSpecular);
    float4 surface = WavefrontHitRecordSurface(record);
    float roughness = saturate(surface.x);
    float metallic = saturate(surface.y);
    float transmission = saturate(surface.z);
    float translucency = saturate(surface.w);
    float specularWeight = saturate(UnpackPayloadSpecularWeight(record.packedIorType));
    float ior = UnpackPayloadIor(record.packedIorType);

    float3 V = normalize(camPos - hitPos);
    float3 L = normalize(lightDir.xyz);
    float3 H = normalize(V + L);
    float NdotL = saturate(dot(worldNormal, L));
    float NdotV = saturate(dot(worldNormal, V));
    float NdotH = saturate(dot(worldNormal, H));
    float VdotH = saturate(dot(V, H));

    float3 diffuseColor = baseColor * (1.0 - metallic) * (1.0 - transmission);
    float3 F0 = ComputeWavefrontSurfaceF0(baseColor, metallic, ior,
                                          specularWeight, specularColor);
    float alpha = max(roughness * roughness, 0.03);
    float alpha2 = alpha * alpha;
    float denom = max(NdotH * NdotH * (alpha2 - 1.0) + 1.0, 1.0e-4);
    float D = alpha2 / max(PI * denom * denom, 1.0e-4);
    float k = (alpha + 1.0) * (alpha + 1.0) * 0.125;
    float Gv = NdotV / max(lerp(NdotV, 1.0, k), 1.0e-4);
    float Gl = NdotL / max(lerp(NdotL, 1.0, k), 1.0e-4);
    float3 F = F0 + (1.0 - F0) * pow(max(1.0 - VdotH, 0.0), 5.0);
    float3 specular = (D * Gv * Gl) * F;

    float horizon = saturate(worldNormal.y * 0.5 + 0.5);
    float3 ambient = diffuseColor * lerp(0.08, 0.24, horizon);

    float3 transmissionTint =
        UnpackPayloadTransmissionColor(record.packedTransmission) * transmission;
    float edgeLight = pow(max(1.0 - NdotV, 0.0), 3.0);
    float3 translucentWrap = transmissionTint * (0.12 + 0.38 * edgeLight) *
                             max(translucency, transmission);

    return WavefrontHitRecordGetColor(record) + ambient + translucentWrap;
}

inline float3 ComputeWavefrontDirectLightingWeightForView(
    WavefrontHitRecord record,
    float3 worldNormal,
    float3 viewDirection,
    float3 lightDirection)
{
    float3 baseColor = UnpackPayloadAlbedo(record.packedAlbedo);
    float3 specularColor = UnpackPayloadSpecularColor(record.packedSpecular);
    float4 surface = WavefrontHitRecordSurface(record);
    float roughness = saturate(surface.x);
    float metallic = saturate(surface.y);
    float transmission = saturate(surface.z);
    float specularWeight = saturate(UnpackPayloadSpecularWeight(record.packedIorType));
    float ior = UnpackPayloadIor(record.packedIorType);

    float3 V = normalize(viewDirection);
    float3 L = normalize(lightDirection);
    float3 H = normalize(V + L);
    float NdotL = saturate(dot(worldNormal, L));
    float NdotV = saturate(dot(worldNormal, V));
    float NdotH = saturate(dot(worldNormal, H));
    float VdotH = saturate(dot(V, H));

    float3 diffuseColor = baseColor * (1.0 - metallic) * (1.0 - transmission);
    float3 F0 = ComputeWavefrontSurfaceF0(baseColor, metallic, ior,
                                          specularWeight, specularColor);
    float alpha = max(roughness * roughness, 0.03);
    float alpha2 = alpha * alpha;
    float denom = max(NdotH * NdotH * (alpha2 - 1.0) + 1.0, 1.0e-4);
    float D = alpha2 / max(PI * denom * denom, 1.0e-4);
    float k = (alpha + 1.0) * (alpha + 1.0) * 0.125;
    float Gv = NdotV / max(lerp(NdotV, 1.0, k), 1.0e-4);
    float Gl = NdotL / max(lerp(NdotL, 1.0, k), 1.0e-4);
    float3 F = F0 + (1.0 - F0) * pow(max(1.0 - VdotH, 0.0), 5.0);
    float3 specular = (D * Gv * Gl) * F;

    return ((diffuseColor / PI) + specular) * NdotL;
}

inline float3 ComputeWavefrontDirectLightingWeight(
    WavefrontHitRecord record,
    float3 worldNormal,
    float3 hitPos,
    float3 lightDirection)
{
    float3 viewDir = normalize(camPos - hitPos);
    return ComputeWavefrontDirectLightingWeightForView(
        record, worldNormal, viewDir, lightDirection);
}

#endif // WAVEFRONT_TRANSPORT_HLSLI
