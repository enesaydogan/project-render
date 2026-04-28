#ifndef WAVEFRONT_TRANSPORT_HLSLI
#define WAVEFRONT_TRANSPORT_HLSLI

inline float3 EvaluateWavefrontPrimaryPreview(
    WavefrontHitRecord record,
    float3 worldNormal,
    float3 hitPos)
{
    float3 baseColor = UnpackPayloadAlbedo(record.packedAlbedo);
    float3 specularColor = UnpackPayloadSpecularColor(record.packedSpecular);
    float4 surface = UnpackPayloadSurface(record.packedSurface);
    float roughness = saturate(surface.x);
    float metallic = saturate(surface.y);
    float transmission = saturate(surface.z);
    float translucency = saturate(surface.w);
    float specularWeight = saturate(UnpackPayloadSpecularWeight(record.packedIorType));

    float3 V = normalize(camPos - hitPos);
    float3 L = normalize(lightDir.xyz);
    float3 H = normalize(V + L);
    float NdotL = saturate(dot(worldNormal, L));
    float NdotV = saturate(dot(worldNormal, V));
    float NdotH = saturate(dot(worldNormal, H));
    float VdotH = saturate(dot(V, H));

    float3 diffuseColor = baseColor * (1.0 - metallic) * (1.0 - transmission);
    float3 dielectricF0 = 0.04.xxx * specularWeight;
    float3 F0 = lerp(dielectricF0 * max(specularColor, 0.0), baseColor, metallic);
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
    float3 ambient = diffuseColor * lerp(0.08, 0.24, horizon) * intensity;

    float3 transmissionTint =
        UnpackPayloadTransmissionColor(record.packedTransmission) * transmission;
    float edgeLight = pow(max(1.0 - NdotV, 0.0), 3.0);
    float3 translucentWrap = transmissionTint * (0.12 + 0.38 * edgeLight) *
                             max(translucency, transmission) * intensity;

    return WavefrontHitRecordGetColor(record) + ambient + translucentWrap;
}

inline float3 ComputeWavefrontDirectLightingWeight(
    WavefrontHitRecord record,
    float3 worldNormal,
    float3 hitPos,
    float3 lightDirection)
{
    float3 baseColor = UnpackPayloadAlbedo(record.packedAlbedo);
    float3 specularColor = UnpackPayloadSpecularColor(record.packedSpecular);
    float4 surface = UnpackPayloadSurface(record.packedSurface);
    float roughness = saturate(surface.x);
    float metallic = saturate(surface.y);
    float transmission = saturate(surface.z);
    float specularWeight = saturate(UnpackPayloadSpecularWeight(record.packedIorType));

    float3 V = normalize(camPos - hitPos);
    float3 L = normalize(lightDirection);
    float3 H = normalize(V + L);
    float NdotL = saturate(dot(worldNormal, L));
    float NdotV = saturate(dot(worldNormal, V));
    float NdotH = saturate(dot(worldNormal, H));
    float VdotH = saturate(dot(V, H));

    float3 diffuseColor = baseColor * (1.0 - metallic) * (1.0 - transmission);
    float3 dielectricF0 = 0.04.xxx * specularWeight;
    float3 F0 = lerp(dielectricF0 * max(specularColor, 0.0), baseColor, metallic);
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

#endif // WAVEFRONT_TRANSPORT_HLSLI