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
    float exportRendering;
    float dxrProceduralSkyBoost;
    float iblIndirectBoost;
    float tonemapAoIntensity;
    float tonemapAoRadiusMeters;
    float tonemapAoMode;
    float triPlanarWorldRotationDegrees;
    float dxrFeatureFlags;
    float4x4 shadowMatrix;
    float4x4 viewProj;
    float4x4 invViewProj;
    float4 shadowViewRow0;
    float4 shadowViewRow1;
    float4 shadowViewRow2;
    float4 shadowProjParams0;
    float4 shadowProjParams1;
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
    int4 textureIndices;        // x=diffuse, y=opacity, z=normal, w=specularColor
    int4 emissiveAndPad;        // x=emissive, y=occlusion, z=metalRough
    float4 extraParams;         // x=emissiveIntensity, y=alphaCutoff, z=isMask, w=isGrass
    float4 coatLayerParams;     // x=coatWeight, y=coatRoughness, z=thinWalled, w=translucency
    float4 uvTransform;         // xy=uvScale, zw=uvOffset
    float4 uvRotationParams;    // x=regular UV rotation in degrees
    float4 triPlanarParams;     // x=enabled, y=scale, z=sharpness, w=normalStrength
    float4 mappingVariationParams; // x=mode, y=offsetJitter, z=randomRotation, w=colorVariation
    float4 triPlanarRotationParams; // xyz=materialRotationDegrees, w=stochasticMirror
    float4 textureWeight0;      // x=baseColor, y=packedSurface, z=metalness, w=roughnessGloss
    float4 textureWeight1;      // x=normal, y=occlusion, z=emissive, w=opacity
    int4 textureIndices2;       // x=coatNormal, y=thickness, z=parallaxDepth
    float4 textureWeight2;      // x=coatNormal, y=thickness, z=specularColor, w=parallaxDepthScale
    float4 volumeParams;        // x=thickness, y=attenuationDistance, z=alphaCutoff, w=coatIor
    float4 specularColor;       // rgb=specularColor, a=specularColorTexAmount
    float4 sheenColor;          // rgb=sheenColor
    float4 lobeParams;          // x=anisotropy, y=anisoRotation, z=sheenWeight, w=coatNormalAmount
    float4 parallaxParams;      // x=heightDepth, y=mode, z=roomDepth, w=windowAspect
    float4 parallaxTransform;   // xy=uvScale, zw=uvOffset
    float4 parallaxOptions;     // x=renderWindowBoxOnBackFace
};

cbuffer GrassDrawCB : register(b3)
{
    uint selectedGrassMaterialIndex;
    uint3 grassDrawPadding;
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

struct ShadowData
{
    float factor;
    float2 uv;
    float currentDepth;
    float rawDepth;
    float valid;
    float invalidReason;
};

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

float3 RotateTriPlanarVector(float3 v)
{
    float3 radiansXYZ = radians(triPlanarRotationParams.xyz);
    float sx, cx, sy, cy, sz, cz;
    sincos(radiansXYZ.x, sx, cx);
    sincos(radiansXYZ.y, sy, cy);
    sincos(radiansXYZ.z, sz, cz);

    v = float3(v.x, v.y * cx - v.z * sx, v.y * sx + v.z * cx);
    v = float3(v.x * cy - v.z * sy, v.y, v.x * sy + v.z * cy);
    v = float3(v.x * cz - v.y * sz, v.x * sz + v.y * cz, v.z);
    return v;
}

float3 RotateTriPlanarAxis(float3 axis)
{
    return normalize(RotateTriPlanarVector(axis));
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

float2 ComputeTriPlanarVariationOffset(float3 objectOrigin, uint primitiveId)
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
        seed = HashTriPlanarU32(seed ^ ((primitiveId + 1u) * 0x9e3779b9u));
    }
    return (float2(HashTriPlanar01(seed ^ 0x68bc21ebu),
                   HashTriPlanar01(seed ^ 0x02e5be93u)) * 2.0 - 1.0) *
           mappingVariationParams.y;
}

bool UseUvStochasticTiling()
{
    return mappingVariationParams.x > 0.5 &&
           (mappingVariationParams.y > 1.0e-4 ||
            mappingVariationParams.z > 1.0e-4 ||
            mappingVariationParams.w > 1.0e-4 ||
            triPlanarRotationParams.w > 0.5);
}

uint ComputeUvVariationBaseSeed(float3 objectOrigin, float3 worldNormal,
                                uint primitiveId)
{
    int3 quantizedOrigin = int3(round(objectOrigin * 100.0));
    uint seed = HashTriPlanarU32(
        asuint(quantizedOrigin.x) * 73856093u ^
        asuint(quantizedOrigin.y) * 19349663u ^
        asuint(quantizedOrigin.z) * 83492791u);
    uint mode = (uint)round(mappingVariationParams.x);
    if (mode >= 2u) {
        float3 an = abs(worldNormal);
        uint dominantAxis = (an.x >= an.y && an.x >= an.z) ? 0u :
                            ((an.y >= an.z) ? 1u : 2u);
        float axisValue = (dominantAxis == 0u) ? worldNormal.x :
                          ((dominantAxis == 1u) ? worldNormal.y : worldNormal.z);
        uint signSeed = axisValue >= 0.0f ? 0x85ebca6bu : 0xc2b2ae35u;
        seed = HashTriPlanarU32(seed ^
                                ((dominantAxis + 1u) * 0x9e3779b9u) ^
                                signSeed ^
                                ((primitiveId + 1u) * 0x632be59bu));
    }
    return seed;
}

void ComputeUvVariationCells(float2 uv, out int2 cell0, out int2 cell1,
                             out int2 cell2, out float3 weights)
{
    float2 baseCellF = floor(uv);
    int2 baseCell = int2(baseCellF);
    float2 tileFrac = uv - baseCellF;
    if (tileFrac.x + tileFrac.y <= 1.0f) {
        cell0 = baseCell;
        cell1 = baseCell + int2(1, 0);
        cell2 = baseCell + int2(0, 1);
        weights = float3(1.0f - tileFrac.x - tileFrac.y, tileFrac.x, tileFrac.y);
        return;
    }

    cell0 = baseCell + int2(1, 1);
    cell1 = baseCell + int2(0, 1);
    cell2 = baseCell + int2(1, 0);
    weights = float3(tileFrac.x + tileFrac.y - 1.0f,
                     1.0f - tileFrac.x,
                     1.0f - tileFrac.y);
}

float2 ComputeUvVariationOffsetForCell(int2 cell, uint baseSeed)
{
    uint cellSeed = HashTriPlanarU32(
        baseSeed ^
        (asuint(cell.x) * 0x632be59bu) ^
        (asuint(cell.y) * 0x85157af5u));
    return (float2(HashTriPlanar01(cellSeed ^ 0x68bc21ebu),
                   HashTriPlanar01(cellSeed ^ 0x02e5be93u)) * 2.0f - 1.0f) *
           mappingVariationParams.y;
}

uint ComputeUvVariationCellSeed(int2 cell, uint baseSeed)
{
    return HashTriPlanarU32(
        baseSeed ^
        (asuint(cell.x) * 0x632be59bu) ^
        (asuint(cell.y) * 0x85157af5u));
}

float2 RotateUvLocal(float2 uvLocal, float sinR, float cosR)
{
    float2 centered = uvLocal - 0.5f;
    return float2(centered.x * cosR - centered.y * sinR,
                  centered.x * sinR + centered.y * cosR) + 0.5f;
}

float2 RotateNormalLocal(float2 tangentXY, float2 mirrorSign,
                         float sinR, float cosR)
{
    tangentXY *= mirrorSign;
    return float2(tangentXY.x * cosR - tangentXY.y * sinR,
                  tangentXY.x * sinR + tangentXY.y * cosR);
}

float2 ApplyRegularUvTransform(float2 uv)
{
    float rotationDegrees = uvRotationParams.x;
    if (abs(rotationDegrees) > 1.0e-5f) {
        float sinR, cosR;
        sincos(radians(rotationDegrees), sinR, cosR);
        uv = RotateUvLocal(uv, sinR, cosR);
    }
    return uv * uvTransform.xy + uvTransform.zw;
}

float3 ApplyRegularUvNormalRotation(float3 tangentNormal)
{
    float rotationDegrees = uvRotationParams.x;
    if (abs(rotationDegrees) <= 1.0e-5f) {
        return tangentNormal;
    }
    float sinR, cosR;
    sincos(radians(rotationDegrees), sinR, cosR);
    tangentNormal.xy =
        RotateNormalLocal(tangentNormal.xy, float2(1.0f, 1.0f), sinR, cosR);
    return normalize(tangentNormal);
}

float2 TransformUvGradientForCell(float2 uvGradient, float2 mirrorSign,
                                  float sinR, float cosR)
{
    uvGradient *= mirrorSign;
    return float2(uvGradient.x * cosR - uvGradient.y * sinR,
                  uvGradient.x * sinR + uvGradient.y * cosR);
}

void ComputeUvVariationTransform(uint cellSeed, out float2 offset,
                                 out float2 mirrorSign,
                                 out float sinR, out float cosR,
                                 out float3 colorScale)
{
    offset = ComputeUvVariationOffsetForCell(int2(0, 0), cellSeed);
    mirrorSign = float2(1.0f, 1.0f);
    if (triPlanarRotationParams.w > 0.5f) {
        mirrorSign.x = HashTriPlanar01(cellSeed ^ 0x51633e2du) > 0.5f ? -1.0f : 1.0f;
        mirrorSign.y = HashTriPlanar01(cellSeed ^ 0x68f7d247u) > 0.5f ? -1.0f : 1.0f;
    }

    float rotationDegrees = HashTriPlanar01(cellSeed ^ 0x02c9277bu) *
                            mappingVariationParams.z;
    sincos(radians(rotationDegrees), sinR, cosR);

    colorScale = float3(1.0f, 1.0f, 1.0f);
    if (mappingVariationParams.w > 1.0e-4) {
        float3 tint = 1.0f +
            (float3(HashTriPlanar01(cellSeed ^ 0x1b56c4e9u),
                    HashTriPlanar01(cellSeed ^ 0x7f4a7c15u),
                    HashTriPlanar01(cellSeed ^ 0x94d049bbu)) * 2.0f - 1.0f) *
            mappingVariationParams.w;
        float tintLuma = max(dot(tint, float3(0.2126f, 0.7152f, 0.0722f)),
                             1.0e-3f);
        colorScale = max(tint / tintLuma, 0.0f);
    }
}

float2 TransformUvForCell(float2 uv, float2 offset, float2 mirrorSign,
                          float sinR, float cosR)
{
    float2 uvLocal = frac(uv);
    if (mirrorSign.x < 0.0f) uvLocal.x = 1.0f - uvLocal.x;
    if (mirrorSign.y < 0.0f) uvLocal.y = 1.0f - uvLocal.y;
    return RotateUvLocal(uvLocal, sinR, cosR) + offset;
}

float4 SampleUvTexture(int texIndex, float2 uv, float3 objectOrigin,
                       float3 worldNormal, uint primitiveId,
                       bool applyColorVariation)
{
    if (texIndex < 0) return float4(1, 1, 1, 1);
    if (!UseUvStochasticTiling()) {
        return textures[texIndex].Sample(linearSampler, uv);
    }

    uint baseSeed = ComputeUvVariationBaseSeed(objectOrigin,
                                               normalize(worldNormal),
                                               primitiveId);
    int2 cell0, cell1, cell2;
    float3 weights;
    ComputeUvVariationCells(uv, cell0, cell1, cell2, weights);

    uint seed0 = ComputeUvVariationCellSeed(cell0, baseSeed);
    uint seed1 = ComputeUvVariationCellSeed(cell1, baseSeed);
    uint seed2 = ComputeUvVariationCellSeed(cell2, baseSeed);

    float2 offset0, mirror0;
    float2 offset1, mirror1;
    float2 offset2, mirror2;
    float sin0, cos0, sin1, cos1, sin2, cos2;
    float3 color0, color1, color2;
    ComputeUvVariationTransform(seed0, offset0, mirror0, sin0, cos0, color0);
    ComputeUvVariationTransform(seed1, offset1, mirror1, sin1, cos1, color1);
    ComputeUvVariationTransform(seed2, offset2, mirror2, sin2, cos2, color2);

    float2 uvDx = ddx(uv);
    float2 uvDy = ddy(uv);
    float4 s0 = textures[texIndex].SampleGrad(
        linearSampler, TransformUvForCell(uv, offset0, mirror0, sin0, cos0),
        TransformUvGradientForCell(uvDx, mirror0, sin0, cos0),
        TransformUvGradientForCell(uvDy, mirror0, sin0, cos0));
    float4 s1 = textures[texIndex].SampleGrad(
        linearSampler, TransformUvForCell(uv, offset1, mirror1, sin1, cos1),
        TransformUvGradientForCell(uvDx, mirror1, sin1, cos1),
        TransformUvGradientForCell(uvDy, mirror1, sin1, cos1));
    float4 s2 = textures[texIndex].SampleGrad(
        linearSampler, TransformUvForCell(uv, offset2, mirror2, sin2, cos2),
        TransformUvGradientForCell(uvDx, mirror2, sin2, cos2),
        TransformUvGradientForCell(uvDy, mirror2, sin2, cos2));
    if (applyColorVariation) {
        s0.rgb *= color0;
        s1.rgb *= color1;
        s2.rgb *= color2;
    }
    return s0 * weights.x + s1 * weights.y + s2 * weights.z;
}

float3 SampleUvNormalTexture(int texIndex, float2 uv, float amount,
                             float3 objectOrigin, float3 worldNormal,
                             uint primitiveId)
{
    if (texIndex < 0 || amount <= 0.0f) return float3(0.0f, 0.0f, 1.0f);
    if (!UseUvStochasticTiling()) {
        return normalize(lerp(
            float3(0.0f, 0.0f, 1.0f),
            textures[texIndex].Sample(linearSampler, uv).xyz * 2.0f - 1.0f,
            saturate(amount)));
    }

    uint baseSeed = ComputeUvVariationBaseSeed(objectOrigin,
                                               normalize(worldNormal),
                                               primitiveId);
    int2 cell0, cell1, cell2;
    float3 weights;
    ComputeUvVariationCells(uv, cell0, cell1, cell2, weights);

    uint seed0 = ComputeUvVariationCellSeed(cell0, baseSeed);
    uint seed1 = ComputeUvVariationCellSeed(cell1, baseSeed);
    uint seed2 = ComputeUvVariationCellSeed(cell2, baseSeed);

    float2 offset0, mirror0;
    float2 offset1, mirror1;
    float2 offset2, mirror2;
    float sin0, cos0, sin1, cos1, sin2, cos2;
    float3 colorUnused0, colorUnused1, colorUnused2;
    ComputeUvVariationTransform(seed0, offset0, mirror0, sin0, cos0,
                                colorUnused0);
    ComputeUvVariationTransform(seed1, offset1, mirror1, sin1, cos1,
                                colorUnused1);
    ComputeUvVariationTransform(seed2, offset2, mirror2, sin2, cos2,
                                colorUnused2);

    float2 uvDx = ddx(uv);
    float2 uvDy = ddy(uv);
    float blendAmount = saturate(amount);
    float3 n0 = normalize(lerp(
        float3(0.0f, 0.0f, 1.0f),
        textures[texIndex].SampleGrad(
            linearSampler, TransformUvForCell(uv, offset0, mirror0, sin0, cos0),
            TransformUvGradientForCell(uvDx, mirror0, sin0, cos0),
            TransformUvGradientForCell(uvDy, mirror0, sin0, cos0)).xyz *
                2.0f - 1.0f,
        blendAmount));
    float3 n1 = normalize(lerp(
        float3(0.0f, 0.0f, 1.0f),
        textures[texIndex].SampleGrad(
            linearSampler, TransformUvForCell(uv, offset1, mirror1, sin1, cos1),
            TransformUvGradientForCell(uvDx, mirror1, sin1, cos1),
            TransformUvGradientForCell(uvDy, mirror1, sin1, cos1)).xyz *
                2.0f - 1.0f,
        blendAmount));
    float3 n2 = normalize(lerp(
        float3(0.0f, 0.0f, 1.0f),
        textures[texIndex].SampleGrad(
            linearSampler, TransformUvForCell(uv, offset2, mirror2, sin2, cos2),
            TransformUvGradientForCell(uvDx, mirror2, sin2, cos2),
            TransformUvGradientForCell(uvDy, mirror2, sin2, cos2)).xyz *
                2.0f - 1.0f,
        blendAmount));
    n0.xy = RotateNormalLocal(n0.xy, mirror0, sin0, cos0);
    n1.xy = RotateNormalLocal(n1.xy, mirror1, sin1, cos1);
    n2.xy = RotateNormalLocal(n2.xy, mirror2, sin2, cos2);
    n0 = normalize(n0);
    n1 = normalize(n1);
    n2 = normalize(n2);
    return normalize(n0 * weights.x + n1 * weights.y + n2 * weights.z);
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
                       float3 objectPos, uint primitiveId)
{
    if (texIndex < 0) return float4(1,1,1,1);
    float3 rotatedPos = RotateTriPlanarVector(worldPos);
    float3 rotatedNormal = normalize(RotateTriPlanarVector(worldNormal));
    float2 variationOffset =
        ComputeTriPlanarVariationOffset(objectOrigin, primitiveId);
    float3 w = TriPlanarWeights(rotatedNormal, sharpness);
    float4 sx = textures[texIndex].Sample(
        linearSampler,
        TriPlanarUV_X(rotatedPos, rotatedNormal, scale, variationOffset));
    float4 sy = textures[texIndex].Sample(
        linearSampler,
        TriPlanarUV_Y(rotatedPos, rotatedNormal, scale, variationOffset));
    float4 sz = textures[texIndex].Sample(
        linearSampler,
        TriPlanarUV_Z(rotatedPos, rotatedNormal, scale, variationOffset));
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
                             float3 objectPos, uint primitiveId)
{
    if (texIndex < 0 || amount <= 0.0) return normalize(worldNormal);
    float3 Nw = normalize(worldNormal);
    float3 rotatedPos = RotateTriPlanarVector(worldPos);
    float3 rotatedNormal = normalize(RotateTriPlanarVector(worldNormal));
    float2 variationOffset =
        ComputeTriPlanarVariationOffset(objectOrigin, primitiveId);
    float3 w = TriPlanarWeights(rotatedNormal, sharpness);

    float3 nx = UnpackNormal(textures[texIndex].Sample(
        linearSampler,
        TriPlanarUV_X(rotatedPos, rotatedNormal, scale, variationOffset)));
    float3 ny = UnpackNormal(textures[texIndex].Sample(
        linearSampler,
        TriPlanarUV_Y(rotatedPos, rotatedNormal, scale, variationOffset)));
    float3 nz = UnpackNormal(textures[texIndex].Sample(
        linearSampler,
        TriPlanarUV_Z(rotatedPos, rotatedNormal, scale, variationOffset)));
    nx = BlendNormalSample(nx, amount);
    ny = BlendNormalSample(ny, amount);
    nz = BlendNormalSample(nz, amount);
    nx.xy *= strength; ny.xy *= strength; nz.xy *= strength;
    nx = normalize(nx); ny = normalize(ny); nz = normalize(nz);

    float sx = (rotatedNormal.x >= 0.0) ? 1.0 : -1.0;
    float sy = (rotatedNormal.y >= 0.0) ? 1.0 : -1.0;
    float sz = (rotatedNormal.z >= 0.0) ? 1.0 : -1.0;

    float3 axisX = RotateTriPlanarAxis(float3(1,0,0));
    float3 axisY = RotateTriPlanarAxis(float3(0,1,0));
    float3 axisZ = RotateTriPlanarAxis(float3(0,0,1));

    float3 Tx = -axisZ * sx;
    float3 Bx = axisY;
    float3 Nx = axisX * sx;
    float3x3 TBNx = float3x3(Tx, Bx, Nx);

    float3 Ty = axisX;
    float3 By = -axisZ * sy;
    float3 Ny = axisY * sy;
    float3x3 TBNy = float3x3(Ty, By, Ny);

    float3 Tz = axisX * sz;
    float3 Bz = axisY;
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

float4 ComputeShadowClipPosition(float3 worldPos)
{
    float3 lightSpacePos;
    lightSpacePos.x = dot(shadowViewRow0.xyz, worldPos) + shadowViewRow0.w;
    lightSpacePos.y = dot(shadowViewRow1.xyz, worldPos) + shadowViewRow1.w;
    lightSpacePos.z = dot(shadowViewRow2.xyz, worldPos) + shadowViewRow2.w;

    return float4(
        lightSpacePos.x * shadowProjParams0.x + shadowProjParams0.w,
        lightSpacePos.y * shadowProjParams0.y + shadowProjParams1.x,
        lightSpacePos.z * shadowProjParams0.z + shadowProjParams1.y,
        1.0f);
}

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
    if ((blade.packedData & 0xFFFFu) != selectedGrassMaterialIndex)
    {
        o.position = float4(0.0, 0.0, -1.0, 1.0);
        o.worldPos = 0.0;
        o.objectPos = 0.0;
        o.normal = float3(0.0, 1.0, 0.0);
        o.tangent = float4(1.0, 0.0, 0.0, 1.0);
        o.uv = 0.0;
        o.grassVariation = 0.0;
        o.emitterUv = 0.0;
        return o;
    }
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
    o.position = ComputeShadowClipPosition(worldPos.xyz);
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
    o.position = ComputeShadowClipPosition(worldPos);
    return o;
}

// Improved microfacet BRDF helpers
static const float PI = 3.14159265359;

float DielectricF0FromIor(float ior)
{
    float safeIor = max(ior, 1.0 + 1.0e-4);
    float f0 = (safeIor - 1.0) / (safeIor + 1.0);
    return f0 * f0;
}

void BuildShadingBasis(float3 N, float4 tangent, float rotationDegrees,
                       out float3 T, out float3 B)
{
    float3 tangentDir = tangent.xyz;
    if (length(tangentDir) < 1.0e-4) {
        float3 helper = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0)
                                         : float3(0.0, 1.0, 0.0);
        tangentDir = normalize(cross(helper, N));
    } else {
        tangentDir = normalize(tangentDir - N * dot(N, tangentDir));
    }

    float handedness = tangent.w >= 0.0 ? 1.0 : -1.0;
    float3 bitangentDir = normalize(cross(N, tangentDir)) * handedness;

    float rotationRadians = radians(rotationDegrees);
    float sinR = sin(rotationRadians);
    float cosR = cos(rotationRadians);
    T = normalize(tangentDir * cosR + bitangentDir * sinR);
    B = normalize(cross(N, T)) * handedness;
}

float2 ApplyParallaxOcclusionUv(float2 uv, float3 worldPos, float3 worldNormal,
                                float4 worldTangent, int parallaxTexIndex,
                                float depthScale)
{
    if (parallaxTexIndex < 0 || depthScale <= 1.0e-5 ||
        length(worldTangent.xyz) < 1.0e-4) {
        return uv;
    }

    float3 N = normalize(worldNormal);
    float3 T, B;
    BuildShadingBasis(N, worldTangent, 0.0, T, B);
    float3 V = normalize(pos - worldPos);
    float3 viewTs = normalize(float3(dot(V, T), dot(V, B), dot(V, N)));
    if (viewTs.z <= 0.05) {
        return uv;
    }

    int layerCount = (int)round(lerp(28.0, 10.0, saturate(viewTs.z)));
    float layerDepth = 1.0 / max((float)layerCount, 1.0);
    float2 deltaUv = (viewTs.xy / max(viewTs.z, 0.08)) *
                     saturate(depthScale) / max((float)layerCount, 1.0);

    float2 currentUv = uv;
    float2 previousUv = uv;
    float currentLayerDepth = 0.0;
    float currentDepth = 1.0 - textures[parallaxTexIndex].Sample(
        linearSampler, currentUv).r;

    [loop]
    for (int step = 0; step < 32; ++step) {
        if (step >= layerCount || currentLayerDepth >= currentDepth) {
            break;
        }
        previousUv = currentUv;
        currentUv -= deltaUv;
        currentLayerDepth += layerDepth;
        currentDepth = 1.0 - textures[parallaxTexIndex].Sample(
            linearSampler, currentUv).r;
    }

    float previousLayerDepth = currentLayerDepth - layerDepth;
    float previousDepth = 1.0 - textures[parallaxTexIndex].Sample(
        linearSampler, previousUv).r;
    float afterDepth = currentDepth - currentLayerDepth;
    float beforeDepth = previousDepth - previousLayerDepth;
    float denom = afterDepth - beforeDepth;
    float weight = (abs(denom) > 1.0e-5) ? saturate(afterDepth / denom) : 0.0;
    return lerp(currentUv, previousUv, weight);
}

float WindowBoxAtlasAlpha(int alphaTexIndex, float2 uv, float fallbackAlpha)
{
    if (alphaTexIndex < 0) {
        return fallbackAlpha;
    }
    return textures[alphaTexIndex].Sample(linearSampler, uv).r;
}

float4 SampleWindowBoxParallax(int texIndex, int alphaTexIndex, float2 uv,
                               float3 worldPos,
                               float3 worldNormal, float4 worldTangent,
                               float roomDepth, float windowAspect,
                               bool renderBackFace)
{
    if (texIndex < 0 || length(worldTangent.xyz) < 1.0e-4) {
        return float4(1.0, 1.0, 1.0, 1.0);
    }

    float3 N = normalize(worldNormal);
    float4 tangent = worldTangent;
    if (renderBackFace) {
        N = -N;
        tangent.w = -tangent.w;
    }
    float3 T, B;
    BuildShadingBasis(N, tangent, 0.0, T, B);
    float3 V = normalize(pos - worldPos);
    float3 viewTs = normalize(float3(dot(V, T), dot(V, B), dot(V, N)));
    if (viewTs.z <= 0.05) {
        float4 grazingSample = textures[texIndex].Sample(linearSampler, uv);
        grazingSample.a = WindowBoxAtlasAlpha(alphaTexIndex, uv,
                                              grazingSample.a);
        return grazingSample;
    }

    float depth = max(roomDepth, 0.1);
    float aspect = max(windowAspect, 0.05);
    float3 objI = normalize(float3(-viewTs.x / aspect, -viewTs.y, -viewTs.z));
    float3 objSign = lerp(-1.0.xxx, 1.0.xxx, step(0.0.xxx, objI));
    float3 safeObjI = objSign * max(abs(objI), 1.0e-4.xxx);
    float3 objP = float3(saturate(uv), 0.5);
    float3 sections = step(0.0.xxx, safeObjI);

    float invThird = 1.0 / 3.0;
    float twoThirds = 2.0 / 3.0;
    float3 baseDepth = (objP - sections) / (-safeObjI * depth);
    float3 baseBack = (objP - sections) / (-safeObjI);
    float3 baseWidth = baseDepth * depth;

    float3 baseDepthX = baseDepth.y * safeObjI + objP + 1.0;
    float3 baseDepthY = baseDepth.x * safeObjI + objP + 1.0;
    float3 baseWidthX = baseWidth.y * safeObjI + objP + 1.0;
    float3 baseWidthY = baseWidth.x * safeObjI + objP + 1.0;

    float horizU = baseDepthY.z - 0.5;
    float vertU = baseWidthX.x - 1.0;
    float horizV = baseWidthY.y - 1.0;
    float vertV = baseDepthX.z - 0.5;

    float2 finalUv = 0.0.xx;
    float floorCeilMask =
        step(0.0, vertV) * step(0.0, 1.0 - max(vertU, 1.0 - vertU));
    float2 floorCeilUv = float2(vertU, vertV) * invThird;
    float2 ceilUv = (floorCeilUv + float2(invThird, twoThirds)) *
                    floorCeilMask * sections.y;
    float2 floorUv = floorCeilUv + float2(invThird, 0.0);
    floorUv.y = invThird - floorCeilUv.y;
    floorUv *= floorCeilMask * (1.0 - sections.y);
    finalUv += ceilUv + floorUv;

    float sideWallsMask =
        step(0.0, horizU) * step(0.0, 1.0 - max(horizV, 1.0 - horizV));
    float2 sideWallsUv = float2(horizU, horizV) * invThird;
    float2 rightUv = (sideWallsUv + float2(twoThirds, invThird)) *
                     sideWallsMask * sections.x;
    float2 leftUv = sideWallsUv + float2(0.0, invThird);
    leftUv.x = invThird - sideWallsUv.x;
    leftUv *= sideWallsMask * (1.0 - sections.x);
    finalUv += leftUv + rightUv;

    float backMask = 1.0 - max(step(0.0, horizU), step(0.0, vertV));
    float2 backUv =
        ((baseBack.z * safeObjI.xy + (objP.xy * 0.5) / depth) *
         (depth * 2.0) * invThird + float2(invThird, invThird)) * backMask;
    finalUv += backUv;

    float hasWall = step(1.0e-5, dot(abs(finalUv), 1.0.xx));
    float4 finalSample = textures[texIndex].Sample(linearSampler, finalUv);
    finalSample.a = WindowBoxAtlasAlpha(alphaTexIndex, finalUv,
                                        finalSample.a) * hasWall;

    float midDepth = clamp(0.5, 0.05, max(depth - 0.01, 0.05));
    float2 midUv = ((baseBack.z * safeObjI.xy + objP.xy / (midDepth * 2.0)) *
                    (midDepth * 2.0) * invThird);
    float midMask = step(0.0, midUv.y * 3.0 * (1.0 - midUv.y * 3.0)) *
                    step(0.0, midUv.x * (invThird - midUv.x));
    if (midUv.x > 0.01 && midUv.x < 0.331 && midUv.y > 0.01 && midUv.y < 0.331) {
        float4 midSample = textures[texIndex].Sample(linearSampler, midUv);
        midSample.a = WindowBoxAtlasAlpha(alphaTexIndex, midUv, midSample.a);
        finalSample.rgb = lerp(finalSample.rgb, midSample.rgb,
                               saturate(midSample.a * midMask));
        finalSample.a = lerp(finalSample.a, 1.0, saturate(midSample.a * midMask));
    }

    float2 curtainsUv = uv * invThird + float2(0.0, twoThirds);
    float4 curtainsSample = textures[texIndex].Sample(linearSampler, curtainsUv);
    curtainsSample.a = WindowBoxAtlasAlpha(alphaTexIndex, curtainsUv,
                                           curtainsSample.a);
    finalSample.rgb = lerp(finalSample.rgb, curtainsSample.rgb,
                           saturate(curtainsSample.a));
    finalSample.a = lerp(finalSample.a, 1.0, saturate(curtainsSample.a));
    return finalSample;
}

// GGX/Trowbridge-Reitz normal distribution with anisotropy support
float DistributionGGX(float3 N, float3 H, float roughness,
                      float anisotropy, float3 T, float3 B)
{
    float a = max(roughness * roughness, 0.001);
    float NdotH = max(dot(N, H), 0.0);

    if (abs(anisotropy) <= 1.0e-4 || length(T) <= 1.0e-4 || length(B) <= 1.0e-4) {
        float a2 = a * a;
        float NdotH2 = NdotH * NdotH;
        float denom = (NdotH2 * (a2 - 1.0) + 1.0);
        denom = PI * denom * denom;
        return a2 / max(denom, 0.0001);
    }

    float aspect = sqrt(max(1.0 - 0.85 * min(abs(anisotropy), 0.99), 0.15));
    float ax = anisotropy >= 0.0 ? a / aspect : a * aspect;
    float ay = anisotropy >= 0.0 ? a * aspect : a / aspect;
    ax = max(ax, 1.0e-3);
    ay = max(ay, 1.0e-3);

    float TdotH = dot(T, H);
    float BdotH = dot(B, H);
    float d = (TdotH * TdotH) / (ax * ax) +
              (BdotH * BdotH) / (ay * ay) +
              NdotH * NdotH;
    return 1.0 / max(PI * ax * ay * d * d, 1.0e-4);
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

float3 EvaluateSheen(float3 sheenTint, float sheenWeight, float VdotH,
                     float metalness)
{
    float weight = saturate(sheenWeight) * (1.0 - saturate(metalness));
    if (weight <= 1.0e-4) {
        return float3(0.0, 0.0, 0.0);
    }

    float sheenF = pow(saturate(1.0 - VdotH), 5.0);
    return saturate(sheenTint) * (weight * sheenF);
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
                        int normalTexIndex, float amount,
                        float3 objectOrigin, uint primitiveId)
{
    if (normalTexIndex < 0 || amount <= 0.0 ||
        length(worldTangent.xyz) < 0.001) return normalize(worldNormal);
    
    float3 tangentNormal =
        SampleUvNormalTexture(normalTexIndex, uv, amount, objectOrigin,
                              worldNormal, primitiveId);
    tangentNormal = ApplyRegularUvNormalRotation(tangentNormal);
    
    float3 N = normalize(worldNormal);
    float3 T = normalize(worldTangent.xyz);
    float3 B = cross(N, T) * worldTangent.w;
    float3x3 TBN = float3x3(T, B, N);
    
    return normalize(mul(tangentNormal, TBN));
}

ShadowData EvaluateShadow(float3 worldPos, float3 N)
{
    ShadowData result;
    result.factor = 1.0;
    result.uv = float2(0.0, 0.0);
    result.currentDepth = 0.0;
    result.rawDepth = 1.0;
    result.valid = 0.0;
    result.invalidReason = 0.0;

    float3 L = normalize(lightDir.xyz);
    float ndotl = saturate(dot(N, L));
    float normalOffset = 0.0002 * (1.0 - ndotl) + 0.00005;
    float3 biasedWorldPos = worldPos + N * normalOffset;

    float4 shadowPos = ComputeShadowClipPosition(biasedWorldPos);
    if (abs(shadowPos.w) < 1.0e-6)
    {
        result.invalidReason = 3.0;
        return result;
    }
    shadowPos.xyz /= shadowPos.w;
    
    // Transform to [0,1] range for UV sampling
    float2 shadowUV = shadowPos.xy * 0.5 + 0.5;
    shadowUV.y = 1.0 - shadowUV.y;
    result.uv = shadowUV;

    if (shadowUV.x < 0 || shadowUV.x > 1 || shadowUV.y < 0 || shadowUV.y > 1)
    {
        result.invalidReason = 1.0;
        return result;
    }
    
    float currentDepth = shadowPos.z;
    result.currentDepth = currentDepth;
    if (currentDepth < 0.0 || currentDepth > 1.0)
    {
        result.invalidReason = 2.0;
        return result;
    }

    uint shadowWidth;
    uint shadowHeight;
    shadowMap.GetDimensions(shadowWidth, shadowHeight);
    float2 texelSize = 1.0 / float2(max(shadowWidth, 1u), max(shadowHeight, 1u));
    uint2 shadowCoord = min(uint2(shadowUV * float2(shadowWidth, shadowHeight)),
                            uint2(max(shadowWidth, 1u) - 1u,
                                  max(shadowHeight, 1u) - 1u));
    result.rawDepth = shadowMap.Load(int3(shadowCoord, 0)).r;
    result.valid = 1.0;

    float depthSlope = max(abs(ddx(currentDepth)), abs(ddy(currentDepth)));
    float receiverBias = max(0.000005, depthSlope * 0.25 + 0.00002 * (1.0 - ndotl));

    // Manual 3x3 PCF to avoid driver/API comparison-sampler mismatches.
    float shadow = 0.0;
    const float compareDepth = currentDepth - receiverBias;
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            int sampleX = clamp((int)shadowCoord.x + x, 0, (int)shadowWidth - 1);
            int sampleY = clamp((int)shadowCoord.y + y, 0, (int)shadowHeight - 1);
            float sampleDepth = shadowMap.Load(int3(sampleX, sampleY, 0)).r;
            shadow += (compareDepth <= sampleDepth) ? 1.0 : 0.0;
        }
    }
    result.factor = shadow / 9.0;
    return result;
}

float CalculateShadow(float3 worldPos, float3 N)
{
    return EvaluateShadow(worldPos, N).factor;
}

struct PSOutput {
    float4 color : SV_Target0;
    float4 normal : SV_Target1;
};

PSOutput PSMainMesh(PSInputMesh input, uint primitiveId : SV_PrimitiveID)
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
    float2 uv = ApplyRegularUvTransform(input.uv);
    float3 worldPos = input.worldPos;
    float3 objectPos = input.objectPos;
    float3 worldNormal = normalize(input.normal);
    float3 objectOrigin = mul(world, float4(0.0, 0.0, 0.0, 1.0)).xyz;

    bool triPlanar = (triPlanarParams.x > 0.5);
    float triScale = max(triPlanarParams.y, 1e-6);
    float triSharp = max(triPlanarParams.z, 0.01);
    float triNormStrength = max(triPlanarParams.w, 0.0);
    const bool clayMode =
        (debugVisualizationMode > 1.5) && (debugVisualizationMode < 2.5);
    int parallaxMode = (int)round(parallaxParams.y);
    bool parallaxMapped =
        !clayMode && !triPlanar && textureIndices2.z >= 0 &&
        parallaxMode == 1 && textureWeight2.w > 1.0e-5;
    bool windowBoxMapped =
        !clayMode && !triPlanar && textureIndices2.z >= 0 &&
        parallaxMode == 2;
    if (parallaxMapped) {
        uv = ApplyParallaxOcclusionUv(uv, worldPos, worldNormal,
                                      input.tangent, textureIndices2.z,
                                      textureWeight2.w);
    }

    float3 BaseColor = diffuseColor.rgb;
    float alpha = diffuseColor.a;
    float3 windowBoxEmission = float3(0.0, 0.0, 0.0);
    if (windowBoxMapped) {
        bool windowBoxBackFace = parallaxOptions.x > 0.5;
        float2 windowBoxUv =
            (uv - 0.5.xx) * max(parallaxTransform.xy, 0.01.xx) +
            0.5.xx + parallaxTransform.zw;
        float4 wb = SampleWindowBoxParallax(textureIndices2.z, textureIndices.y,
                                            windowBoxUv, worldPos,
                                            worldNormal, input.tangent,
                                            parallaxParams.z,
                                            parallaxParams.w,
                                            windowBoxBackFace);
        BaseColor *= wb.rgb;
        alpha *= wb.a;
        if (emissiveAndPad.x >= 0) {
            float4 wbEmission = SampleWindowBoxParallax(
                emissiveAndPad.x, textureIndices.y, windowBoxUv, worldPos,
                worldNormal, input.tangent, parallaxParams.z,
                parallaxParams.w, windowBoxBackFace);
            windowBoxEmission = wbEmission.rgb;
        } else {
            windowBoxEmission = wb.rgb;
        }
    }
    if (!clayMode && textureIndices.x >= 0) {
        float4 diffSample = triPlanar ? SampleTriPlanar(textureIndices.x, worldPos, worldNormal, triScale, triSharp, objectOrigin, objectPos, primitiveId)
                                      : SampleUvTexture(textureIndices.x, uv, objectOrigin, worldNormal, primitiveId, true);
        float3 diffRgb = (parallaxMapped || windowBoxMapped) ? diffSample.rgb
                                        : sRGBToLinear(diffSample.rgb);
        BaseColor *= BlendTextureRgb(diffRgb, textureWeight0.x);
        alpha *= BlendTextureScalar(diffSample.a, textureWeight0.x);
    }
    if (!clayMode && textureIndices.y >= 0 && !windowBoxMapped) {
        float opacitySample = triPlanar ? SampleTriPlanar(textureIndices.y, worldPos, worldNormal, triScale, triSharp, objectOrigin, objectPos, primitiveId).r
                                        : SampleUvTexture(textureIndices.y, uv, objectOrigin, worldNormal, primitiveId, false).r;
        alpha *= BlendTextureScalar(opacitySample, textureWeight1.w);
    }

    float alphaCutoff = extraParams.y;
    bool alphaMasked = !clayMode && extraParams.z > 0.5;
    bool isGrassMaterial = !clayMode && extraParams.w > 0.5;
    if (clayMode) {
        BaseColor = float3(0.5, 0.5, 0.5);
        alpha = 1.0;
        triPlanar = false;
    }
    if (alphaMasked && alphaCutoff >= 0.0) {
        clip(alpha - alphaCutoff);
    }

    float roughness = clayMode ? 1.0 : saturate(surfaceParams.x);
    float metalness = clayMode ? 0.0 : saturate(surfaceParams.y);
    float transmission = clayMode ? 0.0 : saturate(transmissionParams.a) * (1.0 - metalness);
    
    // Metal/Roughness Logic: factor * texture
    // G = Roughness, B = Metalness
    if (!clayMode && emissiveAndPad.z >= 0) {
        float4 mrSample = triPlanar ? SampleTriPlanar(emissiveAndPad.z, worldPos, worldNormal, triScale, triSharp, objectOrigin, objectPos, primitiveId)
                                    : SampleUvTexture(emissiveAndPad.z, uv, objectOrigin, worldNormal, primitiveId, false);

        float roughnessFactor = (emissiveAndPad.w > 0)
                                    ? max(1.0 - mrSample.g, 0.0)
                                    : mrSample.g;
        roughness *= BlendTextureScalar(roughnessFactor, textureWeight0.y);
        metalness *= BlendTextureScalar(mrSample.b, textureWeight0.y);
    }

    // OpenPBR subset: dielectric F0 from IOR scaled by specular weight.
    float ior = max(emissiveColor.w, 1.0);
    float specularWeight = clayMode ? 0.0 : saturate(surfaceParams.z);
    float3 specularTint = saturate(specularColor.rgb);
    if (!clayMode && textureIndices.w >= 0) {
        float3 specSample = triPlanar ? SampleTriPlanar(textureIndices.w, worldPos, worldNormal, triScale, triSharp, objectOrigin, objectPos, primitiveId).rgb
                                      : SampleUvTexture(textureIndices.w, uv, objectOrigin, worldNormal, primitiveId, false).rgb;
        specularTint *= BlendTextureRgb(sRGBToLinear(specSample), textureWeight2.z);
    }
    float f0s = (ior - 1.0) / (ior + 1.0);
    f0s = f0s * f0s;
    float3 dielectricF0 = float3(f0s, f0s, f0s) * specularWeight * specularTint;
    float3 F0 = lerp(dielectricF0, BaseColor, metalness);
    float3 DiffuseAlbedo = BaseColor * (1.0 - metalness) * (1.0 - transmission);
    
    // Normal
    float3 N = clayMode
        ? worldNormal
        : (triPlanar ? SampleTriPlanarNormal(textureIndices.z, worldPos, worldNormal, triScale, triSharp, triNormStrength, textureWeight1.x, objectOrigin, objectPos, primitiveId)
                     : GetNormalFromMap(uv, worldNormal, input.tangent, textureIndices.z, textureWeight1.x, objectOrigin, primitiveId));
    if (!clayMode && coatLayerParams.x > 0.001 && textureIndices2.x >= 0 && lobeParams.w > 1.0e-4) {
        float3 coatN = triPlanar ? SampleTriPlanarNormal(textureIndices2.x, worldPos, worldNormal, triScale, triSharp, triNormStrength, lobeParams.w, objectOrigin, objectPos, primitiveId)
                                 : GetNormalFromMap(uv, worldNormal, input.tangent, textureIndices2.x, lobeParams.w, objectOrigin, primitiveId);
        N = normalize(lerp(N, coatN, saturate(coatLayerParams.x)));
    }

    // Emissive with user-defined intensity
    float3 emiss = clayMode ? float3(0.0, 0.0, 0.0)
                            : emissiveColor.rgb * extraParams.x;
    if (!clayMode && windowBoxMapped) {
        emiss += windowBoxEmission * max(extraParams.x, 0.0);
    }
    if (!clayMode && emissiveAndPad.x >= 0 && !windowBoxMapped) {
        float3 e = triPlanar ? SampleTriPlanar(emissiveAndPad.x, worldPos, worldNormal, triScale, triSharp, objectOrigin, objectPos, primitiveId).rgb
                             : SampleUvTexture(emissiveAndPad.x, uv, objectOrigin, worldNormal, primitiveId, true).rgb;
        emiss *= BlendTextureRgb((parallaxMapped || windowBoxMapped) ? e : sRGBToLinear(e),
                                 textureWeight1.z);
    } 

    // Occlusion
    float ao = 1.0;
    if (!clayMode && emissiveAndPad.y >= 0) {
        float aoSample = triPlanar ? SampleTriPlanar(emissiveAndPad.y, worldPos, worldNormal, triScale, triSharp, objectOrigin, objectPos, primitiveId).r
                                   : SampleUvTexture(emissiveAndPad.y, uv, objectOrigin, worldNormal, primitiveId, false).r;
        ao = BlendTextureScalar(aoSample, textureWeight1.y);
    }

    // Lighting
    float3 V = normalize(pos - input.worldPos);
    // Two-sided shading guard for assets with inconsistent face orientation.
    if (dot(N, V) < 0.0) N = -N;
    float3 L = normalize(lightDir.xyz);
    if (length(lightDir.xyz) < 0.001) L = float3(0, 1, 0);
    float3 H = normalize(V + L);

    // Use a numerical floor only; material roughness should remain artist-linear.
    roughness = max(roughness, 0.001);

    float clearcoat = clayMode ? 0.0 : saturate(coatLayerParams.x);
    float clearcoatRoughness = clayMode ? 1.0 : max(coatLayerParams.y, 0.001);
    float translucency = clayMode ? 0.0 : saturate(coatLayerParams.w);
    float grassRootAmount = 0.0;
    float grassDirectContact = 1.0;
    float grassAmbientContact = 1.0;
    float3 grassSoilBounce = float3(0.0, 0.0, 0.0);

    if (isGrassMaterial) {
        float tip = saturate(1.0 - input.uv.y);
        grassRootAmount = saturate(pow(input.uv.y, 1.65));
        if (!triPlanar && textureIndices.x >= 0) {
            float2 emitterUv = ApplyRegularUvTransform(input.emitterUv);
            float3 groundTint =
                BlendTextureRgb(
                    sRGBToLinear(
                        SampleUvTexture(textureIndices.x, emitterUv,
                                        objectOrigin, worldNormal, primitiveId,
                                        true)
                            .rgb),
                    textureWeight0.x);
            float groundInfluence = lerp(0.70, 0.18, tip);
            BaseColor = lerp(BaseColor, groundTint, groundInfluence);
        } else if (triPlanar && textureIndices.x >= 0) {
            float3 groundTint =
                BlendTextureRgb(
                    sRGBToLinear(SampleTriPlanar(textureIndices.x, worldPos,
                                                worldNormal, triScale,
                                                triSharp, objectOrigin,
                                                objectPos, primitiveId)
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

    float anisotropy = clamp(lobeParams.x, -1.0, 1.0);
    float3 T = float3(0.0, 0.0, 0.0);
    float3 B = float3(0.0, 0.0, 0.0);
    BuildShadingBasis(N, input.tangent, lobeParams.y, T, B);

    float NDF = DistributionGGX(N, H, roughness, anisotropy, T, B);
    float G = GeometrySmith(N, V, L, roughness);
    float3 F = FresnelSchlick(VdotH, F0);
    float3 sheenBrdf = EvaluateSheen(sheenColor.rgb, lobeParams.z, VdotH, metalness);

    float3 numerator = NDF * G * F;
    float denominator = 4.0 * NdotV * NdotL + 0.0001;
    float3 spec = numerator / denominator;

    // Clearcoat (secondary GGX lobe)
    float3 coatSpec = float3(0.0, 0.0, 0.0);
    if (clearcoat > 0.001) {
        float coatF0 = DielectricF0FromIor(volumeParams.w);
        float3 F0c = float3(coatF0, coatF0, coatF0);
        float NDFc = DistributionGGX(N, H, clearcoatRoughness, 0.0, T, B);
        float Gc = GeometrySmith(N, V, L, clearcoatRoughness);
        float3 Fc = FresnelSchlick(VdotH, F0c);
        float3 numc = NDFc * Gc * Fc;
        float denc = 4.0 * NdotV * NdotL + 0.0001;
        coatSpec = numc / denc;
    }
    
    float3 diffuseTerm = (DiffuseAlbedo / PI) * (1.0 - F);

    float3 radiance = lightColor.rgb * lightColor.w;
    
    float3 baseDirect = (diffuseTerm + spec + sheenBrdf) * radiance * NdotL;
    float3 coatDirect = coatSpec * radiance * NdotL;
    float3 directLight = baseDirect * (1.0 - clearcoat) + coatDirect * clearcoat;

    // Modulate direct light by shadow
    ShadowData shadowData = EvaluateShadow(input.worldPos, N);
    float shadow = shadowData.factor;
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

    // Raster does not have a true convolved irradiance map yet, but the Prague
    // sky texture is authored in physical cd/m2. Sample a high mip as a stable
    // sky-fill approximation and keep the energy in the same ballpark as the
    // physical-camera exposure path.
    const float kRasterDiffuseIrradianceScale = 0.95;
    const float kRasterDiffuseAmbientScale = 1.10;
    const float kRasterSpecularAmbientScale = 0.32;
    float rasterSkyBoost = max(dxrProceduralSkyBoost, 0.0);
    float rasterIndirectBoost = max(iblIndirectBoost, 3.0);
    float2 envUV_diff = DirectionToUV(N);
    float3 irradiance = envMap.SampleLevel(linearSampler, envUV_diff, 9.0).rgb *
                        (kRasterDiffuseIrradianceScale * rasterSkyBoost);
    float3 diffuse_ibl = kD_ibl * irradiance * (DiffuseAlbedo / PI);

    float2 envUV_spec = DirectionToUV(R);
    float3 prefilteredColor = envMap.SampleLevel(linearSampler, envUV_spec, roughness * 7.0).rgb *
                              rasterSkyBoost;
    float2 envBrdf = EnvBRDFApprox(F0, roughness, NdotV);
    float3 specular_ibl = prefilteredColor * (F0 * envBrdf.x + envBrdf.y);

    float3 coat_ibl = float3(0.0, 0.0, 0.0);
    if (clearcoat > 0.001) {
        float2 envUV_spec = DirectionToUV(R);
        float3 prefilteredCoat = envMap.SampleLevel(linearSampler, envUV_spec, clearcoatRoughness * 7.0).rgb *
                                 rasterSkyBoost;
        float coatF0 = DielectricF0FromIor(volumeParams.w);
        float3 F0c = float3(coatF0, coatF0, coatF0);
        float2 envBrdfCoat = EnvBRDFApprox(F0c, clearcoatRoughness, NdotV);
        coat_ibl = prefilteredCoat * (F0c * envBrdfCoat.x + envBrdfCoat.y);
    }

    // Raster has no true GI, so keep the environment contribution conservative
    // to avoid blowing out simple procedural-sky scenes.
    float3 envIBL = (diffuse_ibl * kRasterDiffuseAmbientScale +
                     specular_ibl * kRasterSpecularAmbientScale) * (ao * rasterIndirectBoost);
    float3 ambient = envIBL * (1.0 - clearcoat);
    if (clearcoat > 0.001) {
        float3 ambCoat = coat_ibl * (ao * rasterIndirectBoost * kRasterSpecularAmbientScale);
        ambient += ambCoat * clearcoat;
    }

    if (translucency > 0.001) {
        float2 envUV_back = DirectionToUV(-N);
        float3 irradianceBack = envMap.SampleLevel(linearSampler, envUV_back, 9.0).rgb *
                     (kRasterDiffuseIrradianceScale * rasterSkyBoost);
        float3 envBack = (DiffuseAlbedo * irradianceBack) * (ao * rasterIndirectBoost * kRasterDiffuseAmbientScale);
        ambient += envBack * translucency;
    }

    float sheenIblWeight = saturate(lobeParams.z) * (1.0 - metalness);
    if (sheenIblWeight > 1.0e-4) {
        float2 envUV_sheen = DirectionToUV(reflect(-V, N));
        float3 sheenEnv = envMap.SampleLevel(linearSampler, envUV_sheen,
                                             roughness * 6.0 + 2.0).rgb * rasterSkyBoost;
        float grazing = pow(saturate(1.0 - NdotV), 4.0);
        ambient += sheenEnv * saturate(sheenColor.rgb) *
                   (sheenIblWeight * grazing * 0.06 * ao * rasterIndirectBoost);
    }

    float skyHemi = saturate(N.y * 0.5 + 0.5);
    float envLuma = dot(irradiance + prefilteredColor, float3(0.2126, 0.7152, 0.0722));
    float fallbackWeight = saturate(1.0 - envLuma * 2.0);
    float3 skyFill = DiffuseAlbedo * radiance * ao * rasterIndirectBoost * (0.012 + 0.028 * skyHemi);
    ambient += skyFill * fallbackWeight;

    ambient = ambient * grassAmbientContact + grassSoilBounce;
    
    float3 color = directLight + ambient + emiss;
    
    // Exposure handling moved to tonemapping pass
    // color *= intensity;
    
    // DEBUG PASS
    int mode = (int)debugMode;
    if (mode > 0) {
        PSOutput o_dbg;
        // Raster debug views are shown through the HDR tonemap path, so boost
        // them back into a visible range instead of letting exposure crush
        // ordinary 0..1 diagnostic colors to near-black.
        float debugExposureComp = 1.0 / max(intensity, 0.02);
        o_dbg.normal = float4(N * 0.5 + 0.5, 1.0);
        if (mode == 1) o_dbg.color = float4(BaseColor * debugExposureComp, 1.0);
        else if (mode == 2) o_dbg.color = float4((N * 0.5 + 0.5) * debugExposureComp, 1.0);
        else if (mode == 3) o_dbg.color = float4(emiss * debugExposureComp, 1.0);
        else if (mode == 4) o_dbg.color = float4((1.0 - roughness).xxx * debugExposureComp, 1.0);
        else if (mode == 5) o_dbg.color = float4(F0 * debugExposureComp, 1.0);
        else if (mode == 6) o_dbg.color = float4(metalness.xxx * debugExposureComp, 1.0);
        else if (mode == 7) o_dbg.color = float4(ao.xxx * debugExposureComp, 1.0);
        else if (mode == 8) o_dbg.color = float4(float3(1.0 - shadow, shadow, 0.0) * debugExposureComp, 1.0);
        else if (mode == 22) o_dbg.color = float4(float3(1.0 - shadow, shadow, 0.0) * debugExposureComp, 1.0);
        else if (mode == 23) o_dbg.color = (shadowData.valid > 0.5)
            ? float4(float3(shadowData.uv.x,
                            shadowData.uv.y,
                            0.2 + 0.8 * saturate(shadowData.currentDepth)) * debugExposureComp, 1.0)
            : (shadowData.invalidReason < 1.5
                ? float4(float3(1.0, 0.0, 0.0) * debugExposureComp, 1.0)
                : (shadowData.invalidReason < 2.5
                    ? float4(float3(0.0, 0.0, 1.0) * debugExposureComp, 1.0)
                    : float4(float3(1.0, 1.0, 0.0) * debugExposureComp, 1.0)));
        else if (mode == 24) {
            float diff = saturate(0.5 + (shadowData.rawDepth - shadowData.currentDepth) * 200.0);
            o_dbg.color = (shadowData.valid > 0.5)
                ? float4(float3(0.2 + 0.8 * shadowData.rawDepth,
                                0.2 + 0.8 * saturate(shadowData.currentDepth),
                                diff) * debugExposureComp, 1.0)
                : (shadowData.invalidReason < 1.5
                    ? float4(float3(1.0, 0.0, 0.0) * debugExposureComp, 1.0)
                    : (shadowData.invalidReason < 2.5
                        ? float4(float3(0.0, 0.0, 1.0) * debugExposureComp, 1.0)
                        : float4(float3(1.0, 1.0, 0.0) * debugExposureComp, 1.0)));
        }
        else o_dbg.color = float4(0,0,0,1);
        return o_dbg;
    }

    PSOutput o;
    o.color = float4(color, alpha);
    o.normal = float4(N * 0.5 + 0.5, roughness);
    return o;
}
