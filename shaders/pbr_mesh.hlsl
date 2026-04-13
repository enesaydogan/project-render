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
    float exportRendering;
    float dxrProceduralSkyBoost;
    float tonemapAoIntensity;
    float tonemapAoRadiusMeters;
    float tonemapAoMode;
    float triPlanarWorldRotationDegrees;
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
    float4 mappingVariationParams; // x=mode, y=offsetJitter, z=materialRotationDegrees, w=reserved
    float4 textureWeight0;      // x=baseColor, y=packedSurface, z=metalness, w=roughnessGloss
    float4 textureWeight1;      // x=normal, y=occlusion, z=emissive
};

// Texture array - bonded as an unbounded array in SM 6.x
Texture2D textures[] : register(t0);
Texture2D envMap : register(t0, space1);
Texture2D shadowMap : register(t1, space1);
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
StructuredBuffer<FGrassPatch> g_grassInstances : register(t0, space3);
StructuredBuffer<uint4> g_grassVisible : register(t1, space3);

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

float Hash12(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float ValueNoise2D(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    float a = Hash12(i);
    float b = Hash12(i + float2(1.0, 0.0));
    float c = Hash12(i + float2(0.0, 1.0));
    float d = Hash12(i + float2(1.0, 1.0));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float GrassFieldNoise(float2 p)
{
    float v = 0.0;
    float w = 0.58;
    float n = 0.0;
    float2 q = p;
    [unroll]
    for (int octave = 0; octave < 3; ++octave) {
        v += ValueNoise2D(q) * w;
        n += w;
        q = q * 2.13 + 11.7;
        w *= 0.5;
    }
    return (n > 1e-5) ? (v / n) : 0.0;
}

float3 TriPlanarWeights(float3 n, float sharpness)
{
    float3 an = abs(n);
    an = pow(max(an, 0.0), max(sharpness, 0.01));
    float s = an.x + an.y + an.z;
    return (s > 1e-5) ? (an / s) : float3(0.3333, 0.3333, 0.3333);
}

uint HashTriPlanarU32(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float HashTriPlanar01(uint x)
{
    return (HashTriPlanarU32(x) & 0x00FFFFFFu) / 16777215.0;
}

float GetTriPlanarRotationDegrees()
{
    return triPlanarWorldRotationDegrees + mappingVariationParams.z;
}

float2 RotateAroundSceneUp(float2 xz, float degrees)
{
    float s, c;
    sincos(radians(degrees), s, c);
    return float2(xz.x * c - xz.y * s,
                  xz.x * s + xz.y * c);
}

float3 RotateTriPlanarPosition(float3 worldPos)
{
    float2 rotatedXZ = RotateAroundSceneUp(worldPos.xz,
                                           GetTriPlanarRotationDegrees());
    return float3(rotatedXZ.x, worldPos.y, rotatedXZ.y);
}

float3 RotateTriPlanarAxis(float3 axis)
{
    float2 rotatedXZ = RotateAroundSceneUp(axis.xz,
                                           GetTriPlanarRotationDegrees());
    return normalize(float3(rotatedXZ.x, axis.y, rotatedXZ.y));
}

uint ComputeTriPlanarVariationSeed(float3 objectOrigin, uint primitiveId)
{
    uint mode = (uint)round(mappingVariationParams.x);
    if (mode == 0u) {
        return 0u;
    }

    int3 quantizedOrigin = int3(round(objectOrigin * 100.0));
    uint seed = HashTriPlanarU32(
        asuint(quantizedOrigin.x) * 73856093u ^
        asuint(quantizedOrigin.y) * 19349663u ^
        asuint(quantizedOrigin.z) * 83492791u);
    if (mode >= 2u) {
        seed = HashTriPlanarU32(seed ^ ((primitiveId + 1u) * 0x9e3779b9u));
    }
    return seed;
}

float2 ComputeTriPlanarVariationOffset(float3 objectOrigin, float3 objectPos,
                                       float3 worldNormal)
{
    if (mappingVariationParams.x < 0.5 || mappingVariationParams.y <= 1.0e-4) {
        return float2(0.0, 0.0);
    }

    uint mode = (uint)round(mappingVariationParams.x);
    int3 quantizedOrigin = int3(round(objectOrigin * 100.0));
    uint seed = HashTriPlanarU32(
        asuint(quantizedOrigin.x) * 73856093u ^
        asuint(quantizedOrigin.y) * 19349663u ^
        asuint(quantizedOrigin.z) * 83492791u);
    if (mode >= 2u) {
        float3 an = abs(worldNormal);
        uint dominantAxis = (an.x >= an.y && an.x >= an.z) ? 0u :
                            ((an.y >= an.z) ? 1u : 2u);
        float planeValue = (dominantAxis == 0u) ? objectPos.x :
                           ((dominantAxis == 1u) ? objectPos.y : objectPos.z);
        int quantizedPlane = (int)round(planeValue * 100.0);
        seed = HashTriPlanarU32(seed ^ ((dominantAxis + 1u) * 0x9e3779b9u) ^
                                (asuint(quantizedPlane) * 0x85ebca6bu));
    }
    return (float2(HashTriPlanar01(seed ^ 0x68bc21ebu),
                   HashTriPlanar01(seed ^ 0x02e5be93u)) * 2.0 - 1.0) *
           mappingVariationParams.y;
}

float2 TriPlanarUV_X(float3 p, float3 n, float scale, float2 offset)
{
    float signX = (n.x >= 0.0) ? 1.0 : -1.0;
    return (float2(-signX * p.z, p.y) + offset) * scale;
}

float2 TriPlanarUV_Y(float3 p, float3 n, float scale, float2 offset)
{
    float signY = (n.y >= 0.0) ? 1.0 : -1.0;
    return (float2(p.x, -signY * p.z) + offset) * scale;
}

float2 TriPlanarUV_Z(float3 p, float3 n, float scale, float2 offset)
{
    float signZ = (n.z >= 0.0) ? 1.0 : -1.0;
    return (float2(signZ * p.x, p.y) + offset) * scale;
}

float4 SampleTriPlanar(int texIndex, float3 worldPos, float3 worldNormal,
                       float scale, float sharpness, float3 objectOrigin,
                       float3 objectPos)
{
    if (texIndex < 0) return float4(1,1,1,1);
    float3 rotatedPos = RotateTriPlanarPosition(worldPos);
    float2 variationOffset =
        ComputeTriPlanarVariationOffset(objectOrigin, objectPos, worldNormal);
    float3 w = TriPlanarWeights(worldNormal, sharpness);
    float4 sx = textures[texIndex].Sample(
        linearSampler,
        TriPlanarUV_X(rotatedPos, worldNormal, scale, variationOffset));
    float4 sy = textures[texIndex].Sample(
        linearSampler,
        TriPlanarUV_Y(rotatedPos, worldNormal, scale, variationOffset));
    float4 sz = textures[texIndex].Sample(
        linearSampler,
        TriPlanarUV_Z(rotatedPos, worldNormal, scale, variationOffset));
    return sx * w.x + sy * w.y + sz * w.z;
}

float3 UnpackNormal(float4 n)
{
    return n.xyz * 2.0 - 1.0;
}

float3 BlendTextureRgb(float3 sampleValue, float amount)
{
    return lerp(float3(1.0, 1.0, 1.0), sampleValue, saturate(amount));
}

float BlendTextureScalar(float sampleValue, float amount)
{
    return lerp(1.0, sampleValue, saturate(amount));
}

float3 BlendNormalSample(float3 tangentNormal, float amount)
{
    return normalize(lerp(float3(0.0, 0.0, 1.0), tangentNormal,
                          saturate(amount)));
}

float3 SampleTriPlanarNormal(int texIndex, float3 worldPos, float3 worldNormal,
                             float scale, float sharpness, float strength,
                             float amount, float3 objectOrigin,
                             float3 objectPos)
{
    if (texIndex < 0 || amount <= 0.0) return normalize(worldNormal);
    float3 Nw = normalize(worldNormal);
    float3 rotatedPos = RotateTriPlanarPosition(worldPos);
    float2 variationOffset =
        ComputeTriPlanarVariationOffset(objectOrigin, objectPos, Nw);
    float3 w = TriPlanarWeights(Nw, sharpness);

    float3 nx = UnpackNormal(textures[texIndex].Sample(
        linearSampler,
        TriPlanarUV_X(rotatedPos, Nw, scale, variationOffset)));
    float3 ny = UnpackNormal(textures[texIndex].Sample(
        linearSampler,
        TriPlanarUV_Y(rotatedPos, Nw, scale, variationOffset)));
    float3 nz = UnpackNormal(textures[texIndex].Sample(
        linearSampler,
        TriPlanarUV_Z(rotatedPos, Nw, scale, variationOffset)));
    nx = BlendNormalSample(nx, amount);
    ny = BlendNormalSample(ny, amount);
    nz = BlendNormalSample(nz, amount);
    nx.xy *= strength; ny.xy *= strength; nz.xy *= strength;
    nx = normalize(nx); ny = normalize(ny); nz = normalize(nz);

    float sx = (Nw.x >= 0.0) ? 1.0 : -1.0;
    float sy = (Nw.y >= 0.0) ? 1.0 : -1.0;
    float sz = (Nw.z >= 0.0) ? 1.0 : -1.0;

    float3 axisX = RotateTriPlanarAxis(float3(1,0,0));
    float3 axisZ = RotateTriPlanarAxis(float3(0,0,1));

    float3 Tx = -axisZ * sx;
    float3 Bx = float3(0,1,0);
    float3 Nx = axisX * sx;
    float3x3 TBNx = float3x3(Tx, Bx, Nx);

    float3 Ty = axisX;
    float3 By = -axisZ * sy;
    float3 Ny = float3(0,sy,0);
    float3x3 TBNy = float3x3(Ty, By, Ny);

    float3 Tz = axisX * sz;
    float3 Bz = float3(0,1,0);
    float3 Nz = axisZ * sz;
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
    float3 objectPos : POSITION1;
    float3 normal : NORMAL;
    float4 tangent : TANGENT0;
    float2 uv : TEXCOORD0;
    float grassVariation : TEXCOORD1;
    float2 emitterUv : TEXCOORD2;
};

struct VSOutputShadow {
    float4 position : SV_POSITION;
};

void BuildGrassBasis(FGrassPatch blade, out float3 right, out float3 upDir,
                     out float3 forwardDir)
{
    upDir = normalize((dot(blade.normal, blade.normal) > 1e-6)
                      ? blade.normal
                      : float3(0.0, 1.0, 0.0));
    float3 helper = (abs(upDir.y) > 0.9) ? float3(1.0, 0.0, 0.0)
                                         : float3(0.0, 1.0, 0.0);
    right = normalize(cross(helper, upDir));
    forwardDir = normalize(cross(upDir, right));

    float s, c;
    sincos(blade.yawRadians, s, c);
    float3 yawRight = right * c - forwardDir * s;
    float3 yawForward = right * s + forwardDir * c;
    right = yawRight;
    forwardDir = yawForward;
}

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
    o.objectPos = input.position;
    o.normal = mul((float3x3)world, input.normal);
    o.tangent = float4(mul((float3x3)world, input.tangent.xyz), input.tangent.w);
    o.uv = input.uv;
    o.grassVariation = 0.5;
    o.emitterUv = input.uv;
    return o;
}

PSInputMesh VSMainGrass(VSInputMesh input, uint instanceId : SV_InstanceID)
{
    PSInputMesh o;

    uint bladeIndex = g_grassVisible[instanceId + 1].x;
    FGrassPatch blade = g_grassInstances[bladeIndex];
    float bladeScale = max(blade.scale, 1e-3);

    float3 right;
    float3 upDir;
    float3 forwardDir;
    BuildGrassBasis(blade, right, upDir, forwardDir);

    float3 localPos = input.position * bladeScale;
    float3 outWorldPos = blade.position + right * localPos.x +
                         upDir * localPos.y + forwardDir * localPos.z;

    float3 worldNormal = normalize(right * input.normal.x +
                                   upDir * input.normal.y +
                                   forwardDir * input.normal.z);
    float3 worldTangent = normalize(right * input.tangent.x +
                                    upDir * input.tangent.y +
                                    forwardDir * input.tangent.z);

    float3 R = normalize(cross(forward, up));
    float3 U = normalize(cross(R, forward));

    float3 rel = outWorldPos - pos;
    float3 viewPos;
    viewPos.x = dot(rel, R);
    viewPos.y = dot(rel, U);
    viewPos.z = dot(rel, forward);

    float f = 1.0f / tan(radians(fov) * 0.5f);
    float A = farZ / (farZ - nearZ);
    float B = -nearZ * farZ / (farZ - nearZ);
    o.position = float4(
        viewPos.x * f / aspect,
        viewPos.y * f,
        viewPos.z * A + B,
        viewPos.z
    );

    o.worldPos = outWorldPos;
    o.objectPos = input.position;
    o.normal = worldNormal;
    o.tangent = float4(worldTangent, input.tangent.w);
    o.uv = input.uv;
    o.grassVariation = (blade.colorVariation & 0xFFFFu) / 65535.0;
    o.emitterUv = blade.emitterUv;
    return o;
}

VSOutputShadow VSMainShadow(VSInputMesh input)
{
    VSOutputShadow o;
    float4 worldPos = mul(world, float4(input.position, 1.0f));
    o.position = mul(worldPos, shadowMatrix);
    return o;
}

VSOutputShadow VSMainGrassShadow(VSInputMesh input, uint instanceId : SV_InstanceID)
{
    VSOutputShadow o;

    uint bladeIndex = g_grassVisible[instanceId + 1].x;
    FGrassPatch blade = g_grassInstances[bladeIndex];
    float bladeScale = max(blade.scale, 1e-3);

    float3 right;
    float3 upDir;
    float3 forwardDir;
    BuildGrassBasis(blade, right, upDir, forwardDir);

    float3 localPos = input.position * bladeScale;
    float3 worldPos = blade.position + right * localPos.x +
                      upDir * localPos.y + forwardDir * localPos.z;
    o.position = mul(float4(worldPos, 1.0f), shadowMatrix);
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

float2 EnvBRDFApprox(float3 F0, float roughness, float NdotV)
{
    float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    float4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    return float2(-1.04, 1.04) * a004 + r.zw;
}

// Extract normal from normal map and transform to world space
float3 GetNormalFromMap(float2 uv, float3 worldNormal, float4 worldTangent,
                        int normalTexIndex, float amount)
{
    if (normalTexIndex < 0 || amount <= 0.0 ||
        length(worldTangent.xyz) < 0.001) return normalize(worldNormal);
    
    float3 tangentNormal = textures[normalTexIndex].Sample(linearSampler, uv).xyz * 2.0 - 1.0;
    tangentNormal = BlendNormalSample(tangentNormal, amount);
    
    float3 N = normalize(worldNormal);
    float3 T = normalize(worldTangent.xyz);
    float3 B = cross(N, T) * worldTangent.w;
    float3x3 TBN = float3x3(T, B, N);
    
    return normalize(mul(tangentNormal, TBN));
}

float CalculateShadow(float3 worldPos, float3 N)
{
    float3 L = normalize(lightDir.xyz);
    float normalOffset = 0.02 * (1.0 - saturate(dot(N, L))) + 0.002;
    float3 biasedWorldPos = worldPos + N * normalOffset;

    float4 shadowPos = mul(float4(biasedWorldPos, 1.0), shadowMatrix);
    shadowPos.xyz /= shadowPos.w;
    
    // Transform to [0,1] range for UV sampling
    float2 shadowUV = shadowPos.xy * 0.5 + 0.5;
    shadowUV.y = 1.0 - shadowUV.y;

    if (shadowUV.x < 0 || shadowUV.x > 1 || shadowUV.y < 0 || shadowUV.y > 1) return 1.0;
    
    float currentDepth = shadowPos.z;
        if (currentDepth < 0.0 || currentDepth > 1.0) return 1.0;

    float receiverBias = max(0.0002, 0.0015 * (1.0 - saturate(dot(N, L))));

    // 3x3 PCF
    float shadow = 0.0;
    float2 texelSize = 1.0 / 2048.0;
    for(int x = -1; x <= 1; ++x)
    {
        for(int y = -1; y <= 1; ++y)
        {
                shadow += shadowMap.SampleCmpLevelZero(
                    shadowSampler,
                    shadowUV + float2(x, y) * texelSize,
                    currentDepth - receiverBias).r;
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
    float3 objectPos = input.objectPos;
    float3 worldNormal = normalize(input.normal);
    float3 objectOrigin = mul(world, float4(0.0, 0.0, 0.0, 1.0)).xyz;

    bool triPlanar = (triPlanarParams.x > 0.5);
    float triScale = max(triPlanarParams.y, 1e-6);
    float triSharp = max(triPlanarParams.z, 0.01);
    float triNormStrength = max(triPlanarParams.w, 0.0);

    float3 BaseColor = diffuseColor.rgb;
    float alpha = diffuseColor.a;
    if (textureIndices.x >= 0) {
        float4 diffSample = triPlanar ? SampleTriPlanar(textureIndices.x, worldPos, worldNormal, triScale, triSharp, objectOrigin, objectPos)
                                      : textures[textureIndices.x].Sample(linearSampler, uv);
        BaseColor *= BlendTextureRgb(sRGBToLinear(diffSample.rgb), textureWeight0.x);
        alpha *= BlendTextureScalar(diffSample.a, textureWeight0.x);
    }

    float alphaCutoff = extraParams.y;
    bool alphaMasked = extraParams.z > 0.5;
    bool isGrassMaterial = extraParams.w > 0.5;
    if (alphaMasked && alphaCutoff >= 0.0) {
        clip(alpha - alphaCutoff);
    }

    float roughness = saturate(surfaceParams.x);
    float metalness = saturate(surfaceParams.y);
    float transmission = saturate(transmissionParams.a) * (1.0 - metalness);
    
    // Metal/Roughness Logic: factor * texture
    // G = Roughness, B = Metalness
    if (emissiveAndPad.z >= 0) {
        float4 mrSample = triPlanar ? SampleTriPlanar(emissiveAndPad.z, worldPos, worldNormal, triScale, triSharp, objectOrigin, objectPos)
                                    : textures[emissiveAndPad.z].Sample(linearSampler, uv);

        float roughnessFactor = (emissiveAndPad.w > 0)
                                    ? max(1.0 - mrSample.g, 0.0)
                                    : mrSample.g;
        roughness *= BlendTextureScalar(roughnessFactor, textureWeight0.y);
        metalness *= BlendTextureScalar(mrSample.b, textureWeight0.y);
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
    float3 N = triPlanar ? SampleTriPlanarNormal(textureIndices.z, worldPos, worldNormal, triScale, triSharp, triNormStrength, textureWeight1.x, objectOrigin, objectPos)
                         : GetNormalFromMap(uv, worldNormal, input.tangent, textureIndices.z, textureWeight1.x);

    // Emissive with user-defined intensity
    float3 emiss = emissiveColor.rgb * extraParams.x;
    if (emissiveAndPad.x >= 0) {
        float3 e = triPlanar ? SampleTriPlanar(emissiveAndPad.x, worldPos, worldNormal, triScale, triSharp, objectOrigin, objectPos).rgb
                             : textures[emissiveAndPad.x].Sample(linearSampler, uv).rgb;
        emiss *= BlendTextureRgb(sRGBToLinear(e), textureWeight1.z);
    } 

    // Occlusion
    float ao = 1.0;
    if (emissiveAndPad.y >= 0) {
        float aoSample = triPlanar ? SampleTriPlanar(emissiveAndPad.y, worldPos, worldNormal, triScale, triSharp, objectOrigin, objectPos).r
                                   : textures[emissiveAndPad.y].Sample(linearSampler, uv).r;
        ao = BlendTextureScalar(aoSample, textureWeight1.y);
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
    float grassRootAmount = 0.0;
    float grassDirectContact = 1.0;
    float grassAmbientContact = 1.0;
    float3 grassSoilBounce = float3(0.0, 0.0, 0.0);

    if (isGrassMaterial) {
        float tip = saturate(1.0 - input.uv.y);
        grassRootAmount = saturate(pow(input.uv.y, 1.65));
        if (!triPlanar && textureIndices.x >= 0) {
            float2 emitterUv = input.emitterUv * uvTransform.xy + uvTransform.zw;
            float3 groundTint =
                BlendTextureRgb(
                    sRGBToLinear(textures[textureIndices.x].Sample(linearSampler, emitterUv).rgb),
                    textureWeight0.x);
            float groundInfluence = lerp(0.70, 0.18, tip);
            BaseColor = lerp(BaseColor, groundTint, groundInfluence);
        } else if (triPlanar && textureIndices.x >= 0) {
            float3 groundTint =
                BlendTextureRgb(
                    sRGBToLinear(SampleTriPlanar(textureIndices.x, worldPos,
                                                worldNormal, triScale,
                                                triSharp, objectOrigin,
                                                objectPos)
                                     .rgb),
                    textureWeight0.x);
            float groundInfluence = lerp(0.70, 0.18, tip);
            BaseColor = lerp(BaseColor, groundTint, groundInfluence);
        }
        float field = GrassFieldNoise(worldPos.xz * 0.82 + input.grassVariation * 5.7);
        float clump = GrassFieldNoise(worldPos.xz * 4.6 + input.grassVariation * 13.1);
        float hueMix = saturate(field * 0.85 + input.grassVariation * 0.35);
        float3 lushTint = float3(0.92, 1.08, 0.90);
        float3 dryTint = float3(1.10, 0.98, 0.72);
        float3 tint = lerp(lushTint, dryTint, hueMix * 0.38);
        float3 soilTint = lerp(float3(0.44, 0.35, 0.18),
                               float3(0.30, 0.25, 0.14),
                               saturate(field * 0.85 + clump * 0.2));
        float rootDarken = lerp(0.62, 1.08, tip);
        BaseColor *= tint * rootDarken;
        BaseColor = lerp(BaseColor, BaseColor * soilTint,
                         grassRootAmount * (0.18 + 0.18 * (1.0 - field)));
        DiffuseAlbedo = BaseColor * (1.0 - metalness) * (1.0 - transmission);
        roughness = lerp(roughness, 0.92, 0.45);
        clearcoat = 0.0;
        translucency = max(translucency, lerp(0.38, 0.72, tip));
        grassDirectContact = lerp(1.0, 0.68 + 0.10 * clump, grassRootAmount);
        grassAmbientContact = lerp(1.0, 0.46 + 0.18 * field, grassRootAmount);
        grassSoilBounce = DiffuseAlbedo * soilTint *
                          (grassRootAmount * (0.05 + 0.03 * (1.0 - field)));
    }

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
    float shadow = CalculateShadow(input.worldPos, N);
    directLight *= shadow;
    directLight *= grassDirectContact;

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

    // Raster does not have a preconvolved irradiance map yet. A single blurred
    // env lookup overestimates Prague sky energy on upward-facing diffuse
    // surfaces, so keep the fake irradiance both blurrier and much lower-energy
    // until a proper irradiance/prefilter path exists.
    const float kRasterDiffuseIrradianceScale = 0.08;
    const float kRasterDiffuseAmbientScale = 0.18;
    const float kRasterSpecularAmbientScale = 0.10;
    float2 envUV_diff = DirectionToUV(N);
    float3 irradiance = envMap.SampleLevel(linearSampler, envUV_diff, 9.0).rgb *
                        kRasterDiffuseIrradianceScale;
    float3 diffuse_ibl = kD_ibl * irradiance * (DiffuseAlbedo / PI);

    float2 envUV_spec = DirectionToUV(R);
    float3 prefilteredColor = envMap.SampleLevel(linearSampler, envUV_spec, roughness * 7.0).rgb;
    float2 envBrdf = EnvBRDFApprox(F0, roughness, NdotV);
    float3 specular_ibl = prefilteredColor * (F0 * envBrdf.x + envBrdf.y);

    float3 coat_ibl = float3(0.0, 0.0, 0.0);
    if (clearcoat > 0.001) {
        float2 envUV_spec = DirectionToUV(R);
        float3 prefilteredCoat = envMap.SampleLevel(linearSampler, envUV_spec, clearcoatRoughness * 7.0).rgb;
        float3 F0c = float3(0.04, 0.04, 0.04);
        float2 envBrdfCoat = EnvBRDFApprox(F0c, clearcoatRoughness, NdotV);
        coat_ibl = prefilteredCoat * (F0c * envBrdfCoat.x + envBrdfCoat.y);
    }

    // Raster has no true GI, so keep the environment contribution conservative
    // to avoid blowing out simple procedural-sky scenes.
    float3 envIBL = (diffuse_ibl * kRasterDiffuseAmbientScale +
                     specular_ibl * kRasterSpecularAmbientScale) * ao;
    float3 ambient = envIBL * (1.0 - clearcoat);
    if (clearcoat > 0.001) {
        float3 ambCoat = coat_ibl * (ao * kRasterSpecularAmbientScale);
        ambient += ambCoat * clearcoat;
    }

    if (translucency > 0.001) {
        float2 envUV_back = DirectionToUV(-N);
        float3 irradianceBack = envMap.SampleLevel(linearSampler, envUV_back, 9.0).rgb *
                     kRasterDiffuseIrradianceScale;
        float3 envBack = (DiffuseAlbedo * irradianceBack) * (ao * kRasterDiffuseAmbientScale);
        ambient += envBack * translucency;
    }

    float skyHemi = saturate(N.y * 0.5 + 0.5);
    float envLuma = dot(irradiance + prefilteredColor, float3(0.2126, 0.7152, 0.0722));
    float fallbackWeight = saturate(1.0 - envLuma * 2.0);
    float3 skyFill = DiffuseAlbedo * radiance * ao * (0.008 + 0.018 * skyHemi);
    ambient += skyFill * fallbackWeight;

    ambient = ambient * grassAmbientContact + grassSoilBounce;
    
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
        else if (mode == 8) o_dbg.color = float4(shadow, shadow, shadow, 1.0);
        else o_dbg.color = float4(0,0,0,1);
        return o_dbg;
    }

    PSOutput o;
    o.color = float4(color, alpha);
    o.normal = float4(N * 0.5 + 0.5, roughness);
    return o;
}
