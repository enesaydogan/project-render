// shaders/raytracing/hit.hlsl
// Closest hit shader with full PBR lighting

#include "common.hlsli"

// BRDF helpers: using D_GGX, V_SmithCorrelated, F_Schlick from brdf_lib.hlsl
// (included via path_tracer_core.hlsl before this file)

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

float CalculateTextureLod(uint rayType, float3 worldPos)
{
    if (rayType == RAY_TYPE_PRIMARY) return 0.0;
    float pathDistance = max(length(worldPos - camPos), 1e-3);
    float lod = log2(pathDistance * 0.02) + 0.35;
    return clamp(lod, 0.0, 10.0);
}

float4 SampleTriPlanar(int texIndex, float3 worldPos, float3 worldNormal, float scale, float sharpness, float lod, bool dominantAxisOnly)
{
    if (texIndex < 0) return float4(1,1,1,1);
    if (dominantAxisOnly) {
        float3 an = abs(worldNormal);
        if (an.x >= an.y && an.x >= an.z) return textures[texIndex].SampleLevel(linearSampler, TriPlanarUV_X(worldPos, scale), lod);
        if (an.y >= an.z) return textures[texIndex].SampleLevel(linearSampler, TriPlanarUV_Y(worldPos, scale), lod);
        return textures[texIndex].SampleLevel(linearSampler, TriPlanarUV_Z(worldPos, scale), lod);
    }
    float3 w = TriPlanarWeights(worldNormal, sharpness);
    float4 sx = textures[texIndex].SampleLevel(linearSampler, TriPlanarUV_X(worldPos, scale), lod);
    float4 sy = textures[texIndex].SampleLevel(linearSampler, TriPlanarUV_Y(worldPos, scale), lod);
    float4 sz = textures[texIndex].SampleLevel(linearSampler, TriPlanarUV_Z(worldPos, scale), lod);
    return sx * w.x + sy * w.y + sz * w.z;
}

float3 UnpackNormal(float4 n)
{
    return n.xyz * 2.0 - 1.0;
}

float3 SampleTriPlanarNormal(int texIndex, float3 worldPos, float3 worldNormal, float scale, float sharpness, float strength, float lod, bool dominantAxisOnly)
{
    if (texIndex < 0) return normalize(worldNormal);
    float3 Nw = normalize(worldNormal);
    float3 w = dominantAxisOnly ? float3(0,0,0) : TriPlanarWeights(Nw, sharpness);

    float3 nx = UnpackNormal(textures[texIndex].SampleLevel(linearSampler, TriPlanarUV_X(worldPos, scale), lod));
    float3 ny = UnpackNormal(textures[texIndex].SampleLevel(linearSampler, TriPlanarUV_Y(worldPos, scale), lod));
    float3 nz = UnpackNormal(textures[texIndex].SampleLevel(linearSampler, TriPlanarUV_Z(worldPos, scale), lod));
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

    if (dominantAxisOnly) {
        float3 an = abs(Nw);
        if (an.x >= an.y && an.x >= an.z) return wx;
        if (an.y >= an.z) return wy;
        return wz;
    }

    return normalize(wx * w.x + wy * w.y + wz * w.z);
}

// Extract normal from normal map and transform to world space
float3 GetNormalFromMap(float2 uv, float3 worldNormal, float4 worldTangent, int normalTexIndex, float lod)
{
    if (normalTexIndex < 0 || dot(worldTangent.xyz, worldTangent.xyz) < 1e-6) return normalize(worldNormal);
    
    float3 tangentNormal = textures[normalTexIndex].SampleLevel(linearSampler, uv, lod).xyz * 2.0 - 1.0;
    
    float3 N = normalize(worldNormal);
    float3 T = normalize(worldTangent.xyz);
    float3 B = cross(N, T) * worldTangent.w;
    float3x3 TBN = float3x3(T, B, N);
    
    return normalize(mul(tangentNormal, TBN));
}

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    uint rayType = UnpackPayloadRayType(payload.packedIorType);

    // Access mesh and material for this instance
    uint meshIdx = InstanceID();
    MeshData mesh = meshData[meshIdx];
    uint matIdx = (uint)max(0, mesh.materialIndex);
    MaterialData mat = materials[matIdx];
    MaterialExtraData matExtra = materialExtras[matIdx];

    // Get material properties
    float4 diffColor = mat.baseColor_opacity;
    float4 emisColor = mat.emissive_ior; // w=ior
    float4 pbr = mat.pbrParams_flags;    // x=metal, y=rough, z=transmission, w=flags
    uint matFlags = asuint(pbr.w);

    int texDiff = UnpackTextureIndexLow(mat.packedTextures.x);
    int texNorm = UnpackTextureIndexHigh(mat.packedTextures.x);
    int texMR   = UnpackTextureIndexLow(mat.packedTextures.y);
    int texOcc  = UnpackTextureIndexHigh(mat.packedTextures.y);
    int texEmis = UnpackTextureIndexLow(mat.packedTextures.z);

    float4 arch0 = matExtra.archvizParams0;
    float4 uvXf = matExtra.uvTransform;
    float4 triP = matExtra.triPlanarParams;

#ifdef HIT_DEBUG
    // Encode primitive index into color for debugging
    uint primIndex = PrimitiveIndex();
    float r = (float)(primIndex & 0xFF) / 255.0f;
    float g = (float)((primIndex >> 8) & 0xFF) / 255.0f;
    PayloadSetColor(payload, float3(r, g, 0.0));
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
    SHADER_COUNTER_ADD(SHADER_COUNTER_INDEX_LOADS, 3);
    SHADER_COUNTER_ADD(SHADER_COUNTER_VERTEX_FETCHES, 3);
    
    // Interpolate UV
    float2 uv0 = vertices[mesh.vbIndex][i0].uv;
    float2 uv1 = vertices[mesh.vbIndex][i1].uv;
    float2 uv2 = vertices[mesh.vbIndex][i2].uv;
    float2 uv = uv0 * bary.x + uv1 * bary.y + uv2 * bary.z;

    // Material UV transform (real-world scaling control)
    if ((matFlags & MATERIAL_FLAG_UV_TRANSFORM) != 0) {
        uv = uv * uvXf.xy + uvXf.zw;
    }

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
    // Normals must be transformed by the inverse-transpose of the model matrix
    // to handle non-uniform scaling correctly. In DXR:
    //   mul(v, M) = transpose(M)*v, so mul(normal, WorldToObject) = inv(transpose(ObjectToWorld))*normal
    float3 worldNormal = normalize(mul(localNormal, (float3x3)WorldToObject3x4()));
    float4 worldTangent;
    // Tangents (directions) use ObjectToWorld directly
    worldTangent.xyz = normalize(mul((float3x3)ObjectToWorld3x4(), localTangent.xyz));
    worldTangent.w = localTangent.w;
    
    bool triPlanar = ((matFlags & MATERIAL_FLAG_TRI_PLANAR) != 0) && (triP.x > 0.5);
    float triScale = max(triP.y, 1e-6);
    float triSharp = max(triP.z, 0.01);
    float triNormStrength = max(triP.w, 0.0);
    float textureLod = CalculateTextureLod(rayType, P);
    bool dominantTriPlanar = triPlanar && (rayType != RAY_TYPE_PRIMARY);

    // Sample textures
    float3 BaseColor = diffColor.rgb;
    int mode = (int)SHADER_DEBUG_MODE;
    if (texDiff >= 0) {
        float3 bc = triPlanar ? SampleTriPlanar(texDiff, P, worldNormal, triScale, triSharp, textureLod, dominantTriPlanar).rgb
                              : textures[texDiff].SampleLevel(linearSampler, uv, textureLod).rgb;
        BaseColor *= sRGBToLinear(bc);
        SHADER_COUNTER_ADD(SHADER_COUNTER_TEXTURE_SAMPLES, 1);
    }
    
    float metalness = saturate(pbr.x);
    float roughness = max(saturate(pbr.y), 0.02);
    
    // Metal/Roughness Logic: factor * texture
    if (texMR >= 0) {
        float4 mrSample = triPlanar ? SampleTriPlanar(texMR, P, worldNormal, triScale, triSharp, textureLod, dominantTriPlanar)
                                    : textures[texMR].SampleLevel(linearSampler, uv, textureLod);
        roughness *= mrSample.g; 
        metalness *= mrSample.b;
        SHADER_COUNTER_ADD(SHADER_COUNTER_TEXTURE_SAMPLES, 1);
    }

    // Clamp to reduce fireflies / unstable highlights in archviz scenes
    roughness = max(roughness, 0.02);
    
    // Attenuate diffuse by transmission (refraction) for dielectrics.
    // This removes the "tint" or "solid" look from glass.
    float transmission = saturate(pbr.z) * (1.0 - metalness);
    float3 DiffuseAlbedo = BaseColor * (1.0 - metalness) * (1.0 - transmission);

    // Standard PBR Model (dielectric F0 from IOR)
    float ior = max(emisColor.w, 1.0);
    float f0s = (ior - 1.0) / (ior + 1.0);
    f0s = f0s * f0s;
    float3 F0 = lerp(float3(f0s, f0s, f0s), BaseColor, metalness);
    
    // Normal mapping
    float3 N = triPlanar ? SampleTriPlanarNormal(texNorm, P, worldNormal, triScale, triSharp, triNormStrength, textureLod, dominantTriPlanar)
                         : GetNormalFromMap(uv, worldNormal, worldTangent, texNorm, textureLod);
    // Two-sided shading guard for reverse-oriented faces.
    float3 viewDirTwoSided = normalize(-WorldRayDirection());
    if (dot(N, viewDirTwoSided) < 0.0) N = -N;
    
    // Ambient occlusion (only needed for GI_EVAL which computes Lo)
    float ao = 1.0;
    if (texOcc >= 0 && rayType == RAY_TYPE_GI_EVAL) {
        ao = triPlanar ? SampleTriPlanar(texOcc, P, worldNormal, triScale, triSharp, textureLod, dominantTriPlanar).r
                       : textures[texOcc].SampleLevel(linearSampler, uv, textureLod).r;
        SHADER_COUNTER_ADD(SHADER_COUNTER_TEXTURE_SAMPLES, 1);
    }
    
    // Emissive with a conservative default boost.
    const float baseEmissiveBoost = 5.0f;
    float3 emissive = emisColor.rgb * baseEmissiveBoost;
    if (texEmis >= 0) {
        float3 e = triPlanar ? SampleTriPlanar(texEmis, P, worldNormal, triScale, triSharp, textureLod, dominantTriPlanar).rgb
                             : textures[texEmis].SampleLevel(linearSampler, uv, textureLod).rgb;
        emissive *= sRGBToLinear(e);
        SHADER_COUNTER_ADD(SHADER_COUNTER_TEXTURE_SAMPLES, 1);
    }
    
    // Debug Pass
    if (mode == 1) { 
        PayloadSetColor(payload, BaseColor);
        payload.t = RayTCurrent();
        return;
    }
    if (mode == 2) { PayloadSetColor(payload, N * 0.5 + 0.5); payload.t = RayTCurrent(); return; }
    if (mode == 3) { PayloadSetColor(payload, emissive); payload.t = RayTCurrent(); return; }
    if (mode == 4) { PayloadSetColor(payload, float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness)); payload.t = RayTCurrent(); return; }
    if (mode == 5) { PayloadSetColor(payload, F0); payload.t = RayTCurrent(); return; }
    if (mode == 6) { PayloadSetColor(payload, float3(metalness, metalness, metalness)); payload.t = RayTCurrent(); return; }
    if (mode == 7) { PayloadSetColor(payload, float3(ao, ao, ao)); payload.t = RayTCurrent(); return; }

    // Archviz extensions
    float translucency = saturate(arch0.w);
    
    float3 Lo = float3(0,0,0);
    float3 ambient = float3(0,0,0);

    // OPTIMIZATION:
    // Only calculate direct lighting (Shadow Ray) for GI Diffuse rays.
    // Primary rays use ReSTIR in RayGen and don't need this locally computed color.
    // IBL (ambient) is currently unused by both RayGen (for Primary) and Diffuse rays (which take Lo only).
    
    if (rayType == RAY_TYPE_GI_EVAL)
    {
        // Simplified diffuse-only evaluation for GI bounces.
        // Specular and clearcoat are skipped: the path tracer handles specular
        // via BRDF importance sampling; GI reservoirs only carry diffuse transport.
        float3 L = normalize(lightDir.xyz);
        float NdotL = saturate(dot(N, L));

        // Skip shadow ray entirely when sun is below hemisphere
        if (NdotL > 0.0) {
            RayDesc shadowRay;
            shadowRay.Origin = P + N * 0.002;
            shadowRay.Direction = L;
            shadowRay.TMin = 0.002;
            shadowRay.TMax = 1000.0;

            RayPayload shadowPayload;
            shadowPayload.t = 1.0;
            PayloadSetColor(shadowPayload, float3(0.0, 0.0, 0.0));
            shadowPayload.packedNormal = PackNormalOctahedron(float3(0.0, 1.0, 0.0));
            shadowPayload.packedAlbedo = PackPayloadAlbedo(float3(0.0, 0.0, 0.0));
            shadowPayload.packedSurface = PackPayloadSurface(1.0, 0.0, 0.0, 0.0);
            shadowPayload.packedIorType = PackPayloadIorType(1.0, RAY_TYPE_SHADOW, false);

            TraceRay(g_accel, RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xFF, 0, 0, 0, shadowRay, shadowPayload);

            if (shadowPayload.t < 0.0) { // Miss = not occluded
                float3 radiance = lightColor.rgb * lightColor.w;
                Lo = (DiffuseAlbedo / PI) * radiance * NdotL;
            }
        }

        // Back-face diffuse translucency (not shadow-tested, intentional for thin geometry)
        if (translucency > 0.001) {
            float NdotL_back = saturate(dot(-N, normalize(lightDir.xyz)));
            if (NdotL_back > 0.0) {
                Lo += (DiffuseAlbedo / PI) * (lightColor.rgb * lightColor.w) * NdotL_back * translucency;
            }
        }
    }
    
    // Apply AO to diffuse and ambient lighting
    Lo *= ao;
    
    // Keep payload color compact and purpose-specific:
    // - regular path rays carry emissive only (direct/indirect handled in raygen)
    // - GI evaluation rays carry local diffuse+emissive estimate
    float3 color = emissive * intensity;
    if (rayType == RAY_TYPE_GI_EVAL) {
        color = (Lo + emissive) * intensity;
    }
    
    // In PT mode, we skip tone mapping here and do it in RayGen after accumulation
    PayloadSetColor(payload, color);
    payload.t = RayTCurrent();
    payload.packedNormal = PackNormalOctahedron(N);
    payload.packedAlbedo = PackPayloadAlbedo(BaseColor);
    bool thinWalled = ((matFlags & MATERIAL_FLAG_THIN_WALLED) != 0) || (arch0.z > 0.5);
    payload.packedSurface = PackPayloadSurface(roughness, metalness, transmission, translucency);
    payload.packedIorType = PackPayloadIorType(emisColor.w, rayType, thinWalled);
}

[shader("anyhit")]
void AnyHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    uint rayType = UnpackPayloadRayType(payload.packedIorType);
    // For shadow or diffuse (GI visibility) rays hitting glass or thin-walled materials, we want to let light through.
    // This is a critical optimization for interior scenes with windows.
    if (rayType == RAY_TYPE_SHADOW || rayType == RAY_TYPE_DIFFUSE || rayType == RAY_TYPE_GI_EVAL) {
        uint meshIdx = InstanceID();
        MeshData mesh = meshData[meshIdx];
        uint matIdx = (uint)max(0, mesh.materialIndex);
        MaterialData mat = materials[matIdx];
        uint matFlags = asuint(mat.pbrParams_flags.w);
        
        // Let light through if it's a glass-like material:
        // 1. Glass flag
        // 2. Thin-walled flag
        // 3. Translucency flag
        if ((matFlags & (MATERIAL_FLAG_GLASS | MATERIAL_FLAG_THIN_WALLED | MATERIAL_FLAG_TRANSLUCENT)) != 0) {
            IgnoreHit();
        }
    }
}
