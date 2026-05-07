// shaders/raytracing/hit.hlsl
// Closest hit shader with full PBR lighting

#include "common.hlsli"
#include "../clouds.hlsl"

// BRDF helpers: using D_GGX, V_SmithCorrelated, F_Schlick from brdf_lib.hlsl
// (included via path_tracer_core.hlsl before this file)

float3 TriPlanarWeights(float3 n, float sharpness)
{
    float3 an = abs(n);
    an = pow(max(an, 0.0), max(sharpness, 0.01));
    float s = an.x + an.y + an.z;
    return (s > 1e-5) ? (an / s) : float3(0.3333, 0.3333, 0.3333);
}

float HitHash12(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float HitValueNoise2D(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    float a = HitHash12(i);
    float b = HitHash12(i + float2(1.0, 0.0));
    float c = HitHash12(i + float2(0.0, 1.0));
    float d = HitHash12(i + float2(1.0, 1.0));
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
        v += HitValueNoise2D(q) * w;
        n += w;
        q = q * 2.13 + 11.7;
        w *= 0.5;
    }
    return (n > 1e-5) ? (v / n) : 0.0;
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

float3 RotateTriPlanarVector(float3 v, float4 rotationParams)
{
    float3 radiansXYZ = radians(rotationParams.xyz);
    float sx, cx, sy, cy, sz, cz;
    sincos(radiansXYZ.x, sx, cx);
    sincos(radiansXYZ.y, sy, cy);
    sincos(radiansXYZ.z, sz, cz);

    v = float3(v.x, v.y * cx - v.z * sx, v.y * sx + v.z * cx);
    v = float3(v.x * cy - v.z * sy, v.y, v.x * sy + v.z * cy);
    v = float3(v.x * cz - v.y * sz, v.x * sz + v.y * cz, v.z);
    return v;
}

float3 RotateTriPlanarAxis(float3 axis, float4 rotationParams)
{
    return normalize(RotateTriPlanarVector(axis, rotationParams));
}

uint ComputeTriPlanarVariationSeed(float4 variationParams, float3 objectOrigin,
                                   uint primitiveId)
{
    uint mode = (uint)round(variationParams.x);
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

float2 ComputeTriPlanarVariationOffset(float4 variationParams,
                                       float3 objectOrigin,
                                       uint primitiveId)
{
    if (variationParams.x < 0.5 || variationParams.y <= 1.0e-4) {
        return float2(0.0, 0.0);
    }

    const uint seed = ComputeTriPlanarVariationSeed(variationParams,
                                                    objectOrigin,
                                                    primitiveId);
    return (float2(HashTriPlanar01(seed ^ 0x68bc21ebu),
                   HashTriPlanar01(seed ^ 0x02e5be93u)) * 2.0 - 1.0) *
           variationParams.y;
}

bool UseUvStochasticTiling(float4 variationParams, float4 rotationParams)
{
    return variationParams.x > 0.5 &&
           (variationParams.y > 1.0e-4 ||
            variationParams.z > 1.0e-4 ||
            variationParams.w > 1.0e-4 ||
            rotationParams.w > 0.5);
}

uint ComputeUvVariationBaseSeed(float4 variationParams, float3 objectOrigin,
                                float3 worldNormal)
{
    int3 quantizedOrigin = int3(round(objectOrigin * 100.0));
    uint seed = HashTriPlanarU32(
        asuint(quantizedOrigin.x) * 73856093u ^
        asuint(quantizedOrigin.y) * 19349663u ^
        asuint(quantizedOrigin.z) * 83492791u);
    uint mode = (uint)round(variationParams.x);
    if (mode >= 2u) {
        float3 an = abs(worldNormal);
        uint dominantAxis = (an.x >= an.y && an.x >= an.z) ? 0u :
                            ((an.y >= an.z) ? 1u : 2u);
        float axisValue = (dominantAxis == 0u) ? worldNormal.x :
                          ((dominantAxis == 1u) ? worldNormal.y : worldNormal.z);
        uint signSeed = axisValue >= 0.0f ? 0x85ebca6bu : 0xc2b2ae35u;
        seed = HashTriPlanarU32(seed ^ ((dominantAxis + 1u) * 0x9e3779b9u) ^ signSeed);
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

float2 ComputeUvVariationOffsetForCell(int2 cell, uint baseSeed,
                                       float jitter)
{
    uint cellSeed = HashTriPlanarU32(
        baseSeed ^
        (asuint(cell.x) * 0x632be59bu) ^
        (asuint(cell.y) * 0x85157af5u));
    return (float2(HashTriPlanar01(cellSeed ^ 0x68bc21ebu),
                   HashTriPlanar01(cellSeed ^ 0x02e5be93u)) * 2.0f - 1.0f) *
           jitter;
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

void ComputeUvVariationTransform(uint cellSeed, float4 variationParams,
                                 float4 rotationParams, out float2 offset,
                                 out float2 mirrorSign,
                                 out float sinR, out float cosR,
                                 out float3 colorScale)
{
    offset = ComputeUvVariationOffsetForCell(int2(0, 0), cellSeed,
                                             variationParams.y);
    mirrorSign = float2(1.0f, 1.0f);
    if (rotationParams.w > 0.5f) {
        mirrorSign.x = HashTriPlanar01(cellSeed ^ 0x51633e2du) > 0.5f ? -1.0f : 1.0f;
        mirrorSign.y = HashTriPlanar01(cellSeed ^ 0x68f7d247u) > 0.5f ? -1.0f : 1.0f;
    }

    float rotationDegrees = HashTriPlanar01(cellSeed ^ 0x02c9277bu) *
                            variationParams.z;
    sincos(radians(rotationDegrees), sinR, cosR);

    colorScale = float3(1.0f, 1.0f, 1.0f);
    if (variationParams.w > 1.0e-4) {
        float3 tint = 1.0f +
            (float3(HashTriPlanar01(cellSeed ^ 0x1b56c4e9u),
                    HashTriPlanar01(cellSeed ^ 0x7f4a7c15u),
                    HashTriPlanar01(cellSeed ^ 0x94d049bbu)) * 2.0f - 1.0f) *
            variationParams.w;
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
                       float3 worldNormal, float4 variationParams,
                       float4 rotationParams, float lod,
                       bool applyColorVariation)
{
    if (texIndex < 0) return float4(1, 1, 1, 1);
    if (!UseUvStochasticTiling(variationParams, rotationParams)) {
        return textures[texIndex].SampleLevel(linearSampler, uv, lod);
    }

    uint baseSeed = ComputeUvVariationBaseSeed(variationParams, objectOrigin,
                                               normalize(worldNormal));
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
    ComputeUvVariationTransform(seed0, variationParams, rotationParams,
                                offset0, mirror0, sin0, cos0, color0);
    ComputeUvVariationTransform(seed1, variationParams, rotationParams,
                                offset1, mirror1, sin1, cos1, color1);
    ComputeUvVariationTransform(seed2, variationParams, rotationParams,
                                offset2, mirror2, sin2, cos2, color2);

    float4 s0 = textures[texIndex].SampleLevel(
        linearSampler,
        TransformUvForCell(uv, offset0, mirror0, sin0, cos0),
        lod);
    float4 s1 = textures[texIndex].SampleLevel(
        linearSampler,
        TransformUvForCell(uv, offset1, mirror1, sin1, cos1),
        lod);
    float4 s2 = textures[texIndex].SampleLevel(
        linearSampler,
        TransformUvForCell(uv, offset2, mirror2, sin2, cos2),
        lod);
    if (applyColorVariation) {
        s0.rgb *= color0;
        s1.rgb *= color1;
        s2.rgb *= color2;
    }
    return s0 * weights.x + s1 * weights.y + s2 * weights.z;
}

float3 SampleUvNormalTexture(int texIndex, float2 uv, float amount,
                             float3 objectOrigin, float3 worldNormal,
                             float4 variationParams,
                             float4 rotationParams, float lod)
{
    if (texIndex < 0 || amount <= 0.0f) return float3(0.0f, 0.0f, 1.0f);
    if (!UseUvStochasticTiling(variationParams, rotationParams)) {
        return normalize(lerp(
            float3(0.0f, 0.0f, 1.0f),
            textures[texIndex].SampleLevel(linearSampler, uv, lod).xyz *
                2.0f - 1.0f,
            saturate(amount)));
    }

    uint baseSeed = ComputeUvVariationBaseSeed(variationParams, objectOrigin,
                                               normalize(worldNormal));
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
    ComputeUvVariationTransform(seed0, variationParams, rotationParams,
                                offset0, mirror0, sin0, cos0, colorUnused0);
    ComputeUvVariationTransform(seed1, variationParams, rotationParams,
                                offset1, mirror1, sin1, cos1, colorUnused1);
    ComputeUvVariationTransform(seed2, variationParams, rotationParams,
                                offset2, mirror2, sin2, cos2, colorUnused2);

    float blendAmount = saturate(amount);
    float3 n0 = normalize(lerp(
        float3(0.0f, 0.0f, 1.0f),
        textures[texIndex].SampleLevel(
            linearSampler, TransformUvForCell(uv, offset0, mirror0, sin0, cos0),
            lod).xyz * 2.0f - 1.0f,
        blendAmount));
    float3 n1 = normalize(lerp(
        float3(0.0f, 0.0f, 1.0f),
        textures[texIndex].SampleLevel(
            linearSampler, TransformUvForCell(uv, offset1, mirror1, sin1, cos1),
            lod).xyz * 2.0f - 1.0f,
        blendAmount));
    float3 n2 = normalize(lerp(
        float3(0.0f, 0.0f, 1.0f),
        textures[texIndex].SampleLevel(
            linearSampler, TransformUvForCell(uv, offset2, mirror2, sin2, cos2),
            lod).xyz * 2.0f - 1.0f,
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

float CalculateTextureLod(uint rayType, float3 worldPos)
{
    if (rayType == RAY_TYPE_PRIMARY) return 0.0;
    float pathDistance = max(length(worldPos - camPos), 1e-3);
    float lod = log2(pathDistance * 0.02) + 0.35;
    return clamp(lod, 0.0, 10.0);
}

float4 SampleTriPlanar(int texIndex, float3 worldPos, float3 worldNormal,
                       float scale, float sharpness,
                       float4 variationParams, float4 rotationParams,
                       float3 objectOrigin,
                       uint primitiveId, float lod, bool dominantAxisOnly)
{
    if (texIndex < 0) return float4(1,1,1,1);
    float3 rotatedPos = RotateTriPlanarVector(worldPos, rotationParams);
    float3 rotatedNormal = normalize(RotateTriPlanarVector(worldNormal, rotationParams));
    float2 variationOffset = ComputeTriPlanarVariationOffset(
        variationParams, objectOrigin, primitiveId);
    if (dominantAxisOnly) {
        float3 an = abs(rotatedNormal);
        if (an.x >= an.y && an.x >= an.z) return textures[texIndex].SampleLevel(linearSampler, TriPlanarUV_X(rotatedPos, rotatedNormal, scale, variationOffset), lod);
        if (an.y >= an.z) return textures[texIndex].SampleLevel(linearSampler, TriPlanarUV_Y(rotatedPos, rotatedNormal, scale, variationOffset), lod);
        return textures[texIndex].SampleLevel(linearSampler, TriPlanarUV_Z(rotatedPos, rotatedNormal, scale, variationOffset), lod);
    }
    float3 w = TriPlanarWeights(rotatedNormal, sharpness);
    float4 sx = textures[texIndex].SampleLevel(linearSampler, TriPlanarUV_X(rotatedPos, rotatedNormal, scale, variationOffset), lod);
    float4 sy = textures[texIndex].SampleLevel(linearSampler, TriPlanarUV_Y(rotatedPos, rotatedNormal, scale, variationOffset), lod);
    float4 sz = textures[texIndex].SampleLevel(linearSampler, TriPlanarUV_Z(rotatedPos, rotatedNormal, scale, variationOffset), lod);
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

float3 ApplyVolumeAttenuation(float3 transmissionColor, float thickness,
                              float attenuationDistance)
{
    if (thickness <= 1.0e-5 || attenuationDistance <= 1.0e-5) {
        return saturate(transmissionColor);
    }

    float d = max(thickness / attenuationDistance, 0.0);
    return saturate(pow(max(transmissionColor, float3(1.0e-4, 1.0e-4, 1.0e-4)), d));
}

float DielectricF0FromIorLocal(float ior)
{
    float safeIor = max(ior, 1.0 + 1.0e-4);
    float f0 = (safeIor - 1.0) / (safeIor + 1.0);
    return f0 * f0;
}

float3 EvaluateSheenBrdf(float3 sheenTint, float sheenWeight, float VdotH,
                         float metalness)
{
    float weight = saturate(sheenWeight) * (1.0 - saturate(metalness));
    if (weight <= 1.0e-4) {
        return float3(0.0, 0.0, 0.0);
    }

    float sheenF = pow(saturate(1.0 - VdotH), 5.0);
    return saturate(sheenTint) * (weight * sheenF);
}

float3 SampleTriPlanarNormal(int texIndex, float3 worldPos, float3 worldNormal,
                             float scale, float sharpness, float strength,
                             float amount, float4 variationParams,
                             float4 rotationParams,
                             float3 objectOrigin, uint primitiveId,
                             float lod, bool dominantAxisOnly)
{
    if (texIndex < 0 || amount <= 0.0) return normalize(worldNormal);
    float3 Nw = normalize(worldNormal);
    float3 rotatedPos = RotateTriPlanarVector(worldPos, rotationParams);
    float3 rotatedNormal = normalize(RotateTriPlanarVector(worldNormal, rotationParams));
    float2 variationOffset = ComputeTriPlanarVariationOffset(
        variationParams, objectOrigin, primitiveId);
    if (dominantAxisOnly) {
        float sx = (rotatedNormal.x >= 0.0) ? 1.0 : -1.0;
        float sy = (rotatedNormal.y >= 0.0) ? 1.0 : -1.0;
        float sz = (rotatedNormal.z >= 0.0) ? 1.0 : -1.0;

        float3 axisX = RotateTriPlanarAxis(float3(1,0,0), rotationParams);
        float3 axisY = RotateTriPlanarAxis(float3(0,1,0), rotationParams);
        float3 axisZ = RotateTriPlanarAxis(float3(0,0,1), rotationParams);

        float3 an = abs(rotatedNormal);
        float3 axisNormal;
        float3x3 axisTbn;
        if (an.x >= an.y && an.x >= an.z) {
            axisNormal = UnpackNormal(textures[texIndex].SampleLevel(
                linearSampler,
                TriPlanarUV_X(rotatedPos, rotatedNormal, scale,
                               variationOffset),
                lod));
            axisTbn = float3x3(-axisZ * sx, axisY, axisX * sx);
        } else if (an.y >= an.z) {
            axisNormal = UnpackNormal(textures[texIndex].SampleLevel(
                linearSampler,
                TriPlanarUV_Y(rotatedPos, rotatedNormal, scale,
                               variationOffset),
                lod));
            axisTbn = float3x3(axisX, -axisZ * sy, axisY * sy);
        } else {
            axisNormal = UnpackNormal(textures[texIndex].SampleLevel(
                linearSampler,
                TriPlanarUV_Z(rotatedPos, rotatedNormal, scale,
                               variationOffset),
                lod));
            axisTbn = float3x3(axisX * sz, axisY, axisZ * sz);
        }
        axisNormal = BlendNormalSample(axisNormal, amount);
        axisNormal.xy *= strength;
        return normalize(mul(normalize(axisNormal), axisTbn));
    }
    float3 w = dominantAxisOnly ? float3(0,0,0) : TriPlanarWeights(rotatedNormal, sharpness);

    float3 nx = UnpackNormal(textures[texIndex].SampleLevel(linearSampler, TriPlanarUV_X(rotatedPos, rotatedNormal, scale, variationOffset), lod));
    float3 ny = UnpackNormal(textures[texIndex].SampleLevel(linearSampler, TriPlanarUV_Y(rotatedPos, rotatedNormal, scale, variationOffset), lod));
    float3 nz = UnpackNormal(textures[texIndex].SampleLevel(linearSampler, TriPlanarUV_Z(rotatedPos, rotatedNormal, scale, variationOffset), lod));
    nx = BlendNormalSample(nx, amount);
    ny = BlendNormalSample(ny, amount);
    nz = BlendNormalSample(nz, amount);
    nx.xy *= strength; ny.xy *= strength; nz.xy *= strength;
    nx = normalize(nx); ny = normalize(ny); nz = normalize(nz);

    float sx = (rotatedNormal.x >= 0.0) ? 1.0 : -1.0;
    float sy = (rotatedNormal.y >= 0.0) ? 1.0 : -1.0;
    float sz = (rotatedNormal.z >= 0.0) ? 1.0 : -1.0;

    float3 axisX = RotateTriPlanarAxis(float3(1,0,0), rotationParams);
    float3 axisY = RotateTriPlanarAxis(float3(0,1,0), rotationParams);
    float3 axisZ = RotateTriPlanarAxis(float3(0,0,1), rotationParams);

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

    if (dominantAxisOnly) {
        float3 an = abs(rotatedNormal);
        if (an.x >= an.y && an.x >= an.z) return wx;
        if (an.y >= an.z) return wy;
        return wz;
    }

    return normalize(wx * w.x + wy * w.y + wz * w.z);
}

// Extract normal from normal map and transform to world space
float3 GetNormalFromMap(float2 uv, float3 worldNormal, float4 worldTangent,
                        int normalTexIndex, float amount, float lod,
                        float4 variationParams, float4 rotationParams,
                        float3 objectOrigin)
{
    if (normalTexIndex < 0 || amount <= 0.0 ||
        dot(worldTangent.xyz, worldTangent.xyz) < 1e-6) return normalize(worldNormal);
    
    float3 tangentNormal =
        SampleUvNormalTexture(normalTexIndex, uv, amount, objectOrigin,
                              worldNormal, variationParams,
                              rotationParams, lod);
    
    float3 N = normalize(worldNormal);
    float3 T = normalize(worldTangent.xyz);
    float3 B = cross(N, T) * worldTangent.w;
    float3x3 TBN = float3x3(T, B, N);
    
    return normalize(mul(tangentNormal, TBN));
}

void ClosestHitImpl(inout RayPayload payload,
                    in BuiltInTriangleIntersectionAttributes attr,
                    bool wavefrontMinimal)
{
    uint rayType = UnpackPayloadRayType(payload.packedIorType);

    if (rayType == RAY_TYPE_SHADOW) {
        payload.t = RayTCurrent();
        return;
    }

    // Access mesh and material for this instance
    uint instanceIdx = InstanceIndex();
    uint meshIdx = InstanceID();
    MeshData mesh = meshData[meshIdx];
    uint matIdx = (uint)max(0, mesh.materialIndex);
    if (grassTlasStartIndex != 0xFFFFFFFFu && instanceIdx >= grassTlasStartIndex) {
        uint grassBladeIndex = instanceIdx - grassTlasStartIndex;
        matIdx = grassBlades[grassBladeIndex].packedData & 0xFFFFu;
    }
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
    int texOpacity = UnpackTextureIndexHigh(mat.packedTextures.z);
    int texSpecular = UnpackTextureIndexLow(mat.packedTextures.w);
    int texThickness = UnpackTextureIndexHigh(mat.packedTextures.w);
    int texCoatNormal = UnpackTextureIndexLow(matExtra.extraPackedTextures.x);

    float4 arch0 = matExtra.coatLayerParams;
    float4 uvXf = matExtra.uvTransform;
    float4 triP = matExtra.triPlanarParams;
    float4 mappingVariation = matExtra.mappingVariationParams;
    float4 triRotation = matExtra.triPlanarRotationParams;
    float4 texWeight0 = matExtra.textureWeight0;
    float4 texWeight1 = matExtra.textureWeight1;
    float4 volumeParams = matExtra.volumeParams;
    float4 specularColorParams = matExtra.specularColor;
    float4 sheenColorParams = matExtra.sheenColor;
    float4 lobeParams = matExtra.lobeParams;
    float3 transmissionColor = saturate(matExtra.transmissionColor.rgb);
    float emissiveIntensity = max(0.0, matExtra.shadingParams.x);
    float specularWeight = saturate(matExtra.shadingParams.y);
    float alphaCutoff = matExtra.shadingParams.z;
    bool isGrassMaterial = matExtra.shadingParams.w > 0.5;

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
    float3 objectOrigin = mul(ObjectToWorld3x4(), float4(0.0, 0.0, 0.0, 1.0));
    
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
    float4 samplingVariation =
        ShouldSimplifySecondaryMaterial(rayType)
            ? float4(0.0, 0.0, 0.0, 0.0)
            : mappingVariation;
    bool dominantTriPlanar = triPlanar && (rayType != RAY_TYPE_PRIMARY);
    const bool clayMode =
        (SHADER_DEBUG_VIS_MODE > 1.5) && (SHADER_DEBUG_VIS_MODE < 2.5);
    if (clayMode) {
        texDiff = -1;
        texNorm = -1;
        texMR = -1;
        texOcc = -1;
        texEmis = -1;
        texOpacity = -1;
        texSpecular = -1;
        texThickness = -1;
        texCoatNormal = -1;
        triPlanar = false;
        dominantTriPlanar = false;
        samplingVariation = float4(0.0, 0.0, 0.0, 0.0);
        matFlags = 0u;
        alphaCutoff = 0.5;
        emissiveIntensity = 0.0;
        specularWeight = 0.0;
        isGrassMaterial = false;
    }

    // Sample textures
    float3 BaseColor = diffColor.rgb;
    float opacity = diffColor.a;
    int mode = (int)SHADER_DEBUG_MODE;
    if (texDiff >= 0) {
        float4 diffSample = triPlanar ? SampleTriPlanar(texDiff, P, worldNormal, triScale, triSharp, samplingVariation, triRotation, objectOrigin, primIndex, textureLod, dominantTriPlanar)
                                      : SampleUvTexture(texDiff, uv, objectOrigin, worldNormal, samplingVariation, triRotation, textureLod, true);
        BaseColor *= BlendTextureRgb(sRGBToLinear(diffSample.rgb), texWeight0.x);
        opacity *= BlendTextureScalar(diffSample.a, texWeight0.x);
        SHADER_COUNTER_ADD(SHADER_COUNTER_TEXTURE_SAMPLES, 1);
    }
    if (texOpacity >= 0) {
        float opacitySample = triPlanar ? SampleTriPlanar(texOpacity, P, worldNormal, triScale, triSharp, samplingVariation, triRotation, objectOrigin, primIndex, textureLod, dominantTriPlanar).r
                                        : SampleUvTexture(texOpacity, uv, objectOrigin, worldNormal, samplingVariation, triRotation, textureLod, false).r;
        opacity *= BlendTextureScalar(opacitySample, texWeight1.w);
        SHADER_COUNTER_ADD(SHADER_COUNTER_TEXTURE_SAMPLES, 1);
    }
    
    float metalness = saturate(pbr.x);
    float roughnessFloor = 0.001;
    float roughness = max(saturate(pbr.y), roughnessFloor);
    
    // Metal/Roughness Logic: factor * texture
    if (texMR >= 0) {
        float4 mrSample = triPlanar ? SampleTriPlanar(texMR, P, worldNormal, triScale, triSharp, samplingVariation, triRotation, objectOrigin, primIndex, textureLod, dominantTriPlanar)
                                    : SampleUvTexture(texMR, uv, objectOrigin, worldNormal, samplingVariation, triRotation, textureLod, false);
        float roughnessFactor = ((matFlags & MATERIAL_FLAG_INVERT_ROUGHNESS) != 0)
                                    ? max(1.0 - mrSample.g, 0.0)
                                    : mrSample.g;
        roughness *= BlendTextureScalar(roughnessFactor, texWeight0.y);
        metalness *= BlendTextureScalar(mrSample.b, texWeight0.y);
        SHADER_COUNTER_ADD(SHADER_COUNTER_TEXTURE_SAMPLES, 1);
    }

    // Use a numerical floor only; material roughness should remain artist-linear.
    roughness = max(roughness, roughnessFloor);
    
    // Attenuate diffuse by transmission (refraction) for dielectrics.
    // This removes the "tint" or "solid" look from glass.
    float transmission = saturate(pbr.z) * (1.0 - metalness);
    if (alphaCutoff < 0.0 && opacity < 0.999) {
        transmission = max(transmission, 1.0 - saturate(opacity));
    }
    if (alphaCutoff < 0.0 &&
        ((matFlags & (MATERIAL_FLAG_GLASS | MATERIAL_FLAG_THIN_WALLED)) != 0) &&
        arch0.z > 0.5 &&
        transmission > 1.0e-5) {
        transmission = 1.0;
    }
    float3 DiffuseAlbedo = BaseColor * (1.0 - metalness) * (1.0 - transmission);
    if (alphaCutoff < 0.0) {
        DiffuseAlbedo *= saturate(opacity);
    }
    float clearcoat = saturate(arch0.x);
    float clearcoatRoughness = max(arch0.y, roughnessFloor);
    float grassRootAmount = 0.0;
    float grassDirectContact = 1.0;
    float grassAmbientContact = 1.0;
    float3 grassSoilBounce = float3(0.0, 0.0, 0.0);

    if (clayMode) {
        BaseColor = float3(0.5, 0.5, 0.5);
        opacity = 1.0;
        metalness = 0.0;
        roughness = 1.0;
        transmission = 0.0;
        DiffuseAlbedo = BaseColor;
        clearcoat = 0.0;
        clearcoatRoughness = 1.0;
        transmissionColor = float3(1.0, 1.0, 1.0);
    }

    if (isGrassMaterial) {
        if (!triPlanar && texDiff >= 0 && grassTlasStartIndex != 0xFFFFFFFFu &&
            instanceIdx >= grassTlasStartIndex) {
            uint grassBladeIndex = instanceIdx - grassTlasStartIndex;
            float2 emitterUv =
                grassBlades[grassBladeIndex].emitterUv * uvXf.xy + uvXf.zw;
            float3 groundTint =
                BlendTextureRgb(
                    sRGBToLinear(SampleUvTexture(texDiff, emitterUv,
                                                objectOrigin, worldNormal,
                                                samplingVariation, triRotation,
                                                textureLod, true).rgb),
                    texWeight0.x);
            float groundInfluence = lerp(0.70, 0.18, saturate(1.0 - uv.y));
            BaseColor = lerp(BaseColor, groundTint, groundInfluence);
        } else if (triPlanar && texDiff >= 0) {
            float3 groundTint =
                BlendTextureRgb(
                    sRGBToLinear(SampleTriPlanar(texDiff, P, worldNormal,
                                                triScale, triSharp,
                                                samplingVariation, triRotation,
                                                objectOrigin, primIndex,
                                                textureLod,
                                                dominantTriPlanar)
                                     .rgb),
                    texWeight0.x);
            float groundInfluence = lerp(0.70, 0.18, saturate(1.0 - uv.y));
            BaseColor = lerp(BaseColor, groundTint, groundInfluence);
        }
        float field = GrassFieldNoise(P.xz * 0.82 + (float)matIdx * 0.37);
        float tip = saturate(1.0 - uv.y);
        grassRootAmount = saturate(pow(uv.y, 1.65));
        float clump = GrassFieldNoise(P.xz * 4.6 + (float)matIdx * 1.31);
        float3 lushTint = float3(0.92, 1.08, 0.90);
        float3 dryTint = float3(1.10, 0.98, 0.72);
        float3 tint = lerp(lushTint, dryTint, saturate(field * 0.38));
        float3 soilTint = lerp(float3(0.44, 0.35, 0.18),
                               float3(0.30, 0.25, 0.14),
                               saturate(field * 0.85 + clump * 0.2));
        BaseColor *= tint * lerp(0.62, 1.08, tip);
        BaseColor = lerp(BaseColor, BaseColor * soilTint,
                         grassRootAmount * (0.18 + 0.18 * (1.0 - field)));
        DiffuseAlbedo = BaseColor * (1.0 - metalness) * (1.0 - transmission);
        roughness = lerp(roughness, 0.92, 0.45);
        clearcoat = 0.0;
        grassDirectContact = lerp(1.0, 0.68 + 0.10 * clump, grassRootAmount);
        grassAmbientContact = lerp(1.0, 0.46 + 0.18 * field, grassRootAmount);
        grassSoilBounce = DiffuseAlbedo * soilTint *
                          (grassRootAmount * (0.05 + 0.03 * (1.0 - field)));
    }

    // Standard PBR Model (dielectric F0 from IOR)
    float ior = max(emisColor.w, 1.0);
    float3 specularColor = saturate(specularColorParams.rgb);
    if (texSpecular >= 0) {
        float3 specSample = triPlanar ? SampleTriPlanar(texSpecular, P, worldNormal, triScale, triSharp, samplingVariation, triRotation, objectOrigin, primIndex, textureLod, dominantTriPlanar).rgb
                                      : SampleUvTexture(texSpecular, uv, objectOrigin, worldNormal, samplingVariation, triRotation, textureLod, false).rgb;
        specularColor *= BlendTextureRgb(sRGBToLinear(specSample), specularColorParams.a);
        SHADER_COUNTER_ADD(SHADER_COUNTER_TEXTURE_SAMPLES, 1);
    }
    float f0s = (ior - 1.0) / (ior + 1.0);
    f0s = f0s * f0s;
    float3 dielectricF0 = float3(f0s * specularWeight,
                                 f0s * specularWeight,
                                 f0s * specularWeight) * specularColor;
    float3 F0 = lerp(dielectricF0, BaseColor, metalness);
    
    // Normal mapping
    float3 N = triPlanar ? SampleTriPlanarNormal(texNorm, P, worldNormal, triScale, triSharp, triNormStrength, texWeight1.x, samplingVariation, triRotation, objectOrigin, primIndex, textureLod, dominantTriPlanar)
                         : GetNormalFromMap(uv, worldNormal, worldTangent, texNorm, texWeight1.x, textureLod, samplingVariation, triRotation, objectOrigin);
    if (clearcoat > 0.001 && texCoatNormal >= 0 && lobeParams.x > 1.0e-4) {
        float3 coatN = triPlanar ? SampleTriPlanarNormal(texCoatNormal, P, worldNormal, triScale, triSharp, triNormStrength, lobeParams.x, samplingVariation, triRotation, objectOrigin, primIndex, textureLod, dominantTriPlanar)
                                 : GetNormalFromMap(uv, worldNormal, worldTangent, texCoatNormal, lobeParams.x, textureLod, samplingVariation, triRotation, objectOrigin);
        N = normalize(lerp(N, coatN, saturate(clearcoat)));
    }
    // Two-sided shading guard for reverse-oriented faces.
    float3 viewDirTwoSided = normalize(-WorldRayDirection());
    if (dot(N, viewDirTwoSided) < 0.0) N = -N;
    
    // Ambient occlusion (only needed for GI_EVAL which computes Lo)
    float ao = 1.0;
    if (texOcc >= 0 && rayType == RAY_TYPE_GI_EVAL) {
        float aoSample = triPlanar ? SampleTriPlanar(texOcc, P, worldNormal, triScale, triSharp, samplingVariation, triRotation, objectOrigin, primIndex, textureLod, dominantTriPlanar).r
                                   : SampleUvTexture(texOcc, uv, objectOrigin, worldNormal, samplingVariation, triRotation, textureLod, false).r;
        ao = BlendTextureScalar(aoSample, texWeight1.y);
        SHADER_COUNTER_ADD(SHADER_COUNTER_TEXTURE_SAMPLES, 1);
    }
    ao *= grassAmbientContact;
    
    // Emissive with a conservative default boost.
    const float baseEmissiveBoost = 5.0f;
    float3 emissive = emisColor.rgb * (baseEmissiveBoost * emissiveIntensity);
    if (texEmis >= 0) {
        float3 e = triPlanar ? SampleTriPlanar(texEmis, P, worldNormal, triScale, triSharp, samplingVariation, triRotation, objectOrigin, primIndex, textureLod, dominantTriPlanar).rgb
                             : SampleUvTexture(texEmis, uv, objectOrigin, worldNormal, samplingVariation, triRotation, textureLod, true).rgb;
        emissive *= BlendTextureRgb(sRGBToLinear(e), texWeight1.z);
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
    float translucency = clayMode ? 0.0 : saturate(arch0.w);
    float thickness = clayMode ? 0.0 : max(volumeParams.x, 0.0);
    if (texThickness >= 0) {
        float thicknessSample = triPlanar ? SampleTriPlanar(texThickness, P, worldNormal, triScale, triSharp, samplingVariation, triRotation, objectOrigin, primIndex, textureLod, dominantTriPlanar).r
                                          : SampleUvTexture(texThickness, uv, objectOrigin, worldNormal, samplingVariation, triRotation, textureLod, false).r;
        thickness *= BlendTextureScalar(thicknessSample, volumeParams.z);
        SHADER_COUNTER_ADD(SHADER_COUNTER_TEXTURE_SAMPLES, 1);
    }
    float3 effectiveTransmissionColor =
        ApplyVolumeAttenuation(transmissionColor, thickness, volumeParams.y);
    if (isGrassMaterial) {
        translucency = max(translucency, lerp(0.38, 0.72, saturate(1.0 - uv.y)));
    }

    if (wavefrontMinimal) {
        float3 color = emissive;
        PayloadSetColor(payload, color);
        payload.t = RayTCurrent();
        payload.packedNormal = PackNormalOctahedron(N);
        payload.packedAlbedo = PackPayloadAlbedoCoat(BaseColor, clearcoat);
        PayloadSetCoatRoughness(payload, clearcoatRoughness);
        bool thinWalled = !clayMode &&
                          (((matFlags & MATERIAL_FLAG_THIN_WALLED) != 0) ||
                           (arch0.z > 0.5));
        payload.packedSurface =
            PackPayloadSurface(roughness, metalness, transmission, translucency);
        payload.packedIorType =
            PackPayloadIorType(emisColor.w, rayType, thinWalled, specularWeight);
        payload.packedTransmission =
            PackPayloadTransmissionColor(effectiveTransmissionColor);
        payload.packedSpecular =
            PackPayloadSpecularColorThickness(specularColor, thickness);
        return;
    }
    
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
            shadowPayload.packedColor1 = 0u;
            PayloadSetColor(shadowPayload, float3(0.0, 0.0, 0.0));
            shadowPayload.packedNormal = PackNormalOctahedron(float3(0.0, 1.0, 0.0));
            shadowPayload.packedAlbedo = PackPayloadAlbedo(float3(0.0, 0.0, 0.0));
            shadowPayload.packedSurface = PackPayloadSurface(1.0, 0.0, 0.0, 0.0);
            shadowPayload.packedIorType = PackPayloadIorType(1.0, RAY_TYPE_SHADOW, false, 1.0);
            shadowPayload.packedTransmission = PackPayloadTransmissionColor(float3(1.0, 1.0, 1.0));
            shadowPayload.packedSpecular = PackPayloadSpecularColor(float3(1.0, 1.0, 1.0));
            TraceRay(g_accel, RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xFF, 0, 0, 0, shadowRay, shadowPayload);

            if (shadowPayload.t < 0.0) { // Miss = not occluded
                float3 radiance = lightColor.rgb * lightColor.w;
                if (cloudRenderingEnabled > 0.5f) {
                    radiance *= CloudSunTransmittance(shadowRay.Origin, shadowRay.Direction);
                }
                float3 V = normalize(-WorldRayDirection());
                float3 H = normalize(V + L);
                float NdotV = saturate(dot(N, V));
                float NdotH = saturate(dot(N, H));
                float VdotH = saturate(dot(V, H));
                
                float3 F = F_Schlick(VdotH, F0);
                float D = D_GGX(NdotH, roughness);
                float V_vis = V_SmithCorrelated(NdotV, NdotL, roughness);
                float3 sheenBrdf =
                    EvaluateSheenBrdf(sheenColorParams.rgb, lobeParams.w,
                                      VdotH, metalness);

                float3 SpecularBRDF = D * V_vis * F;
                float3 DiffuseBRDF = (DiffuseAlbedo / PI) * (1.0 - F);
                float3 BaseBRDF = DiffuseBRDF + SpecularBRDF + sheenBrdf;
                float3 CoatBRDF = float3(0.0, 0.0, 0.0);
                if (clearcoat > 0.001) {
                    float coatF0 = DielectricF0FromIorLocal(volumeParams.w);
                    float3 F0c = float3(coatF0, coatF0, coatF0);
                    float3 Fc = F_Schlick(VdotH, F0c);
                    float Dc = D_GGX(NdotH, clearcoatRoughness);
                    float Vc = V_SmithCorrelated(NdotV, NdotL, clearcoatRoughness);
                    CoatBRDF = Dc * Vc * Fc;
                }

                Lo = ((BaseBRDF * (1.0 - clearcoat)) + CoatBRDF * clearcoat) *
                     radiance * NdotL;
                Lo *= grassDirectContact;
            }
        }

        // Back-face diffuse translucency (not shadow-tested, intentional for thin geometry)
        if (translucency > 0.001) {
            float NdotL_back = saturate(dot(-N, normalize(lightDir.xyz)));
            if (NdotL_back > 0.0) {
                float3 backRadiance = lightColor.rgb * lightColor.w;
                if (cloudRenderingEnabled > 0.5f) {
                    backRadiance *= CloudSunTransmittance(P - N * 0.002, normalize(lightDir.xyz));
                }
                Lo += (DiffuseAlbedo / PI) * backRadiance * NdotL_back * translucency;
            }
        }
    }
    
    // Apply AO to diffuse and ambient lighting
    Lo *= ao;
    Lo += grassSoilBounce * ao;
    
    // Keep payload color compact and purpose-specific:
    // - regular path rays carry emissive only (direct/indirect handled in raygen)
    // - GI evaluation rays carry local diffuse+emissive estimate
    float3 color = emissive;
    if (rayType == RAY_TYPE_GI_EVAL) {
        color = Lo + emissive;
    }
    
    // In PT mode, we skip tone mapping here and do it in RayGen after accumulation
    PayloadSetColor(payload, color);
    payload.t = RayTCurrent();
    payload.packedNormal = PackNormalOctahedron(N);
    payload.packedAlbedo = PackPayloadAlbedoCoat(BaseColor, clearcoat);
    PayloadSetCoatRoughness(payload, clearcoatRoughness);
    bool thinWalled = !clayMode &&
                      (((matFlags & MATERIAL_FLAG_THIN_WALLED) != 0) ||
                       (arch0.z > 0.5));
    payload.packedSurface = PackPayloadSurface(roughness, metalness, transmission, translucency);
    payload.packedIorType = PackPayloadIorType(emisColor.w, rayType, thinWalled, specularWeight);
    payload.packedTransmission = PackPayloadTransmissionColor(effectiveTransmissionColor);
    payload.packedSpecular = PackPayloadSpecularColorThickness(specularColor, thickness);
}

[shader("closesthit")]
void ClosestHit(inout RayPayload payload,
                in BuiltInTriangleIntersectionAttributes attr)
{
    ClosestHitImpl(payload, attr, false);
}

[shader("closesthit")]
void WavefrontClosestHit(inout RayPayload payload,
                         in BuiltInTriangleIntersectionAttributes attr)
{
    ClosestHitImpl(payload, attr, true);
}

[shader("anyhit")]
void AnyHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    uint rayType = UnpackPayloadRayType(payload.packedIorType);
    uint instanceIdx = InstanceIndex();
    uint meshIdx = InstanceID();
    MeshData mesh = meshData[meshIdx];
    uint matIdx = (uint)max(0, mesh.materialIndex);
    if (grassTlasStartIndex != 0xFFFFFFFFu && instanceIdx >= grassTlasStartIndex) {
        uint grassBladeIndex = instanceIdx - grassTlasStartIndex;
        matIdx = grassBlades[grassBladeIndex].packedData & 0xFFFFu;
    }
    MaterialData mat = materials[matIdx];
    MaterialExtraData matExtra = materialExtras[matIdx];
    uint matFlags = asuint(mat.pbrParams_flags.w);
    bool alphaTested = (matFlags & MATERIAL_FLAG_ALPHA_TESTED) != 0;
    float alphaCutoff = matExtra.shadingParams.z;

    if ((SHADER_DEBUG_VIS_MODE > 1.5) && (SHADER_DEBUG_VIS_MODE < 2.5)) {
        return;
    }

    if (alphaTested) {
        uint primIndex = PrimitiveIndex();
        uint baseIndex = primIndex * 3;
        uint i0 = indices[mesh.ibIndex].Load(baseIndex);
        uint i1 = indices[mesh.ibIndex].Load(baseIndex + 1);
        uint i2 = indices[mesh.ibIndex].Load(baseIndex + 2);

        float2 bary2 = attr.barycentrics;
        float3 bary = float3(1.0 - bary2.x - bary2.y, bary2.x, bary2.y);
        float2 uv0 = vertices[mesh.vbIndex][i0].uv;
        float2 uv1 = vertices[mesh.vbIndex][i1].uv;
        float2 uv2 = vertices[mesh.vbIndex][i2].uv;
        float2 uv = uv0 * bary.x + uv1 * bary.y + uv2 * bary.z;
        if ((matFlags & MATERIAL_FLAG_UV_TRANSFORM) != 0) {
            uv = uv * matExtra.uvTransform.xy + matExtra.uvTransform.zw;
        }

        float alpha = mat.baseColor_opacity.a;
        int texDiff = UnpackTextureIndexLow(mat.packedTextures.x);
        if (texDiff >= 0) {
            float3 n0 = vertices[mesh.vbIndex][i0].normal;
            float3 n1 = vertices[mesh.vbIndex][i1].normal;
            float3 n2 = vertices[mesh.vbIndex][i2].normal;
            float3 localNormal = normalize(n0 * bary.x + n1 * bary.y + n2 * bary.z);
            float3 worldNormal = normalize(mul(localNormal, (float3x3)WorldToObject3x4()));
            float3 P = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
            float3 objectOrigin = mul(ObjectToWorld3x4(), float4(0.0, 0.0, 0.0, 1.0));
            bool triPlanar = ((matFlags & MATERIAL_FLAG_TRI_PLANAR) != 0) && (matExtra.triPlanarParams.x > 0.5);
            float textureLod = CalculateTextureLod(rayType, P);
            bool dominantTriPlanar = triPlanar && (rayType != RAY_TYPE_PRIMARY);
            float alphaSample = triPlanar
                ? SampleTriPlanar(texDiff, P, worldNormal,
                                  max(matExtra.triPlanarParams.y, 1e-6),
                                  max(matExtra.triPlanarParams.z, 0.01),
                                  matExtra.mappingVariationParams,
                                  matExtra.triPlanarRotationParams,
                                  objectOrigin,
                                  primIndex,
                                  textureLod, dominantTriPlanar).a
                : SampleUvTexture(texDiff, uv, objectOrigin, worldNormal,
                                  matExtra.mappingVariationParams,
                                  matExtra.triPlanarRotationParams,
                                  textureLod, true).a;
            alpha *= BlendTextureScalar(alphaSample, matExtra.textureWeight0.x);
        }
        int texOpacity = UnpackTextureIndexHigh(mat.packedTextures.z);
        if (texOpacity >= 0) {
            float3 n0 = vertices[mesh.vbIndex][i0].normal;
            float3 n1 = vertices[mesh.vbIndex][i1].normal;
            float3 n2 = vertices[mesh.vbIndex][i2].normal;
            float3 localNormal = normalize(n0 * bary.x + n1 * bary.y + n2 * bary.z);
            float3 worldNormal = normalize(mul(localNormal, (float3x3)WorldToObject3x4()));
            float3 P = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
            float3 objectOrigin = mul(ObjectToWorld3x4(), float4(0.0, 0.0, 0.0, 1.0));
            bool triPlanar = ((matFlags & MATERIAL_FLAG_TRI_PLANAR) != 0) && (matExtra.triPlanarParams.x > 0.5);
            float textureLod = CalculateTextureLod(rayType, P);
            bool dominantTriPlanar = triPlanar && (rayType != RAY_TYPE_PRIMARY);
            float opacitySample = triPlanar
                ? SampleTriPlanar(texOpacity, P, worldNormal,
                                  max(matExtra.triPlanarParams.y, 1e-6),
                                  max(matExtra.triPlanarParams.z, 0.01),
                                  matExtra.mappingVariationParams,
                                  matExtra.triPlanarRotationParams,
                                  objectOrigin,
                                  primIndex,
                                  textureLod, dominantTriPlanar).r
                : SampleUvTexture(texOpacity, uv, objectOrigin, worldNormal,
                                  matExtra.mappingVariationParams,
                                  matExtra.triPlanarRotationParams,
                                  textureLod, false).r;
            alpha *= BlendTextureScalar(opacitySample, matExtra.textureWeight1.w);
        }

        if ((alphaCutoff >= 0.0 && alpha < alphaCutoff) ||
            (alphaCutoff < 0.0 && alpha <= 1.0e-3)) {
            IgnoreHit();
        }
    }

    // For shadow or diffuse (GI visibility) rays hitting glass or thin-walled
    // materials, we want to let light through.  Smooth BLEND glass still sets
    // the alpha-tested runtime flag so any-hit can sample texture alpha, but
    // only MASK/cutout materials should block this visibility shortcut.
    if (rayType == RAY_TYPE_SHADOW || rayType == RAY_TYPE_DIFFUSE || rayType == RAY_TYPE_GI_EVAL) {
        const bool transmissiveVisibility =
            ((matFlags & (MATERIAL_FLAG_GLASS | MATERIAL_FLAG_THIN_WALLED | MATERIAL_FLAG_TRANSLUCENT)) != 0) &&
            alphaCutoff < 0.0;
        if (transmissiveVisibility) {
            IgnoreHit();
        }
    }
}
