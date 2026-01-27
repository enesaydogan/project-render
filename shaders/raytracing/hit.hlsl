// shaders/raytracing/hit.hlsl
// Closest hit shader with full PBR lighting

#include "common.hlsli"

// Improved microfacet BRDF helpers (matching raster renderer)
static const float PI = 3.14159265359;

float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    
    return a2 / max(denom, 0.0001);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    
    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    
    return nom / max(denom, 0.0001);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    
    return ggx1 * ggx2;
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// Extract normal from normal map and transform to world space
float3 GetNormalFromMap(float2 uv, float3 worldNormal, float4 worldTangent, int normalTexIndex)
{
    if (normalTexIndex < 0) return normalize(worldNormal);
    
    float3 tangentNormal = textures[normalTexIndex].SampleLevel(linearSampler, uv, 0).xyz * 2.0 - 1.0;
    
    float3 N = normalize(worldNormal);
    float3 T = normalize(worldTangent.xyz);
    float3 B = cross(N, T) * worldTangent.w;
    float3x3 TBN = float3x3(T, B, N);
    
    return normalize(mul(tangentNormal, TBN));
}

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    // Get texture indices
    int baseColorIdx = textureIndices.x;
    int metallicRoughnessIdx = textureIndices.y;
    int normalIdx = textureIndices.z;
    int occlusionIdx = textureIndices.w;
    int emissiveIdx = emissiveAndPad.x;
    
#ifdef HIT_DEBUG
    // Encode primitive index into color for debugging
    uint primIndex = PrimitiveIndex();
    float r = (float)(primIndex & 0xFF) / 255.0f;
    float g = (float)((primIndex >> 8) & 0xFF) / 255.0f;
    payload.color = float4(r, g, 0.0, 1.0);
    return;
#endif

    // Interpolate vertex attributes
    float2 bary2 = attr.barycentrics;
    float3 bary = float3(bary2.x, bary2.y, 1.0 - bary2.x - bary2.y);
    uint primIndex = PrimitiveIndex();
    uint baseIndex = primIndex * 3;
    uint i0 = indices[baseIndex];
    uint i1 = indices[baseIndex + 1];
    uint i2 = indices[baseIndex + 2];
    
    // Interpolate UV
    float2 uv0 = vertices[i0].uv;
    float2 uv1 = vertices[i1].uv;
    float2 uv2 = vertices[i2].uv;
    float2 uv = uv0 * bary.x + uv1 * bary.y + uv2 * bary.z;
    
    // Interpolate normal and tangent
    float3 n0 = vertices[i0].normal;
    float3 n1 = vertices[i1].normal;
    float3 n2 = vertices[i2].normal;
    float3 worldNormal = normalize(n0 * bary.x + n1 * bary.y + n2 * bary.z);
    
    float4 t0 = vertices[i0].tangent;
    float4 t1 = vertices[i1].tangent;
    float4 t2 = vertices[i2].tangent;
    float4 worldTangent = t0 * bary.x + t1 * bary.y + t2 * bary.z;
    
    // Sample textures
    float3 baseColor = baseColorFactor.rgb;
    if (baseColorIdx >= 0) {
        float4 texColor = textures[baseColorIdx].SampleLevel(linearSampler, uv, 0);
        baseColor *= texColor.rgb;
    }
    
    // Metallic-roughness
    float metallic = params1.x;
    float roughness = params1.y;
    if (metallicRoughnessIdx >= 0) {
        float4 mr = textures[metallicRoughnessIdx].SampleLevel(linearSampler, uv, 0);
        metallic *= mr.b; // Blue channel = metallic
        roughness *= mr.g; // Green channel = roughness
    }
    
    // Clamp roughness to avoid division by zero
    roughness = max(roughness, 0.04);
    
    // Normal mapping
    float3 N = GetNormalFromMap(uv, worldNormal, worldTangent, normalIdx);
    
    // Ambient occlusion
    float ao = 1.0;
    if (occlusionIdx >= 0) {
        ao = textures[occlusionIdx].SampleLevel(linearSampler, uv, 0).r;
    }
    
    // Emissive
    float3 emissive = emissiveFactor.rgb;
    if (emissiveIdx >= 0) {
        emissive *= textures[emissiveIdx].SampleLevel(linearSampler, uv, 0).rgb;
    }
    
    // Calculate view direction (from surface point to camera)
    float3 worldPos = vertices[i0].position * bary.x + vertices[i1].position * bary.y + vertices[i2].position * bary.z;
    float3 V = normalize(camPos - worldPos);
    
    // Simple directional light
    float3 L = normalize(-lightDir.xyz); // Light direction (negate because it points towards light)
    float3 H = normalize(V + L);
    
    // Calculate reflectance at normal incidence
    float3 F0 = float3(0.04, 0.04, 0.04); // Dielectric base reflectivity
    F0 = lerp(F0, baseColor, metallic);
    
    // Cook-Torrance BRDF
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
    
    float3 numerator = NDF * G * F;
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float denominator = 4.0 * NdotV * NdotL;
    float3 specular = numerator / max(denominator, 0.001);
    
    // Energy conservation
    float3 kS = F;
    float3 kD = float3(1.0, 1.0, 1.0) - kS;
    kD *= 1.0 - metallic;
    
    // Outgoing radiance
    float3 Lo = (kD * baseColor / PI + specular) * NdotL;
    
    // Ambient lighting with AO
    float3 ambient = float3(0.03, 0.03, 0.03) * baseColor * ao;
    
    // Add emissive and light color scaling
    float3 color = ambient + Lo * lightColor.rgb * lightColor.w + emissive;
    
    // Simple tone mapping
    color = color / (color + float3(1.0, 1.0, 1.0));
    
    // Gamma correction
    color = pow(color, float3(1.0/2.2, 1.0/2.2, 1.0/2.2));
    
    payload.color = float4(color, baseColorFactor.a);
}