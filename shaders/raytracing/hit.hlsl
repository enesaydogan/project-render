// shaders/raytracing/hit.hlsl
// Closest hit shader with full PBR lighting

#include "common.hlsli"

// Improved microfacet BRDF helpers (matching raster renderer)

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

float3 TriPlanarWeights(float3 n, float sharpness)
{
    float3 an = abs(n);
    an = pow(max(an, 0.0), max(sharpness, 0.01));
    float s = an.x + an.y + an.z;
    return (s > 1e-5) ? (an / s) : float3(0.3333, 0.3333, 0.3333);
}

float2 TriPlanarUV_X(float3 p, float scale) { return float2(p.y, p.z) * scale; }
float2 TriPlanarUV_Y(float3 p, float scale) { return float2(p.z, p.x) * scale; }
float2 TriPlanarUV_Z(float3 p, float scale) { return float2(p.x, p.y) * scale; }

float4 SampleTriPlanarLevel0(int texIndex, float3 worldPos, float3 worldNormal, float scale, float sharpness)
{
    if (texIndex < 0) return float4(1,1,1,1);
    float3 w = TriPlanarWeights(worldNormal, sharpness);
    float4 sx = textures[texIndex].SampleLevel(linearSampler, TriPlanarUV_X(worldPos, scale), 0);
    float4 sy = textures[texIndex].SampleLevel(linearSampler, TriPlanarUV_Y(worldPos, scale), 0);
    float4 sz = textures[texIndex].SampleLevel(linearSampler, TriPlanarUV_Z(worldPos, scale), 0);
    return sx * w.x + sy * w.y + sz * w.z;
}

float3 UnpackNormal(float4 n)
{
    return n.xyz * 2.0 - 1.0;
}

float3 SampleTriPlanarNormalLevel0(int texIndex, float3 worldPos, float3 worldNormal, float scale, float sharpness, float strength)
{
    if (texIndex < 0) return normalize(worldNormal);
    float3 Nw = normalize(worldNormal);
    float3 w = TriPlanarWeights(Nw, sharpness);

    float3 nx = UnpackNormal(textures[texIndex].SampleLevel(linearSampler, TriPlanarUV_X(worldPos, scale), 0));
    float3 ny = UnpackNormal(textures[texIndex].SampleLevel(linearSampler, TriPlanarUV_Y(worldPos, scale), 0));
    float3 nz = UnpackNormal(textures[texIndex].SampleLevel(linearSampler, TriPlanarUV_Z(worldPos, scale), 0));
    nx.xy *= strength; ny.xy *= strength; nz.xy *= strength;
    nx = normalize(nx); ny = normalize(ny); nz = normalize(nz);

    float sx = (Nw.x >= 0.0) ? 1.0 : -1.0;
    float sy = (Nw.y >= 0.0) ? 1.0 : -1.0;
    float sz = (Nw.z >= 0.0) ? 1.0 : -1.0;

    float3 Tx = float3(0,1,0);
    float3 Bx = float3(0,0,sx);
    float3 Nx = float3(sx,0,0);
    float3x3 TBNx = float3x3(Tx, Bx, Nx);

    float3 Ty = float3(0,0,1);
    float3 By = float3(sy,0,0);
    float3 Ny = float3(0,sy,0);
    float3x3 TBNy = float3x3(Ty, By, Ny);

    float3 Tz = float3(1,0,0);
    float3 Bz = float3(0,sz,0);
    float3 Nz = float3(0,0,sz);
    float3x3 TBNz = float3x3(Tz, Bz, Nz);

    float3 wx = normalize(mul(nx, TBNx));
    float3 wy = normalize(mul(ny, TBNy));
    float3 wz = normalize(mul(nz, TBNz));

    return normalize(wx * w.x + wy * w.y + wz * w.z);
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
    uint matIdx = (uint)max(0, mesh.materialIndex);
    MaterialData mat = materials[matIdx];

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

    float4 arch0 = mat.archvizParams0;
    float4 uvXf = mat.uvTransform;
    float4 triP = mat.triPlanarParams;

#ifdef HIT_DEBUG
    // Encode primitive index into color for debugging
    uint primIndex = PrimitiveIndex();
    float r = (float)(primIndex & 0xFF) / 255.0f;
    float g = (float)((primIndex >> 8) & 0xFF) / 255.0f;
    payload.color = float3(r, g, 0.0);
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

    // Instrumentation: count index + vertex fetches
    // InterlockedAdd(g_shaderCounters[SHADER_COUNTER_INDEX_LOADS], 3);
    // InterlockedAdd(g_shaderCounters[SHADER_COUNTER_VERTEX_FETCHES], 3);
    
    // Interpolate UV
    float2 uv0 = vertices[mesh.vbIndex][i0].uv;
    float2 uv1 = vertices[mesh.vbIndex][i1].uv;
    float2 uv2 = vertices[mesh.vbIndex][i2].uv;
    float2 uv = uv0 * bary.x + uv1 * bary.y + uv2 * bary.z;

    // Material UV transform (real-world scaling control)
    uv = uv * uvXf.xy + uvXf.zw;

    // World position (used by tri-planar)
    float3 P = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    
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
    
    bool triPlanar = (triP.x > 0.5);
    float triScale = max(triP.y, 1e-6);
    float triSharp = max(triP.z, 0.01);
    float triNormStrength = max(triP.w, 0.0);

    // Sample textures
    float3 BaseColor = diffColor.rgb;
    if (texDiff >= 0) {
        float3 bc = triPlanar ? SampleTriPlanarLevel0(texDiff, P, worldNormal, triScale, triSharp).rgb
                              : textures[texDiff].SampleLevel(linearSampler, uv, 0).rgb;
        BaseColor *= sRGBToLinear(bc);
        // InterlockedAdd(g_shaderCounters[SHADER_COUNTER_TEXTURE_SAMPLES], 1);
    }
    
    float metalness = mat.extraParams.x;
    float roughnessFactor = saturate(1.0 - reflColor.w);
    float roughness = roughnessFactor;
    
    // Metal/Roughness Logic: factor * texture
    if (texMR >= 0) {
        float4 mrSample = triPlanar ? SampleTriPlanarLevel0(texMR, P, worldNormal, triScale, triSharp)
                                    : textures[texMR].SampleLevel(linearSampler, uv, 0);
        roughness *= mrSample.g; 
        metalness *= mrSample.b;
        // InterlockedAdd(g_shaderCounters[SHADER_COUNTER_TEXTURE_SAMPLES], 1);
    }

    // Clamp to reduce fireflies / unstable highlights in archviz scenes
    roughness = max(roughness, 0.02);
    
    // Attenuate diffuse by transmission (refraction) for dielectrics.
    // This removes the "tint" or "solid" look from glass.
    float transmission = saturate(max(refrColor.r, max(refrColor.g, refrColor.b))) * (1.0 - metalness);
    float3 DiffuseAlbedo = BaseColor * (1.0 - metalness) * (1.0 - transmission);

    // Standard PBR Model (dielectric F0 from IOR)
    float ior = max(emisColor.w, 1.0);
    float f0s = (ior - 1.0) / (ior + 1.0);
    f0s = f0s * f0s;
    float3 F0 = lerp(float3(f0s, f0s, f0s), BaseColor, metalness);
    
    // Normal mapping
    float3 N = triPlanar ? SampleTriPlanarNormalLevel0(texNorm, P, worldNormal, triScale, triSharp, triNormStrength)
                         : GetNormalFromMap(uv, worldNormal, worldTangent, texNorm);
    
    // Ambient occlusion
    float ao = 1.0;
    if (texOcc >= 0) {
        ao = triPlanar ? SampleTriPlanarLevel0(texOcc, P, worldNormal, triScale, triSharp).r
                       : textures[texOcc].SampleLevel(linearSampler, uv, 0).r;
        // InterlockedAdd(g_shaderCounters[SHADER_COUNTER_TEXTURE_SAMPLES], 1);
    }
    
    // Emissive with boost factor and user intensity
    const float baseEmissiveBoost = 5.0f;
    float3 emissive = emisColor.rgb * baseEmissiveBoost * mat.extraParams.y;
    if (texEmis >= 0) {
        float3 e = triPlanar ? SampleTriPlanarLevel0(texEmis, P, worldNormal, triScale, triSharp).rgb
                             : textures[texEmis].SampleLevel(linearSampler, uv, 0).rgb;
        emissive *= sRGBToLinear(e);
        // InterlockedAdd(g_shaderCounters[SHADER_COUNTER_TEXTURE_SAMPLES], 1);
    }
    
    // Debug Pass
    int mode = (int)SHADER_DEBUG_MODE;
    if (mode == 1) { 
        payload.color = BaseColor;
        payload.t = RayTCurrent();
        return;
    }
    if (mode == 2) { payload.color = N * 0.5 + 0.5; payload.t = RayTCurrent(); return; }
    if (mode == 3) { payload.color = emissive; payload.t = RayTCurrent(); return; }
    if (mode == 4) { payload.color = float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness); payload.t = RayTCurrent(); return; }
    if (mode == 5) { payload.color = F0; payload.t = RayTCurrent(); return; }
    if (mode == 6) { payload.color = float3(metalness, metalness, metalness); payload.t = RayTCurrent(); return; }
    if (mode == 7) { payload.color = float3(ao, ao, ao); payload.t = RayTCurrent(); return; }

    // Archviz extensions
    float translucency = saturate(arch0.w);
    
    float3 Lo = float3(0,0,0);
    float3 ambient = float3(0,0,0);

    // OPTIMIZATION:
    // Only calculate direct lighting (Shadow Ray) for GI Diffuse rays.
    // Primary rays use ReSTIR in RayGen and don't need this locally computed color.
    // IBL (ambient) is currently unused by both RayGen (for Primary) and Diffuse rays (which take Lo only).
    
    if (payload.rayType == RAY_TYPE_DIFFUSE)
    {
        // Calculate view direction correctly in world space
        float3 V = normalize(camPos - P);
        
        // Directional light
        float3 L = normalize(lightDir.xyz);
        float3 H = normalize(V + L);

        float clearcoat = saturate(arch0.x);
        float clearcoatRoughness = max(arch0.y, 0.02);

        // Cook-Torrance BRDF
        float NDF = DistributionGGX(N, H, roughness);
        float G = GeometrySmith(N, V, L, roughness);
        float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
        
        float3 numerator = NDF * G * F;
        float NdotV = max(dot(N, V), 0.0);
        float NdotL = saturate(dot(N, L));
        float denominator = 4.0 * NdotV * NdotL;
        float3 specular = numerator / max(denominator, 0.001);

        // Clearcoat (secondary GGX lobe, fixed dielectric F0)
        float3 coatSpecular = float3(0.0, 0.0, 0.0);
        if (clearcoat > 0.001) {
            float3 F0c = float3(0.04, 0.04, 0.04);
            float NDFc = DistributionGGX(N, H, clearcoatRoughness);
            float Gc = GeometrySmith(N, V, L, clearcoatRoughness);
            float3 Fc = FresnelSchlick(max(dot(H, V), 0.0), F0c);
            float3 numc = NDFc * Gc * Fc;
            float denc = 4.0 * NdotV * NdotL;
            coatSpecular = numc / max(denc, 0.001);
        }
        
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
        shadowPayload.color = float3(0,0,0);
        shadowPayload.albedo = float3(0,0,0);
        shadowPayload.emissive = float3(0,0,0);
        shadowPayload.refractionColor = float3(0,0,0);
        shadowPayload.ior = 1.0;
        shadowPayload.roughness = 1.0;
        shadowPayload.metalness = 0.0;
        shadowPayload.matIndex = 0;
        shadowPayload.t = 1.0; 
        shadowPayload.rayDepth = 1;
        shadowPayload.rayType = RAY_TYPE_SHADOW;
        
        TraceRay(g_accel, RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_FORCE_NON_OPAQUE, 0xFF, 0, 0, 0, shadowRay, shadowPayload);
        
        if (shadowPayload.t > 0.0) shadowed = 0.0;

        // Outgoing radiance
        float3 radiance = lightColor.rgb * lightColor.w;

        float3 baseLo = (kD * DiffuseAlbedo / PI + specular) * radiance * NdotL * shadowed;
        float3 coatLo = coatSpecular * radiance * NdotL * shadowed;

        Lo = baseLo * (1.0 - clearcoat) + coatLo * clearcoat;

        if (translucency > 0.001) {
            float NdotL_back = saturate(dot(-N, L));
            Lo += (DiffuseAlbedo / PI) * radiance * NdotL_back * translucency;
        }
    }
    
    // For diffuse transport rays used by path tracing GI probes, avoid adding
    // unoccluded ambient IBL here. GI raygen will add emissive separately.
    float3 color = (ambient + Lo + emissive) * intensity;
    if (payload.rayType == RAY_TYPE_DIFFUSE) {
        color = Lo;
    }
    
    // In PT mode, we skip tone mapping here and do it in RayGen after accumulation
    payload.color = color;
    payload.t = RayTCurrent();
    payload.refractionColor = refrColor.rgb;
    payload.ior = emisColor.w;
    payload.normal = N;
    payload.position = P;
    payload.albedo = BaseColor;
    payload.emissive = emissive;
    payload.roughness = roughness;
    payload.metalness = metalness;
    payload.thinWalled = arch0.z;
    payload.translucency = translucency;
    payload.matIndex = (uint)mesh.materialIndex;
}

[shader("anyhit")]
void AnyHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    // For shadow or diffuse (GI visibility) rays hitting glass or thin-walled materials, we want to let light through.
    // This is a critical optimization for interior scenes with windows.
    if (payload.rayType == RAY_TYPE_SHADOW || payload.rayType == RAY_TYPE_DIFFUSE) {
        uint meshIdx = InstanceID();
        MeshData mesh = meshData[meshIdx];
        uint matIdx = (uint)max(0, mesh.materialIndex);
        MaterialData mat = materials[matIdx];
        
        // Let light through if it's a glass-like material:
        // 1. Has refraction color (transparent)
        // 2. Is marked as thin-walled (architectural glass)
        // 3. Has translucency
        if (length(mat.refractionColor.rgb) > 0.01 || mat.archvizParams0.z > 0.5 || mat.archvizParams0.w > 0.01) {
            IgnoreHit();
        }
    }
}
