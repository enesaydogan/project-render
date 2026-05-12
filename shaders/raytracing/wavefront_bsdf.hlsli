#ifndef WAVEFRONT_BSDF_HLSLI
#define WAVEFRONT_BSDF_HLSLI

struct WavefrontBsdfLobeProbabilities
{
    float reflection;
    float diffuse;
    float transmission;
};

inline WavefrontBsdfLobeProbabilities WavefrontBsdfComputeLobeProbabilities(
    float3 normal,
    float3 viewDir,
    float3 albedo,
    float metallic,
    float transmission,
    float translucency,
    float ior,
    float specularWeight,
    float3 specularColor)
{
    float3 N = normalize(normal);
    float3 V = normalize(viewDir);
    float metal = saturate(metallic);
    float transmit = saturate(transmission) * (1.0 - metal);
    float dielectricF =
        saturate(FresnelDielectric(abs(dot(N, V)), ior) *
                 saturate(specularWeight));
    float3 metalF0 = saturate(albedo) * saturate(specularColor);
    float3 metalF = F_Schlick(abs(dot(N, V)), metalF0);
    float conductorF = max(metalF.r, max(metalF.g, metalF.b));

    WavefrontBsdfLobeProbabilities p;
    p.reflection = lerp(dielectricF, conductorF, metal);
    p.transmission = transmit * (1.0 - dielectricF);
    p.diffuse = (1.0 - metal) * (1.0 - transmit) * (1.0 - dielectricF);
    p.diffuse = max(p.diffuse, p.diffuse * saturate(translucency));

    float total = max(p.reflection + p.diffuse + p.transmission, 1.0e-6);
    p.reflection /= total;
    p.diffuse /= total;
    p.transmission /= total;
    return p;
}

inline float3 WavefrontBsdfSafeNormalize(float3 value, float3 fallback)
{
    float lenSq = dot(value, value);
    return (lenSq > 1.0e-8) ? value * rsqrt(lenSq) : fallback;
}

inline float3 WavefrontBsdfSampleCone(float3 direction, float cosThetaMax,
                                      float2 u)
{
    float3 D = normalize(direction);
    float z = lerp(saturate(cosThetaMax), 1.0, u.x);
    float r = sqrt(max(0.0, 1.0 - z * z));
    float phi = 2.0 * PI * u.y;
    float x = r * cos(phi);
    float y = r * sin(phi);
    float3 up = abs(D.z) < 0.999 ? float3(0.0, 0.0, 1.0)
                                 : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(up, D));
    float3 bitangent = cross(D, tangent);
    return normalize(tangent * x + bitangent * y + D * z);
}

inline bool WavefrontBsdfSampleRoughDielectric(
    float3 rayDir,
    float3 normal,
    float roughness,
    float ior,
    bool thinWalled,
    inout RNG rng,
    out float3 continuationDir,
    out bool reflected,
    out float branchProbability)
{
    float3 I = normalize(rayDir);
    float r = saturate(roughness);
    branchProbability = 1.0;
    reflected = false;

    if (thinWalled) {
        if (r <= 1.0e-4) {
            continuationDir = I;
        } else {
            float spread = min(0.85, r * r * 2.0 + r * 0.12);
            continuationDir =
                WavefrontBsdfSampleCone(I, cos(spread), next_float2(rng));
        }
        return true;
    }

    float3 orientedNormal = (dot(I, normal) < 0.0) ? normal : -normal;
    float alpha = max(r, 0.001);
    float3 microNormal = SampleGGX(next_float2(rng), orientedNormal, alpha);
    if (dot(microNormal, orientedNormal) < 0.0) {
        microNormal = -microNormal;
    }

    float cosVm = saturate(dot(-I, microNormal));
    float fresnel = saturate(FresnelDielectric(cosVm, ior));
    if (next_float(rng) < fresnel) {
        reflected = true;
        branchProbability = max(fresnel, 1.0e-4);
        continuationDir =
            WavefrontBsdfSafeNormalize(reflect(I, microNormal),
                                       reflect(I, orientedNormal));
        return dot(continuationDir, orientedNormal) > 1.0e-5;
    }

    float entering = dot(I, microNormal) < 0.0 ? 1.0 : 0.0;
    float3 faceNormal = (entering > 0.5) ? microNormal : -microNormal;
    float eta = (entering > 0.5) ? rcp(max(ior, 1.0)) : max(ior, 1.0);
    float3 refracted = refract(I, faceNormal, eta);
    if (dot(refracted, refracted) < 1.0e-8) {
        reflected = true;
        branchProbability = 1.0;
        continuationDir =
            WavefrontBsdfSafeNormalize(reflect(I, faceNormal),
                                       reflect(I, orientedNormal));
        return true;
    }

    branchProbability = max(1.0 - fresnel, 1.0e-4);
    continuationDir = WavefrontBsdfSafeNormalize(refracted, -faceNormal);
    return true;
}

#endif // WAVEFRONT_BSDF_HLSLI
