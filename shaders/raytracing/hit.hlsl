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

float3 sRGBToLinear(float3 sRGB) {
    return pow(max(sRGB, 0.0), 2.2);
}

// Extract normal from normal map and transform to world space
float3 GetNormalFromMap(float2 uv, float3 worldNormal, float4 worldTangent, int normalTexIndex)
{
    if (normalTexIndex < 0 || length(worldTangent.xyz) < 0.001) return normalize(worldNormal);
    
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
    // Access mesh and material for this instance
    uint meshIdx = InstanceID();
    MeshData mesh = meshData[meshIdx];
    MaterialData mat = materials[mesh.materialIndex];

    // Get material properties
    float4 diffColor = mat.diffuseColor;
    float4 reflColor = mat.reflectionColor;
    float4 refrColor = mat.refractionColor;
    float4 emisColor = mat.emissiveColor; // w=ior
    
    int texDiff = mat.textureIndices.x;
    int texRefl = mat.textureIndices.y;
    int texNorm = mat.textureIndices.z;
    int texRefr = mat.textureIndices.w;
    int texEmis = mat.emissiveAndPad.x;
    int texOcc  = mat.emissiveAndPad.y;
    int texMR   = mat.emissiveAndPad.z;

#ifdef HIT_DEBUG
    // Encode primitive index into color for debugging
    uint primIndex = PrimitiveIndex();
    float r = (float)(primIndex & 0xFF) / 255.0f;
    float g = (float)((primIndex >> 8) & 0xFF) / 255.0f;
    payload.color = float4(r, g, 0.0, 1.0);
    return;
#endif

    uint3 launchIndex = DispatchRaysIndex();
    float2 bary2 = attr.barycentrics;
    float3 bary = float3(1.0 - bary2.x - bary2.y, bary2.x, bary2.y);
    uint primIndex = PrimitiveIndex();
    uint baseIndex = primIndex * 3;
    
    // Use .Load for indices to ensure compatibility with typed buffer arrays
    uint i0 = indices[mesh.ibIndex].Load(baseIndex);
    uint i1 = indices[mesh.ibIndex].Load(baseIndex + 1);
    uint i2 = indices[mesh.ibIndex].Load(baseIndex + 2);
    
    // Interpolate UV
    float2 uv0 = vertices[mesh.vbIndex][i0].uv;
    float2 uv1 = vertices[mesh.vbIndex][i1].uv;
    float2 uv2 = vertices[mesh.vbIndex][i2].uv;
    float2 uv = uv0 * bary.x + uv1 * bary.y + uv2 * bary.z;
    
    // Interpolate normal and tangent (local space)
    float3 n0 = vertices[mesh.vbIndex][i0].normal;
    float3 n1 = vertices[mesh.vbIndex][i1].normal;
    float3 n2 = vertices[mesh.vbIndex][i2].normal;
    float3 localNormal = normalize(n0 * bary.x + n1 * bary.y + n2 * bary.z);
    
    float4 t0 = vertices[mesh.vbIndex][i0].tangent;
    float4 t1 = vertices[mesh.vbIndex][i1].tangent;
    float4 t2 = vertices[mesh.vbIndex][i2].tangent;
    float4 localTangent = t0 * bary.x + t1 * bary.y + t2 * bary.z;

    // Transform to world space
    // ObjectToWorld3x4() is provided by DXR
    float3 worldNormal = normalize(mul((float3x3)ObjectToWorld3x4(), localNormal));
    float4 worldTangent;
    worldTangent.xyz = normalize(mul((float3x3)ObjectToWorld3x4(), localTangent.xyz));
    worldTangent.w = localTangent.w;
    
    // Sample textures
    float3 BaseColor = diffColor.rgb;
    if (texDiff >= 0) {
        BaseColor *= sRGBToLinear(textures[texDiff].SampleLevel(linearSampler, uv, 0).rgb);
    }
    
    float metalness = mat.extraParams.x;
    float roughnessFactor = saturate(1.0 - reflColor.w);
    float roughness = roughnessFactor;
    
    // Metal/Roughness Logic: factor * texture
    if (texMR >= 0) {
        float4 mrSample = textures[texMR].SampleLevel(linearSampler, uv, 0);
        roughness *= mrSample.g; 
        metalness *= mrSample.b;
    }

    roughness = max(roughness, 0.002);
    
    // Standard PBR Model
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), BaseColor, metalness);
    float3 DiffuseAlbedo = BaseColor * (1.0 - metalness);
    
    // Normal mapping
    float3 N = GetNormalFromMap(uv, worldNormal, worldTangent, texNorm);
    
    // Ambient occlusion
    float ao = 1.0;
    if (texOcc >= 0) {
        ao = textures[texOcc].SampleLevel(linearSampler, uv, 0).r;
    }
    
    // Emissive with boost factor and user intensity
    const float baseEmissiveBoost = 5.0f;
    float3 emissive = emisColor.rgb * baseEmissiveBoost * mat.extraParams.y;
    if (texEmis >= 0) {
        emissive *= sRGBToLinear(textures[texEmis].SampleLevel(linearSampler, uv, 0).rgb);
    }
    
    // Debug Pass
    int mode = (int)debugMode;
    if (mode == 1) { 
        payload.color = float4(BaseColor, 1.0);
        return;
    }
    if (mode == 2) { payload.color = float4(N * 0.5 + 0.5, 1.0); return; }
    if (mode == 3) { payload.color = float4(emissive, 1.0); return; }
    if (mode == 4) { payload.color = float4(1.0 - roughness, 1.0 - roughness, 1.0 - roughness, 1.0 - roughness); return; }
    if (mode == 5) { payload.color = float4(F0, 1.0); return; }
    if (mode == 6) { payload.color = float4(metalness, metalness, metalness, 1.0); return; }
    if (mode == 7) { payload.color = float4(ao, ao, ao, 1.0); return; }

    // Calculate view direction correctly in world space
    float3 P = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    float3 V = normalize(camPos - P);
    
    // Directional light
    float3 L = normalize(lightDir.xyz);
    float3 H = normalize(V + L);
    
    // Cook-Torrance BRDF
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
    
    float3 numerator = NDF * G * F;
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = saturate(dot(N, L));
    float denominator = 4.0 * NdotV * NdotL;
    float3 specular = numerator / max(denominator, 0.001);
    
    // Energy conservation
    float3 kS = F;
    float3 kD = 1.0 - kS;
    
    // Shadow Ray
    float shadowed = 1.0;
    RayDesc shadowRay;
    shadowRay.Origin = P + N * 0.001; // Offset to avoid self-intersection
    shadowRay.Direction = L;
    shadowRay.TMin = 0.001;
    shadowRay.TMax = 1000.0;
    
    RayPayload shadowPayload;
    shadowPayload.color = float4(0,0,0,0); // Use alpha=0 to indicate shadow
    
    // Trace shadow ray using flags to skip hits and stop at the first occlusion
    TraceRay(g_accel, RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xFF, 0, 0, 0, shadowRay, shadowPayload);
    
    // If the shadow ray reached something (didn't finish with a miss shader that sets alpha 1), it's shadowed
    if (shadowPayload.color.a < 0.5) shadowed = 0.0;

    // Outgoing radiance
    float3 radiance = lightColor.rgb * lightColor.w;
    float3 Lo = (kD * DiffuseAlbedo / PI + specular) * radiance * NdotL * shadowed;
    
    // IBL
    float3 R = reflect(-V, N);
    float3 F_ibl = FresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    float3 kS_ibl = F_ibl;
    float3 kD_ibl = 1.0 - kS_ibl;

    float2 envUV_diff = DirectionToUV(N);
    // Use higher mips for diffuse irradiance
    float3 irradiance = envMap.SampleLevel(linearSampler, envUV_diff, 7.0).rgb; 
    float3 diffuse_ibl = kD_ibl * irradiance * DiffuseAlbedo;

    float2 envUV_spec = DirectionToUV(R);
    // Use mips for glossy reflections
    float3 prefilteredColor = envMap.SampleLevel(linearSampler, envUV_spec, roughness * 7.0).rgb;
    float3 specular_ibl = kS_ibl * prefilteredColor;

    // Use a consistent 5.0x boost for IBL to match raster and provide good environment lighting
    float3 ambient = (diffuse_ibl + specular_ibl) * ao * ambientColor.rgb * 5.0;
    
    // Final color calculation with camera intensity. 1:1 match with raster.
    float3 color = (ambient + Lo + emissive) * intensity;
    
    // Final tone mapping and gamma
    color = ToneMap(color);
    color = pow(color, float3(1.0/2.2, 1.0/2.2, 1.0/2.2));
    
    payload.color = float4(color, diffColor.a);
}