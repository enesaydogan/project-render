cbuffer CameraCB : register(b0)
{
    float3 pos;
    float debugMode;
    float3 forward;
    float _pad1;
    float3 up;
    float _pad2;
    float fov;
    float aspect;
    float nearZ;
    float farZ;
    float intensity;
    float globalFrameCount; // was frameCount
    float lightCount;
    float maxSpecularBounces;
    float maxRefractiveBounces;
    float maxGIBounces;
    float maxSPP;
    float accumulationCount;
    
    // Global Lighting
    float4 lightDir; // xyz = direction towards light
    float4 lightColor; // rgb + intensity in .w

    // Keep layout aligned with src/camera.h for trailing fields.
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
    float debugVisualizationMode;
    float cloudRenderingEnabled;
    float iblRotationDegrees;
    float sampleEnvSolidAngle;
    float nrdEnabled;
    float _pad3;
    float4x4 shadowMatrix;
    float4x4 viewProj;
    float4x4 invViewProj;
};

cbuffer WorldCB : register(b2)
{
    float4x4 world;
};

cbuffer MaterialCB : register(b1)
{
    float4 diffuseColor;        // rgb, a=opacity
    float4 surfaceParams;       // x=roughness, y=metalness, z=specularWeight
    float4 transmissionParams;  // rgb=transmissionColor, a=transmissionWeight
    float4 emissiveColor;       // rgb, w=ior
    int4 textureIndices;        // x=diffuse, z=normal
    int4 emissiveAndPad;        // x=emissive, y=occlusion, z=metalRough
    float4 extraParams;         // x=emissiveIntensity
    float4 coatLayerParams;     // x=coatWeight, y=coatRoughness, z=thinWalled, w=translucency
    float4 uvTransform;         // xy=uvScale, zw=uvOffset
    float4 triPlanarParams;     // x=enabled, y=scale, z=sharpness, w=normalStrength
};

// Texture array - bonded as an unbounded array in SM 6.x
Texture2D textures[] : register(t0);
Texture2D envMap : register(t0, space1);
Texture2D shadowMap : register(t1, space1);

SamplerState linearSampler : register(s0);
SamplerComparisonState shadowSampler : register(s1, space1);

float2 DirectionToUV(float3 dir) {
    float2 uv;
    uv.x = atan2(dir.x, dir.z) / (2.0 * 3.14159265) + 0.5;
    uv.y = acos(clamp(dir.y, -1.0, 1.0)) / 3.14159265;
    uv.x = frac(uv.x + (iblRotationDegrees / 360.0));
    return uv;
}

float3 sRGBToLinear(float3 sRGB) {
    return pow(max(sRGB, 0.0), 2.2);
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

float4 SampleTriPlanar(int texIndex, float3 worldPos, float3 worldNormal, float scale, float sharpness)
{
    if (texIndex < 0) return float4(1,1,1,1);
    float3 w = TriPlanarWeights(worldNormal, sharpness);
    float4 sx = textures[texIndex].Sample(linearSampler, TriPlanarUV_X(worldPos, scale));
    float4 sy = textures[texIndex].Sample(linearSampler, TriPlanarUV_Y(worldPos, scale));
    float4 sz = textures[texIndex].Sample(linearSampler, TriPlanarUV_Z(worldPos, scale));
    return sx * w.x + sy * w.y + sz * w.z;
}

float3 UnpackNormal(float4 n)
{
    return n.xyz * 2.0 - 1.0;
}

float3 SampleTriPlanarNormal(int texIndex, float3 worldPos, float3 worldNormal, float scale, float sharpness, float strength)
{
    if (texIndex < 0) return normalize(worldNormal);
    float3 Nw = normalize(worldNormal);
    float3 w = TriPlanarWeights(Nw, sharpness);

    float3 nx = UnpackNormal(textures[texIndex].Sample(linearSampler, TriPlanarUV_X(worldPos, scale)));
    float3 ny = UnpackNormal(textures[texIndex].Sample(linearSampler, TriPlanarUV_Y(worldPos, scale)));
    float3 nz = UnpackNormal(textures[texIndex].Sample(linearSampler, TriPlanarUV_Z(worldPos, scale)));
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

// ACES Tone Mapping
float3 ToneMap(float3 x) {
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

struct VSInputMesh {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 uv : TEXCOORD0;
};

struct PSInputMesh {
    float4 position : SV_POSITION;
    float3 worldPos : POSITION0;
    float3 normal : NORMAL;
    float4 tangent : TANGENT0;
    float2 uv : TEXCOORD0;
};

struct VSOutputShadow {
    float4 position : SV_POSITION;
};

PSInputMesh VSMainMesh(VSInputMesh input)
{
    PSInputMesh o;

    // World-space position
    float4 worldPos = mul(world, float4(input.position, 1.0f));
    float3 outWorldPos = worldPos.xyz;

    // Use a standard right-handed view basis for the camera
    float3 R = normalize(cross(forward, up)); // Right (F x U in RH with F pointing away)
    float3 U = normalize(cross(R, forward));  // Up (orthonormal)
    
    float3 rel = outWorldPos - pos;
    float3 viewPos;
    viewPos.x = dot(rel, R);
    viewPos.y = dot(rel, U);
    viewPos.z = dot(rel, forward);
    
    // Build projection matrix
    // Convert FOV to radians and compute focal term
    float f = 1.0f / tan(radians(fov) * 0.5f);

    // Apply projection (D3D clip space: z in [0,1])
    // Standard perspective: W=Z_view, X_ndc = X_view * f / aspect, Y_ndc = Y_view * f
    float A = farZ / (farZ - nearZ);
    float B = -nearZ * farZ / (farZ - nearZ);
    o.position = float4(
        viewPos.x * f / aspect,
        viewPos.y * f,
        viewPos.z * A + B,
        viewPos.z
    );

    o.worldPos = outWorldPos;
    o.normal = mul((float3x3)world, input.normal);
    o.tangent = float4(mul((float3x3)world, input.tangent.xyz), input.tangent.w);
    o.uv = input.uv;
    return o;
}

VSOutputShadow VSMainShadow(VSInputMesh input)
{
    VSOutputShadow o;
    float4 worldPos = mul(world, float4(input.position, 1.0f));
    o.position = mul(shadowMatrix, worldPos);
    return o;
}

// Improved microfacet BRDF helpers
static const float PI = 3.14159265359;

// GGX/Trowbridge-Reitz normal distribution with anisotropy support
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

// Smith's shadowing-masking function with height-correlated masking
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

// Fresnel-Schlick approximation with roughness for energy compensation
float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}
float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    return F0 + (max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0) - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// Extract normal from normal map and transform to world space
float3 GetNormalFromMap(float2 uv, float3 worldNormal, float4 worldTangent, int normalTexIndex)
{
    if (normalTexIndex < 0 || length(worldTangent.xyz) < 0.001) return normalize(worldNormal);
    
    float3 tangentNormal = textures[normalTexIndex].Sample(linearSampler, uv).xyz * 2.0 - 1.0;
    
    float3 N = normalize(worldNormal);
    float3 T = normalize(worldTangent.xyz);
    float3 B = cross(N, T) * worldTangent.w;
    float3x3 TBN = float3x3(T, B, N);
    
    return normalize(mul(tangentNormal, TBN));
}

float CalculateShadow(float3 worldPos)
{
    float4 shadowPos = mul(shadowMatrix, float4(worldPos, 1.0));
    shadowPos.xyz /= shadowPos.w;
    
    // Transform to [0,1] range for UV sampling
    float2 shadowUV = shadowPos.xy * 0.5 + 0.5;
    shadowUV.y = 1.0 - shadowUV.y;

    if (shadowUV.x < 0 || shadowUV.x > 1 || shadowUV.y < 0 || shadowUV.y > 1) return 1.0;
    
    float currentDepth = shadowPos.z;
    if (currentDepth > 1.0) return 1.0;

    // 3x3 PCF
    float shadow = 0.0;
    float2 texelSize = 1.0 / 2048.0;
    for(int x = -1; x <= 1; ++x)
    {
        for(int y = -1; y <= 1; ++y)
        {
            shadow += shadowMap.SampleCmpLevelZero(shadowSampler, shadowUV + float2(x, y) * texelSize, currentDepth - 0.0005).r;
        }
    }
    return shadow / 9.0;
}

struct PSOutput {
    float4 color : SV_Target0;
    float4 normal : SV_Target1;
};

PSOutput PSMainMesh(PSInputMesh input)
{
#ifdef RASTER_DEBUG_DEPTH
    // Output clip-space depth as grayscale for debugging
    float clipW = input.position.w;
    float depthVal = 0.0f;
    if (abs(clipW) > 1e-6) {
        depthVal = saturate(input.position.z / clipW);
    }
    PSOutput o_depth;
    o_depth.color = float4(depthVal, depthVal, depthVal, 1.0);
    o_depth.normal = float4(0,0,0,1);
    return o_depth;
#endif
#ifdef RASTER_DEBUG_UV
    // Debug mode: output UVs in RGB for quick comparison with RayGen UV output
    PSOutput o;
    o.color = float4(input.uv.xy, 0.0, 1.0);
    o.normal = float4(0,0,0,1);
    return o;
#endif

    // --- Texture Lookups ---
    float2 uv = input.uv * uvTransform.xy + uvTransform.zw;
    float3 worldPos = input.worldPos;
    float3 worldNormal = normalize(input.normal);

    bool triPlanar = (triPlanarParams.x > 0.5);
    float triScale = max(triPlanarParams.y, 1e-6);
    float triSharp = max(triPlanarParams.z, 0.01);
    float triNormStrength = max(triPlanarParams.w, 0.0);

    float3 BaseColor = diffuseColor.rgb;
    float alpha = diffuseColor.a;
    if (textureIndices.x >= 0) {
        float4 diffSample = triPlanar ? SampleTriPlanar(textureIndices.x, worldPos, worldNormal, triScale, triSharp)
                                      : textures[textureIndices.x].Sample(linearSampler, uv);
        BaseColor *= sRGBToLinear(diffSample.rgb);
        alpha *= diffSample.a;
    }

    float roughness = saturate(surfaceParams.x);
    float metalness = saturate(surfaceParams.y);
    float transmission = saturate(transmissionParams.a) * (1.0 - metalness);
    
    // Metal/Roughness Logic: factor * texture
    // G = Roughness, B = Metalness
    if (emissiveAndPad.z >= 0) {
        float4 mrSample = triPlanar ? SampleTriPlanar(emissiveAndPad.z, worldPos, worldNormal, triScale, triSharp)
                                    : textures[emissiveAndPad.z].Sample(linearSampler, uv);
        roughness *= mrSample.g; 
        metalness *= mrSample.b;
    }
    
    // OpenPBR subset: dielectric F0 from IOR scaled by specular weight.
    float ior = max(emissiveColor.w, 1.0);
    float specularWeight = saturate(surfaceParams.z);
    float f0s = (ior - 1.0) / (ior + 1.0);
    f0s = f0s * f0s;
    float3 dielectricF0 = float3(f0s, f0s, f0s) * specularWeight;
    float3 F0 = lerp(dielectricF0, BaseColor, metalness);
    float3 DiffuseAlbedo = BaseColor * (1.0 - metalness) * (1.0 - transmission);
    
    // Normal
    float3 N = triPlanar ? SampleTriPlanarNormal(textureIndices.z, worldPos, worldNormal, triScale, triSharp, triNormStrength)
                         : GetNormalFromMap(uv, worldNormal, input.tangent, textureIndices.z);

    // Emissive with user-defined intensity
    float3 emiss = emissiveColor.rgb * extraParams.x;
    if (emissiveAndPad.x >= 0) {
        float3 e = triPlanar ? SampleTriPlanar(emissiveAndPad.x, worldPos, worldNormal, triScale, triSharp).rgb
                             : textures[emissiveAndPad.x].Sample(linearSampler, uv).rgb;
        emiss *= sRGBToLinear(e);
    } 

    // Occlusion
    float ao = 1.0;
    if (emissiveAndPad.y >= 0) {
        ao = triPlanar ? SampleTriPlanar(emissiveAndPad.y, worldPos, worldNormal, triScale, triSharp).r
                       : textures[emissiveAndPad.y].Sample(linearSampler, uv).r;
    }

    // Lighting
    float3 V = normalize(pos - input.worldPos);
    // Two-sided shading guard for assets with inconsistent face orientation.
    if (dot(N, V) < 0.0) N = -N;
    float3 L = normalize(lightDir.xyz);
    if (length(lightDir.xyz) < 0.001) L = float3(0, 1, 0);
    float3 H = normalize(V + L);

    // Clamp to reduce fireflies / unstable highlights in archviz scenes
    roughness = max(roughness, 0.02);

    float clearcoat = saturate(coatLayerParams.x);
    float clearcoatRoughness = max(coatLayerParams.y, 0.02);
    float translucency = saturate(coatLayerParams.w);

    // Cook-Torrance BRDF
    float NdotV = saturate(dot(N, V));
    float NdotL = saturate(dot(N, L));
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));

    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    float3 F = FresnelSchlick(VdotH, F0);

    float3 numerator = NDF * G * F;
    float denominator = 4.0 * NdotV * NdotL + 0.0001;
    float3 spec = numerator / denominator;

    // Clearcoat (secondary GGX lobe)
    float3 coatSpec = float3(0.0, 0.0, 0.0);
    if (clearcoat > 0.001) {
        float3 F0c = float3(0.04, 0.04, 0.04);
        float NDFc = DistributionGGX(N, H, clearcoatRoughness);
        float Gc = GeometrySmith(N, V, L, clearcoatRoughness);
        float3 Fc = FresnelSchlick(VdotH, F0c);
        float3 numc = NDFc * Gc * Fc;
        float denc = 4.0 * NdotV * NdotL + 0.0001;
        coatSpec = numc / denc;
    }
    
    float3 diffuseTerm = (DiffuseAlbedo / PI) * (1.0 - F);

    float3 radiance = lightColor.rgb * lightColor.w;
    
    float3 baseDirect = (diffuseTerm + spec) * radiance * NdotL;
    float3 coatDirect = coatSpec * radiance * NdotL;
    float3 directLight = baseDirect * (1.0 - clearcoat) + coatDirect * clearcoat;

    // Modulate direct light by shadow
    float shadow = CalculateShadow(input.worldPos);
    directLight *= shadow;

    // Backlighting translucency approximation
    if (translucency > 0.001) {
        float NdotL_back = saturate(dot(-N, L));
        directLight += (DiffuseAlbedo / PI) * radiance * NdotL_back * translucency * shadow;
    }
    
    // IBL (Image Based Lighting)
    float3 R = reflect(-V, N);
    float3 F_ibl = FresnelSchlickRoughness(NdotV, F0, roughness);
    float3 kS_ibl = F_ibl;
    float3 kD_ibl = 1.0 - kS_ibl;

    float2 envUV_diff = DirectionToUV(N);
    float3 irradiance = envMap.SampleLevel(linearSampler, envUV_diff, 7.0).rgb;
    float3 diffuse_ibl = kD_ibl * irradiance * (DiffuseAlbedo / PI);

    float2 envUV_spec = DirectionToUV(R);
    float3 prefilteredColor = envMap.SampleLevel(linearSampler, envUV_spec, roughness * 7.0).rgb;
    float3 specular_ibl = kS_ibl * prefilteredColor;

    float3 coat_ibl = float3(0.0, 0.0, 0.0);
    if (clearcoat > 0.001) {
        float2 envUV_spec = DirectionToUV(R);
        float3 prefilteredCoat = envMap.SampleLevel(linearSampler, envUV_spec, clearcoatRoughness * 7.0).rgb;
        float3 F0c = float3(0.04, 0.04, 0.04);
        float3 Fc_ibl = FresnelSchlickRoughness(NdotV, F0c, clearcoatRoughness);
        coat_ibl = Fc_ibl * prefilteredCoat;
    }

    // Raster has no true GI, but full env energy here reads too hot versus the scene.
    const float kRasterAmbientScale = 0.35;
    float3 envIBL = (diffuse_ibl + specular_ibl) * (ao * kRasterAmbientScale);
    float3 ambient = envIBL * (1.0 - clearcoat);
    if (clearcoat > 0.001) {
        float3 ambCoat = coat_ibl * (ao * kRasterAmbientScale);
        ambient += ambCoat * clearcoat;
    }

    if (translucency > 0.001) {
        float2 envUV_back = DirectionToUV(-N);
        float3 irradianceBack = envMap.SampleLevel(linearSampler, envUV_back, 7.0).rgb;
        float3 envBack = (DiffuseAlbedo * irradianceBack) * (ao * kRasterAmbientScale);
        ambient += envBack * translucency;
    }
    
    float3 color = directLight + ambient + emiss;
    
    // Exposure handling moved to tonemapping pass
    // color *= intensity;
    
    // DEBUG PASS
    int mode = (int)debugMode;
    if (mode > 0) {
        PSOutput o_dbg;
        o_dbg.normal = float4(N * 0.5 + 0.5, 1.0);
        if (mode == 1) o_dbg.color = float4(BaseColor, 1.0);
        else if (mode == 2) o_dbg.color = float4(N * 0.5 + 0.5, 1.0);
        else if (mode == 3) o_dbg.color = float4(emiss, 1.0);
        else if (mode == 4) o_dbg.color = float4(1.0 - roughness, 1.0 - roughness, 1.0 - roughness, 1.0);
        else if (mode == 5) o_dbg.color = float4(F0, 1.0);
        else if (mode == 6) o_dbg.color = float4(metalness, metalness, metalness, 1.0);
        else if (mode == 7) o_dbg.color = float4(ao, ao, ao, 1.0);
        else o_dbg.color = float4(0,0,0,1);
        return o_dbg;
    }

    PSOutput o;
    o.color = float4(color, alpha);
    o.normal = float4(N * 0.5 + 0.5, roughness);
    return o;
}
