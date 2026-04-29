#include "raytracing/common.hlsli"
#include "raytracing/wavefront_transport.hlsli"
#include "restir_lib.hlsl"

cbuffer WavefrontResolveConstants : register(b1)
{
    uint outputWidth;
    uint outputHeight;
    uint activeCount;
    uint reservedFlags;
};

static const float2 kInvalidMvec = float2(-1e6, -1e6);
static const uint kWavefrontSecondaryQueueCounter = WAVEFRONT_QUEUE_PATH_B;
static const uint kWavefrontShadowQueueCounter = WAVEFRONT_QUEUE_SHADOW;
static const float kWavefrontRayBias = 0.002f;

RWTexture2D<float4> g_reservoir0 : register(u2);
RWTexture2D<float4> g_reservoir1 : register(u3);
RWTexture2D<float4> g_gi_reservoir_a0 : register(u4);
RWTexture2D<float4> g_gi_reservoir_a1 : register(u5);
RWTexture2D<float4> g_gi_reservoir_a2 : register(u6);
RWTexture2D<float4> g_gi_reservoir_b0 : register(u7);
RWTexture2D<float4> g_gi_reservoir_b1 : register(u8);
RWTexture2D<float4> g_gi_reservoir_b2 : register(u9);

inline uint2 WavefrontPixelCoord(uint pixelIndex)
{
    return uint2(pixelIndex % outputWidth, pixelIndex / outputWidth);
}

inline bool WavefrontReservoirFlip()
{
    return (((uint)globalFrameCount) & 1u) == 1u;
}

inline void StoreWavefrontDiReservoir(uint2 pixel, Reservoir reservoir)
{
    float4 packedReservoir = pack_reservoir(reservoir);
    if (WavefrontReservoirFlip()) {
        g_reservoir1[pixel] = packedReservoir;
    } else {
        g_reservoir0[pixel] = packedReservoir;
    }
}

inline Reservoir LoadWavefrontDiReservoir(uint2 pixel)
{
    return unpack_reservoir(WavefrontReservoirFlip()
                                ? g_reservoir1[pixel]
                                : g_reservoir0[pixel]);
}

inline void ClearWavefrontGiReservoir(uint2 pixel)
{
    const float4 zero4 = float4(0.0, 0.0, 0.0, 0.0);
    if (WavefrontReservoirFlip()) {
        g_gi_reservoir_b0[pixel] = zero4;
        g_gi_reservoir_b1[pixel] = zero4;
        g_gi_reservoir_b2[pixel] = zero4;
    } else {
        g_gi_reservoir_a0[pixel] = zero4;
        g_gi_reservoir_a1[pixel] = zero4;
        g_gi_reservoir_a2[pixel] = zero4;
    }
}

inline void StoreWavefrontGiReservoir(uint2 pixel, GI_Reservoir reservoir)
{
    float4 out0 = float4(reservoir.hitPos, 0.0);
    float4 out1 = float4(reservoir.radiance, 0.0);
    float4 out2 = float4(reservoir.w_sum, asfloat(reservoir.M),
                         reservoir.W, 0.0);
    if (WavefrontReservoirFlip()) {
        g_gi_reservoir_a0[pixel] = out0;
        g_gi_reservoir_a1[pixel] = out1;
        g_gi_reservoir_a2[pixel] = out2;
    } else {
        g_gi_reservoir_b0[pixel] = out0;
        g_gi_reservoir_b1[pixel] = out1;
        g_gi_reservoir_b2[pixel] = out2;
    }
}

inline float WavefrontEvaluateReservoirTarget(WavefrontHitRecord record,
                                              float3 worldNormal,
                                              float3 hitPos,
                                              WavefrontLightSample lightSample)
{
    float3 directWeight = ComputeWavefrontDirectLightingWeight(
        record, worldNormal, hitPos, lightSample.direction);
    return length(max(lightSample.radiance * directWeight, 0.0));
}

inline float2 ComputeWavefrontSkyMotion(float3 rayDir, float2 currScreen)
{
    float2 motion = float2(0.0, 0.0);
    if (prevValid > 0.5) {
        float3 forwardPrev = normalize(prevForward);
        float3 rightPrev = normalize(cross(forwardPrev, prevUp));
        float3 upPrev = normalize(cross(rightPrev, forwardPrev));
        float fInvPrev = tan(radians(prevFov) * 0.5);
        float vxPrev = dot(rayDir, rightPrev);
        float vyPrev = dot(rayDir, upPrev);
        float vzPrev = dot(rayDir, forwardPrev);
        if (vzPrev > 0.001) {
            float ndcXPrev = vxPrev / (vzPrev * prevAspect * fInvPrev);
            float ndcYPrev = -vyPrev / (vzPrev * fInvPrev);
            float2 prevScreen =
                (float2(ndcXPrev, ndcYPrev) * 0.5 + 0.5) * float2(outputWidth, outputHeight);
            float2 screenMin = float2(0.0, 0.0);
            float2 screenMax = float2(outputWidth, outputHeight);
            motion = (any(prevScreen < screenMin) || any(prevScreen > screenMax))
                         ? kInvalidMvec
                         : (prevScreen - currScreen);
        }
    }
    return motion;
}

inline float2 ComputeWavefrontSurfaceMotion(float3 hitPos, float2 currScreen)
{
    float2 motion = kInvalidMvec;
    if (prevValid > 0.5) {
        float3 forwardPrev = normalize(prevForward);
        float3 rightPrev = normalize(cross(forwardPrev, prevUp));
        float3 upPrev = normalize(cross(rightPrev, forwardPrev));
        float fInvPrev = tan(radians(prevFov) * 0.5);
        float3 relPrev = hitPos - prevPos;
        float vxPrev = dot(relPrev, rightPrev);
        float vyPrev = dot(relPrev, upPrev);
        float vzPrev = dot(relPrev, forwardPrev);
        if (vzPrev > 0.001) {
            float ndcXPrev = vxPrev / (vzPrev * prevAspect * fInvPrev);
            float ndcYPrev = -vyPrev / (vzPrev * fInvPrev);
            float2 prevScreen =
                (float2(ndcXPrev, ndcYPrev) * 0.5 + 0.5) * float2(outputWidth, outputHeight);
            float2 screenMin = float2(0.0, 0.0);
            float2 screenMax = float2(outputWidth, outputHeight);
            motion = (any(prevScreen < screenMin) || any(prevScreen > screenMax))
                         ? kInvalidMvec
                         : (prevScreen - currScreen);
        }
    }
    return motion;
}

inline void ReserveWavefrontQueueDimensions(out uint pathQueueCapacity,
                                            out uint pathQueueStride,
                                            out uint shadowQueueCapacity,
                                            out uint shadowQueueStride)
{
    g_wavefrontPathQueueB.GetDimensions(pathQueueCapacity, pathQueueStride);
    g_wavefrontShadowQueue.GetDimensions(shadowQueueCapacity, shadowQueueStride);
}

inline float3 SafeNormalize(float3 value, float3 fallback)
{
    float lenSq = dot(value, value);
    return (lenSq > 1.0e-8) ? value * rsqrt(lenSq) : fallback;
}

inline float3 BuildDiffuseContinuation(float3 normal, inout RNG rng)
{
    float3 localSample = sample_hemisphere_cosine(next_float2(rng));
    return SafeNormalize(align_to_normal(localSample, normal), normal);
}

inline bool BuildSpecularContinuation(float3 rayDir, float3 normal,
                                      float roughness, inout RNG rng,
                                      out float3 continuationDir)
{
    float3 halfVector = SampleGGX(next_float2(rng), normal,
                                  max(roughness, 0.001));
    float3 reflected = reflect(rayDir, halfVector);
    float reflectedLenSq = dot(reflected, reflected);
    if (reflectedLenSq <= 1.0e-8) {
        continuationDir = normal;
        return false;
    }

    continuationDir = reflected * rsqrt(reflectedLenSq);
    float3 viewDir = normalize(-rayDir);
    float NdotL = saturate(dot(normal, continuationDir));
    float NdotH = saturate(dot(normal, halfVector));
    float VdotH = saturate(dot(viewDir, halfVector));
    return (NdotL > 1.0e-5 && NdotH > 1.0e-5 && VdotH > 1.0e-5);
}

inline float3 BuildTransmissionContinuation(float3 rayDir, float3 normal,
                                            float ior)
{
    float entering = dot(rayDir, normal) < 0.0 ? 1.0 : 0.0;
    float3 faceNormal = (entering > 0.5) ? normal : -normal;
    float eta = (entering > 0.5) ? rcp(max(ior, 1.0)) : max(ior, 1.0);
    float3 refracted = refract(rayDir, faceNormal, eta);
    if (dot(refracted, refracted) < 1.0e-8) {
        refracted = reflect(rayDir, faceNormal);
    }
    return SafeNormalize(refracted, -faceNormal);
}

inline float WavefrontGiHash12(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

inline uint WavefrontGiHashU32(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

inline float WavefrontGiHash01(uint x)
{
    return (WavefrontGiHashU32(x) & 0x00FFFFFFu) / 16777215.0;
}

inline float2 WavefrontGiTransformUvForCell(float2 uv, float2 offset,
                                            float2 mirrorSign,
                                            float sinR, float cosR)
{
    float2 uvLocal = frac(uv);
    if (mirrorSign.x < 0.0) {
        uvLocal.x = 1.0 - uvLocal.x;
    }
    if (mirrorSign.y < 0.0) {
        uvLocal.y = 1.0 - uvLocal.y;
    }
    float2 centered = uvLocal - 0.5;
    return float2(centered.x * cosR - centered.y * sinR,
                  centered.x * sinR + centered.y * cosR) + 0.5 + offset;
}

inline bool WavefrontGiUseUvStochasticTiling(float4 variationParams,
                                             float4 rotationParams)
{
    return variationParams.x > 0.5 &&
           (variationParams.y > 1.0e-4 ||
            variationParams.z > 1.0e-4 ||
            variationParams.w > 1.0e-4 ||
            rotationParams.w > 0.5);
}

inline uint WavefrontGiVariationSeed(float4 variationParams,
                                     float3 objectOrigin,
                                     float3 worldNormal,
                                     uint primitiveId)
{
    int3 quantizedOrigin = int3(round(objectOrigin * 100.0));
    uint seed = WavefrontGiHashU32(
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
        uint signSeed = axisValue >= 0.0 ? 0x85ebca6bu : 0xc2b2ae35u;
        seed = WavefrontGiHashU32(seed ^ ((dominantAxis + 1u) * 0x9e3779b9u) ^
                                  signSeed ^ ((primitiveId + 1u) * 0x632be59bu));
    }
    return seed;
}

inline float3 WavefrontGiTriPlanarWeights(float3 n, float sharpness)
{
    float3 an = pow(abs(n), max(sharpness, 0.01));
    float s = an.x + an.y + an.z;
    return (s > 1.0e-5) ? (an / s) : float3(0.3333, 0.3333, 0.3333);
}

inline float3 WavefrontGiRotateVector(float3 v, float4 rotationParams)
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

inline float2 WavefrontGiTriUvX(float3 p, float3 n, float scale,
                                float2 offset)
{
    return (float2(-(n.x >= 0.0 ? 1.0 : -1.0) * p.z, p.y) + offset) *
           scale;
}

inline float2 WavefrontGiTriUvY(float3 p, float3 n, float scale,
                                float2 offset)
{
    return (float2(p.x, -(n.y >= 0.0 ? 1.0 : -1.0) * p.z) + offset) *
           scale;
}

inline float2 WavefrontGiTriUvZ(float3 p, float3 n, float scale,
                                float2 offset)
{
    return (float2((n.z >= 0.0 ? 1.0 : -1.0) * p.x, p.y) + offset) *
           scale;
}

inline float3 WavefrontGiBlendTextureRgb(float3 sampleValue, float amount)
{
    return lerp(float3(1.0, 1.0, 1.0), sampleValue, saturate(amount));
}

inline float WavefrontGiBlendTextureScalar(float sampleValue, float amount)
{
    return lerp(1.0, sampleValue, saturate(amount));
}

inline float4 WavefrontGiSampleUvTexture(int texIndex, float2 uv,
                                         float3 objectOrigin,
                                         float3 worldNormal,
                                         float4 variationParams,
                                         float4 rotationParams,
                                         float lod,
                                         bool applyColorVariation)
{
    if (texIndex < 0) {
        return float4(1.0, 1.0, 1.0, 1.0);
    }
    if (!WavefrontGiUseUvStochasticTiling(variationParams, rotationParams)) {
        return textures[texIndex].SampleLevel(linearSampler, uv, lod);
    }

    float2 baseCellF = floor(uv);
    int2 baseCell = int2(baseCellF);
    float2 tileFrac = uv - baseCellF;
    int2 cell0, cell1, cell2;
    float3 weights;
    if (tileFrac.x + tileFrac.y <= 1.0) {
        cell0 = baseCell;
        cell1 = baseCell + int2(1, 0);
        cell2 = baseCell + int2(0, 1);
        weights = float3(1.0 - tileFrac.x - tileFrac.y,
                         tileFrac.x,
                         tileFrac.y);
    } else {
        cell0 = baseCell + int2(1, 1);
        cell1 = baseCell + int2(0, 1);
        cell2 = baseCell + int2(1, 0);
        weights = float3(tileFrac.x + tileFrac.y - 1.0,
                         1.0 - tileFrac.x,
                         1.0 - tileFrac.y);
    }

    uint baseSeed = WavefrontGiVariationSeed(variationParams, objectOrigin,
                                             normalize(worldNormal), 0u);
    float4 result = 0.0;
    [unroll]
    for (uint i = 0u; i < 3u; ++i) {
        int2 cell = (i == 0u) ? cell0 : ((i == 1u) ? cell1 : cell2);
        uint cellSeed = WavefrontGiHashU32(
            baseSeed ^ (asuint(cell.x) * 0x632be59bu) ^
            (asuint(cell.y) * 0x85157af5u));
        float2 offset = (float2(WavefrontGiHash01(cellSeed ^ 0x68bc21ebu),
                                WavefrontGiHash01(cellSeed ^ 0x02e5be93u)) *
                         2.0 - 1.0) * variationParams.y;
        float2 mirrorSign = float2(1.0, 1.0);
        if (rotationParams.w > 0.5) {
            mirrorSign.x = WavefrontGiHash01(cellSeed ^ 0x51633e2du) > 0.5
                               ? -1.0
                               : 1.0;
            mirrorSign.y = WavefrontGiHash01(cellSeed ^ 0x68f7d247u) > 0.5
                               ? -1.0
                               : 1.0;
        }
        float sinR, cosR;
        sincos(radians(WavefrontGiHash01(cellSeed ^ 0x02c9277bu) *
                       variationParams.z),
               sinR, cosR);
        float4 sampleValue = textures[texIndex].SampleLevel(
            linearSampler,
            WavefrontGiTransformUvForCell(uv, offset, mirrorSign, sinR, cosR),
            lod);
        if (applyColorVariation && variationParams.w > 1.0e-4) {
            float3 tint = 1.0 +
                (float3(WavefrontGiHash01(cellSeed ^ 0x1b56c4e9u),
                        WavefrontGiHash01(cellSeed ^ 0x7f4a7c15u),
                        WavefrontGiHash01(cellSeed ^ 0x94d049bbu)) *
                 2.0 - 1.0) * variationParams.w;
            sampleValue.rgb *= max(tint / max(dot(tint, float3(0.2126, 0.7152, 0.0722)),
                                               1.0e-3),
                                   0.0);
        }
        result += sampleValue * ((i == 0u) ? weights.x :
                                 ((i == 1u) ? weights.y : weights.z));
    }
    return result;
}

inline float4 WavefrontGiSampleTriPlanar(int texIndex,
                                         float3 worldPos,
                                         float3 worldNormal,
                                         float scale,
                                         float sharpness,
                                         float4 variationParams,
                                         float4 rotationParams,
                                         float3 objectOrigin,
                                         uint primitiveId,
                                         float lod)
{
    if (texIndex < 0) {
        return float4(1.0, 1.0, 1.0, 1.0);
    }
    float3 rotatedPos = WavefrontGiRotateVector(worldPos, rotationParams);
    float3 rotatedNormal =
        normalize(WavefrontGiRotateVector(worldNormal, rotationParams));
    uint seed = WavefrontGiVariationSeed(variationParams, objectOrigin,
                                         worldNormal, primitiveId);
    float2 offset =
        (float2(WavefrontGiHash01(seed ^ 0x68bc21ebu),
                WavefrontGiHash01(seed ^ 0x02e5be93u)) * 2.0 - 1.0) *
        max(variationParams.y, 0.0);
    float3 weights = WavefrontGiTriPlanarWeights(rotatedNormal, sharpness);
    float4 sx = textures[texIndex].SampleLevel(
        linearSampler, WavefrontGiTriUvX(rotatedPos, rotatedNormal, scale, offset), lod);
    float4 sy = textures[texIndex].SampleLevel(
        linearSampler, WavefrontGiTriUvY(rotatedPos, rotatedNormal, scale, offset), lod);
    float4 sz = textures[texIndex].SampleLevel(
        linearSampler, WavefrontGiTriUvZ(rotatedPos, rotatedNormal, scale, offset), lod);
    return sx * weights.x + sy * weights.y + sz * weights.z;
}

inline float3 WavefrontGiUnpackNormal(float4 n)
{
    return n.xyz * 2.0 - 1.0;
}

inline float3 WavefrontGiBlendNormalSample(float3 tangentNormal, float amount)
{
    return normalize(lerp(float3(0.0, 0.0, 1.0), tangentNormal,
                          saturate(amount)));
}

inline float3 WavefrontGiSampleNormalMap(int texIndex,
                                         float2 uv,
                                         float3 worldPos,
                                         float3 worldNormal,
                                         float4 worldTangent,
                                         float amount,
                                         bool triPlanar,
                                         float triScale,
                                         float triSharp,
                                         float triNormalStrength,
                                         float4 variationParams,
                                         float4 rotationParams,
                                         float3 objectOrigin,
                                         uint primitiveId,
                                         float lod)
{
    if (texIndex < 0 || amount <= 0.0) {
        return normalize(worldNormal);
    }

    if (!triPlanar) {
        if (dot(worldTangent.xyz, worldTangent.xyz) < 1.0e-6) {
            return normalize(worldNormal);
        }
        float3 tangentNormal = WavefrontGiSampleUvTexture(
            texIndex, uv, objectOrigin, worldNormal,
            variationParams, rotationParams, lod, false).xyz * 2.0 - 1.0;
        tangentNormal = WavefrontGiBlendNormalSample(tangentNormal, amount);
        float3 n = normalize(worldNormal);
        float3 t = normalize(worldTangent.xyz);
        float3 b = cross(n, t) * worldTangent.w;
        return normalize(mul(tangentNormal, float3x3(t, b, n)));
    }

    float3 rotatedPos = WavefrontGiRotateVector(worldPos, rotationParams);
    float3 rotatedNormal =
        normalize(WavefrontGiRotateVector(worldNormal, rotationParams));
    uint seed = WavefrontGiVariationSeed(variationParams, objectOrigin,
                                         worldNormal, primitiveId);
    float2 offset =
        (float2(WavefrontGiHash01(seed ^ 0x68bc21ebu),
                WavefrontGiHash01(seed ^ 0x02e5be93u)) * 2.0 - 1.0) *
        max(variationParams.y, 0.0);
    float3 weights = WavefrontGiTriPlanarWeights(rotatedNormal, triSharp);
    float3 nx = WavefrontGiBlendNormalSample(
        WavefrontGiUnpackNormal(textures[texIndex].SampleLevel(
            linearSampler,
            WavefrontGiTriUvX(rotatedPos, rotatedNormal, triScale, offset), lod)),
        amount);
    float3 ny = WavefrontGiBlendNormalSample(
        WavefrontGiUnpackNormal(textures[texIndex].SampleLevel(
            linearSampler,
            WavefrontGiTriUvY(rotatedPos, rotatedNormal, triScale, offset), lod)),
        amount);
    float3 nz = WavefrontGiBlendNormalSample(
        WavefrontGiUnpackNormal(textures[texIndex].SampleLevel(
            linearSampler,
            WavefrontGiTriUvZ(rotatedPos, rotatedNormal, triScale, offset), lod)),
        amount);
    nx.xy *= triNormalStrength;
    ny.xy *= triNormalStrength;
    nz.xy *= triNormalStrength;

    float sx = (rotatedNormal.x >= 0.0) ? 1.0 : -1.0;
    float sy = (rotatedNormal.y >= 0.0) ? 1.0 : -1.0;
    float sz = (rotatedNormal.z >= 0.0) ? 1.0 : -1.0;
    float3 axisX = normalize(WavefrontGiRotateVector(float3(1, 0, 0), rotationParams));
    float3 axisY = normalize(WavefrontGiRotateVector(float3(0, 1, 0), rotationParams));
    float3 axisZ = normalize(WavefrontGiRotateVector(float3(0, 0, 1), rotationParams));
    float3x3 tbnX = float3x3(-axisZ * sx, axisY, axisX * sx);
    float3x3 tbnY = float3x3(axisX, -axisZ * sy, axisY * sy);
    float3x3 tbnZ = float3x3(axisX * sz, axisY, axisZ * sz);
    return normalize(normalize(mul(normalize(nx), tbnX)) * weights.x +
                     normalize(mul(normalize(ny), tbnY)) * weights.y +
                     normalize(mul(normalize(nz), tbnZ)) * weights.z);
}

inline bool WavefrontGiIsShadowVisible(float3 origin,
                                       float3 direction,
                                       float maxDistance)
{
    RayDesc shadowRay;
    shadowRay.Origin = origin;
    shadowRay.Direction = normalize(direction);
    shadowRay.TMin = 0.002;
    shadowRay.TMax = max(maxDistance, 0.002);

    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
             RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> shadowQuery;
    shadowQuery.TraceRayInline(g_accel, RAY_FLAG_NONE, 0xFF, shadowRay);
    shadowQuery.Proceed();
    return shadowQuery.CommittedStatus() == COMMITTED_NOTHING;
}

inline float3 WavefrontGiEvaluateBrdfLighting(float3 diffuseAlbedo,
                                              float3 f0,
                                              float roughness,
                                              float clearcoat,
                                              float3 normal,
                                              float3 viewDir,
                                              float3 lightDir,
                                              float3 radiance)
{
    float nDotL = saturate(dot(normal, lightDir));
    if (nDotL <= 0.0 || !any(radiance > 0.0)) {
        return float3(0.0, 0.0, 0.0);
    }
    float3 halfVec = normalize(viewDir + lightDir);
    float nDotV = saturate(dot(normal, viewDir));
    float nDotH = saturate(dot(normal, halfVec));
    float vDotH = saturate(dot(viewDir, halfVec));
    float3 f = F_Schlick(vDotH, f0);
    float d = D_GGX(nDotH, roughness);
    float vis = V_SmithCorrelated(nDotV, nDotL, roughness);
    float3 diffuseBrdf = (diffuseAlbedo / PI) * (1.0 - f);
    float3 specularBrdf = d * vis * f;
    return (diffuseBrdf * (1.0 - clearcoat) + specularBrdf) *
           radiance * nDotL;
}

inline float3 EvaluateWavefrontGiSurfaceRadiance(
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query,
    float3 incomingDirection,
    out float3 surfacePos)
{
    surfacePos = float3(0.0, 0.0, 0.0);
    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT) {
        return float3(0.0, 0.0, 0.0);
    }

    const uint meshIdx = query.CommittedInstanceID();
    MeshData mesh = meshData[meshIdx];
    const uint materialIndex = (uint)max(0, mesh.materialIndex);
    MaterialData material = materials[materialIndex];
    MaterialExtraData materialExtra = materialExtras[materialIndex];

    const uint primitiveIndex = query.CommittedPrimitiveIndex();
    const uint baseIndex = primitiveIndex * 3u;
    const uint i0 = indices[mesh.ibIndex].Load(baseIndex);
    const uint i1 = indices[mesh.ibIndex].Load(baseIndex + 1u);
    const uint i2 = indices[mesh.ibIndex].Load(baseIndex + 2u);
    const float2 bary2 = query.CommittedTriangleBarycentrics();
    const float3 bary = float3(1.0 - bary2.x - bary2.y, bary2.x, bary2.y);

    const uint matFlags = asuint(material.pbrParams_flags.w);
    int texDiff = UnpackTextureIndexLow(material.packedTextures.x);
    int texNorm = UnpackTextureIndexHigh(material.packedTextures.x);
    int texMR = UnpackTextureIndexLow(material.packedTextures.y);
    int texOcc = UnpackTextureIndexHigh(material.packedTextures.y);
    int texEmis = UnpackTextureIndexLow(material.packedTextures.z);
    int texOpacity = UnpackTextureIndexHigh(material.packedTextures.z);
    int texSpecular = UnpackTextureIndexLow(material.packedTextures.w);
    int texThickness = UnpackTextureIndexHigh(material.packedTextures.w);
    int texCoatNormal = UnpackTextureIndexLow(materialExtra.extraPackedTextures.x);

    const float4 triParams = materialExtra.triPlanarParams;
    const float4 mappingVariation = materialExtra.mappingVariationParams;
    const float4 triRotation = materialExtra.triPlanarRotationParams;
    const float4 texWeight0 = materialExtra.textureWeight0;
    const float4 texWeight1 = materialExtra.textureWeight1;
    const float4 coatLayer = materialExtra.coatLayerParams;
    const float4 volumeParams = materialExtra.volumeParams;
    const float4 specularColorParams = materialExtra.specularColor;
    const float4 lobeParams = materialExtra.lobeParams;

    const float3 localNormal =
        normalize(vertices[mesh.vbIndex][i0].normal * bary.x +
                  vertices[mesh.vbIndex][i1].normal * bary.y +
                  vertices[mesh.vbIndex][i2].normal * bary.z);
    const float4 localTangent =
        vertices[mesh.vbIndex][i0].tangent * bary.x +
        vertices[mesh.vbIndex][i1].tangent * bary.y +
        vertices[mesh.vbIndex][i2].tangent * bary.z;
    float2 uv =
        vertices[mesh.vbIndex][i0].uv * bary.x +
        vertices[mesh.vbIndex][i1].uv * bary.y +
        vertices[mesh.vbIndex][i2].uv * bary.z;
    if ((matFlags & MATERIAL_FLAG_UV_TRANSFORM) != 0u) {
        uv = uv * materialExtra.uvTransform.xy + materialExtra.uvTransform.zw;
    }

    const float3x4 objectToWorld = query.CommittedObjectToWorld3x4();
    const float3x4 worldToObject = query.CommittedWorldToObject3x4();
    float3 worldNormal = normalize(mul(localNormal, (float3x3)worldToObject));
    float4 worldTangent;
    worldTangent.xyz = normalize(mul((float3x3)objectToWorld,
                                     localTangent.xyz));
    worldTangent.w = localTangent.w;
    if (dot(worldNormal, -incomingDirection) < 0.0) {
        worldNormal = -worldNormal;
    }

    surfacePos = query.WorldRayOrigin() +
                 query.WorldRayDirection() * query.CommittedRayT();

    float3 objectOrigin = mul(objectToWorld, float4(0.0, 0.0, 0.0, 1.0));
    const bool triPlanar =
        ((matFlags & MATERIAL_FLAG_TRI_PLANAR) != 0u) && triParams.x > 0.5;
    const float triScale = max(triParams.y, 1.0e-6);
    const float triSharp = max(triParams.z, 0.01);
    const float triNormalStrength = max(triParams.w, 0.0);
    const float textureLod =
        clamp(log2(max(length(surfacePos - camPos), 1.0e-3) * 0.02) + 0.35,
              0.0, 10.0);

    float3 baseColor = saturate(material.baseColor_opacity.rgb);
    float opacity = saturate(material.baseColor_opacity.a);
    if (texDiff >= 0) {
        float4 diffSample = triPlanar
            ? WavefrontGiSampleTriPlanar(texDiff, surfacePos, worldNormal,
                                         triScale, triSharp, mappingVariation,
                                         triRotation, objectOrigin,
                                         primitiveIndex, textureLod)
            : WavefrontGiSampleUvTexture(texDiff, uv, objectOrigin,
                                         worldNormal, mappingVariation,
                                         triRotation, textureLod, true);
        baseColor *= WavefrontGiBlendTextureRgb(sRGBToLinear(diffSample.rgb),
                                                texWeight0.x);
        opacity *= WavefrontGiBlendTextureScalar(diffSample.a, texWeight0.x);
    }
    if (texOpacity >= 0) {
        float opacitySample = triPlanar
            ? WavefrontGiSampleTriPlanar(texOpacity, surfacePos, worldNormal,
                                         triScale, triSharp, mappingVariation,
                                         triRotation, objectOrigin,
                                         primitiveIndex, textureLod).r
            : WavefrontGiSampleUvTexture(texOpacity, uv, objectOrigin,
                                         worldNormal, mappingVariation,
                                         triRotation, textureLod, false).r;
        opacity *= WavefrontGiBlendTextureScalar(opacitySample,
                                                 texWeight1.w);
    }

    float metallic = saturate(material.pbrParams_flags.x);
    float roughness = max(saturate(material.pbrParams_flags.y), 0.001);
    if (texMR >= 0) {
        float4 mrSample = triPlanar
            ? WavefrontGiSampleTriPlanar(texMR, surfacePos, worldNormal,
                                         triScale, triSharp, mappingVariation,
                                         triRotation, objectOrigin,
                                         primitiveIndex, textureLod)
            : WavefrontGiSampleUvTexture(texMR, uv, objectOrigin, worldNormal,
                                         mappingVariation, triRotation,
                                         textureLod, false);
        float roughnessFactor = ((matFlags & MATERIAL_FLAG_INVERT_ROUGHNESS) != 0u)
                                    ? max(1.0 - mrSample.g, 0.0)
                                    : mrSample.g;
        roughness *= WavefrontGiBlendTextureScalar(roughnessFactor,
                                                   texWeight0.y);
        metallic *= WavefrontGiBlendTextureScalar(mrSample.b,
                                                  texWeight0.y);
    }
    roughness = max(roughness, 0.001);

    float transmission = saturate(material.pbrParams_flags.z) * (1.0 - metallic);
    if (materialExtra.shadingParams.z < 0.0 && opacity < 0.999) {
        transmission = max(transmission, 1.0 - opacity);
    }
    float3 diffuseAlbedo = baseColor * (1.0 - metallic) *
                           (1.0 - transmission);
    if (materialExtra.shadingParams.z < 0.0) {
        diffuseAlbedo *= opacity;
    }

    float clearcoat = saturate(coatLayer.x);
    float3 normal = WavefrontGiSampleNormalMap(
        texNorm, uv, surfacePos, worldNormal, worldTangent,
        texWeight1.x, triPlanar, triScale, triSharp, triNormalStrength,
        mappingVariation, triRotation, objectOrigin, primitiveIndex,
        textureLod);
    if (clearcoat > 0.001 && texCoatNormal >= 0 && lobeParams.x > 1.0e-4) {
        float3 coatNormal = WavefrontGiSampleNormalMap(
            texCoatNormal, uv, surfacePos, worldNormal, worldTangent,
            lobeParams.x, triPlanar, triScale, triSharp, triNormalStrength,
            mappingVariation, triRotation, objectOrigin, primitiveIndex,
            textureLod);
        normal = normalize(lerp(normal, coatNormal, clearcoat));
    }
    if (dot(normal, -incomingDirection) < 0.0) {
        normal = -normal;
    }

    float ao = 1.0;
    if (texOcc >= 0) {
        float aoSample = triPlanar
            ? WavefrontGiSampleTriPlanar(texOcc, surfacePos, worldNormal,
                                         triScale, triSharp, mappingVariation,
                                         triRotation, objectOrigin,
                                         primitiveIndex, textureLod).r
            : WavefrontGiSampleUvTexture(texOcc, uv, objectOrigin,
                                         worldNormal, mappingVariation,
                                         triRotation, textureLod, false).r;
        ao = WavefrontGiBlendTextureScalar(aoSample, texWeight1.y);
    }

    float3 emissive =
        material.emissive_ior.rgb *
        (5.0 * max(0.0, materialExtra.shadingParams.x));
    if (texEmis >= 0) {
        float3 e = triPlanar
            ? WavefrontGiSampleTriPlanar(texEmis, surfacePos, worldNormal,
                                         triScale, triSharp, mappingVariation,
                                         triRotation, objectOrigin,
                                         primitiveIndex, textureLod).rgb
            : WavefrontGiSampleUvTexture(texEmis, uv, objectOrigin,
                                         worldNormal, mappingVariation,
                                         triRotation, textureLod, true).rgb;
        emissive *= WavefrontGiBlendTextureRgb(sRGBToLinear(e),
                                               texWeight1.z);
    }

    float thickness = max(volumeParams.x, 0.0);
    if (texThickness >= 0) {
        float thicknessSample = triPlanar
            ? WavefrontGiSampleTriPlanar(texThickness, surfacePos, worldNormal,
                                         triScale, triSharp, mappingVariation,
                                         triRotation, objectOrigin,
                                         primitiveIndex, textureLod).r
            : WavefrontGiSampleUvTexture(texThickness, uv, objectOrigin,
                                         worldNormal, mappingVariation,
                                         triRotation, textureLod, false).r;
        thickness *= WavefrontGiBlendTextureScalar(thicknessSample,
                                                   volumeParams.z);
    }

    float3 specularColor = saturate(specularColorParams.rgb);
    if (texSpecular >= 0) {
        float3 specSample = triPlanar
            ? WavefrontGiSampleTriPlanar(texSpecular, surfacePos, worldNormal,
                                         triScale, triSharp, mappingVariation,
                                         triRotation, objectOrigin,
                                         primitiveIndex, textureLod).rgb
            : WavefrontGiSampleUvTexture(texSpecular, uv, objectOrigin,
                                         worldNormal, mappingVariation,
                                         triRotation, textureLod, false).rgb;
        specularColor *= WavefrontGiBlendTextureRgb(sRGBToLinear(specSample),
                                                    specularColorParams.a);
    }

    float ior = max(material.emissive_ior.w, 1.0);
    float specularWeight = saturate(materialExtra.shadingParams.y);
    float3 f0 = ComputeWavefrontSurfaceF0(baseColor, metallic, ior,
                                          specularWeight, specularColor);
    float3 viewDir = normalize(-incomingDirection);
    float3 direct = float3(0.0, 0.0, 0.0);
    WavefrontLightSample sun = WavefrontSampleDirectionalLight(1.0);
    float nDotL = saturate(dot(normal, sun.direction));
    if (nDotL > 0.0 &&
        WavefrontGiIsShadowVisible(surfacePos + normal * kWavefrontRayBias,
                                   sun.direction, sun.maxDistance)) {
        direct += WavefrontGiEvaluateBrdfLighting(
            diffuseAlbedo, f0, roughness, clearcoat,
            normal, viewDir, sun.direction, sun.radiance);
    }
    float3 giLighting = direct;
    float translucency = saturate(coatLayer.w);
    if (translucency > 0.001) {
        float backNdotL = saturate(dot(-normal, sun.direction));
        giLighting += (diffuseAlbedo / PI) * sun.radiance * backNdotL *
                      translucency;
    }

    return max((emissive + giLighting * ao) * intensity, 0.0);
}

inline GI_Reservoir GenerateWavefrontGiCandidate(float3 hitPos,
                                                 float3 normal,
                                                 float3 diffuseAlbedo,
                                                 inout RNG rng)
{
    GI_Reservoir reservoir = init_gi_reservoir();
    if (maxGIBounces <= 0.0 || !any(diffuseAlbedo > 1.0e-4)) {
        return reservoir;
    }

    float3 candidateDir = BuildDiffuseContinuation(normal, rng);
    float NdotL = saturate(dot(normal, candidateDir));
    float pdf = NdotL / PI;
    if (pdf <= 1.0e-6 || NdotL <= 0.0) {
        return reservoir;
    }

    RayDesc giRay;
    giRay.Origin = hitPos + normal * kWavefrontRayBias;
    giRay.Direction = candidateDir;
    giRay.TMin = 0.001;
    giRay.TMax = 10000.0;

    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
    query.TraceRayInline(g_accel, RAY_FLAG_NONE, 0xFF, giRay);
    query.Proceed();

    float3 radiance = float3(0.0, 0.0, 0.0);
    float3 candidatePos = hitPos + candidateDir * 1000.0;
    if (query.CommittedStatus() == COMMITTED_NOTHING) {
        radiance = WavefrontEvaluateEnvironmentRadiance(candidateDir,
                                                        candidatePos);
    } else if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        radiance = EvaluateWavefrontGiSurfaceRadiance(query, candidateDir,
                                                      candidatePos);
    }

    float3 brdf = diffuseAlbedo / PI;
    float pTarget = length(max(radiance * brdf * NdotL, 0.0));
    float risWeight = min(pTarget / max(pdf, 1.0e-5), 1.0e5);
    update_gi_reservoir(reservoir, candidatePos, radiance, risWeight, rng);
    finalize_gi_reservoir(reservoir, pTarget);
    return reservoir;
}

inline float3 EvaluateWavefrontGiReservoirContribution(GI_Reservoir reservoir,
                                                       float3 hitPos,
                                                       float3 normal,
                                                       float3 diffuseAlbedo)
{
    if (reservoir.M == 0u || reservoir.W <= 0.0 ||
        !any(reservoir.radiance > 0.0)) {
        return float3(0.0, 0.0, 0.0);
    }

    float3 candidateVector = reservoir.hitPos - hitPos;
    float candidateDistanceSq = dot(candidateVector, candidateVector);
    if (candidateDistanceSq <= 1.0e-8) {
        return float3(0.0, 0.0, 0.0);
    }

    float3 candidateDir = candidateVector * rsqrt(candidateDistanceSq);
    float nDotL = saturate(dot(normal, candidateDir));
    float3 brdf = diffuseAlbedo / PI;
    return max(reservoir.radiance * brdf * nDotL * reservoir.W, 0.0);
}

inline void EmitWavefrontSecondaryPath(uint queueIndex,
                                       uint pixelIndex,
                                       float3 origin,
                                       float3 direction,
                                       uint rngState,
                                       float3 throughput,
                                       uint packedState)
{
    WavefrontPathState nextState;
    nextState.origin = origin;
    nextState.pixelIndex = pixelIndex;
    nextState.direction = direction;
    nextState.rngState = rngState;
    nextState.throughput = throughput;
    nextState.packedState = packedState;
    g_wavefrontPathQueueB[queueIndex] = nextState;
}

inline void EmitWavefrontShadowTask(uint queueIndex,
                                    float3 origin,
                                    float3 direction,
                                    float maxDistance,
                                    uint packedLightIndex,
                                    float3 throughput,
                                    uint pixelIndex)
{
    WavefrontShadowTask task;
    task.origin = origin;
    task.maxDistance = maxDistance;
    task.direction = direction;
    task.packedLightIndex = packedLightIndex;
    task.throughput = throughput;
    task.packedState = pixelIndex;
    g_wavefrontShadowQueue[queueIndex] = task;
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    const uint dispatchIndex = dispatchThreadID.x;
    uint pathIndex = dispatchIndex;
    const bool useMaterialBinList =
        (reservedFlags & WAVEFRONT_QUEUE_FLAG_USE_MATERIAL_BIN_LIST) != 0u;
    const bool missOnly =
        (reservedFlags & WAVEFRONT_QUEUE_FLAG_MISS_ONLY) != 0u;
    if (useMaterialBinList) {
        const uint materialBin = WavefrontGetMaterialBinFromQueueFlags(
            reservedFlags);
        uint binIndexCapacity = 0u;
        uint binIndexStride = 0u;
        g_wavefrontMaterialBinIndices.GetDimensions(binIndexCapacity,
                                                    binIndexStride);
        const uint perBinCapacity =
            binIndexCapacity / WAVEFRONT_MATERIAL_BIN_COUNT;
        if (materialBin >= WAVEFRONT_MATERIAL_BIN_COUNT ||
            dispatchIndex >= perBinCapacity ||
            dispatchIndex >=
                g_wavefrontQueueCounters[
                    WAVEFRONT_MATERIAL_BIN_COUNTER_BASE + materialBin]) {
            return;
        }
        pathIndex =
            g_wavefrontMaterialBinIndices[materialBin * perBinCapacity +
                                          dispatchIndex];
    }
    if (pathIndex >= activeCount) {
        return;
    }

    WavefrontHitRecord record = g_wavefrontHitQueue[pathIndex];
    WavefrontPathState state = g_wavefrontPathQueueA[pathIndex];
    uint2 pixel = WavefrontPixelCoord(record.pixelIndex);
    if (pixel.x >= outputWidth || pixel.y >= outputHeight) {
        return;
    }

    if (dispatchIndex == 0u && !useMaterialBinList) {
        g_wavefrontStats[8] = activeCount;
    }

    float2 currScreen = float2(pixel) + 0.5;
    float3 rayDir = normalize(state.direction);
    float3 pathThroughput = max(state.throughput, 0.0);
    bool isMiss = WavefrontHitRecordIsMiss(record);
    if (missOnly && !isMiss) {
        return;
    }
    if (useMaterialBinList && isMiss) {
        return;
    }
    const bool primarySurfaceOnly =
        (reservedFlags & WAVEFRONT_RESOLVE_FLAG_PRIMARY_SURFACE_ONLY) != 0u;

    float3 color = WavefrontHitRecordGetColor(record) * pathThroughput;
    float depth = (dlssRayReconstruction > 0.5) ? farZ : 1.0;
    float linearDepth = farZ;
    float2 motion = ComputeWavefrontSkyMotion(rayDir, currScreen);
    float3 normal = float3(0.0, 1.0, 0.0);
    float3 albedo = float3(0.0, 0.0, 0.0);
    float3 specularAlbedo = float3(0.0, 0.0, 0.0);
    float3 rrSpecularAlbedo = float3(0.0, 0.0, 0.0);
    float roughness = 1.0;
    uint pathQueueCapacity = 0u;
    uint pathQueueStride = 0u;
    uint shadowQueueCapacity = 0u;
    uint shadowQueueStride = 0u;
    Reservoir diReservoir = init_reservoir();
    GI_Reservoir giReservoir = init_gi_reservoir();
    WavefrontLightSample selectedDirectLightSample =
        WavefrontSampleDirectionalLight(1.0);
    float selectedDirectLightWeight = 0.0;
    ReserveWavefrontQueueDimensions(pathQueueCapacity, pathQueueStride,
                                    shadowQueueCapacity, shadowQueueStride);

    if (!isMiss) {
        float3 hitPos = state.origin + rayDir * record.hitT;
        normal = UnpackNormalOctahedron(record.packedNormal);
        albedo = UnpackPayloadAlbedo(record.packedAlbedo);
        float4 surface = UnpackPayloadSurface(record.packedSurface);
        roughness = saturate(surface.x);
        float metallic = saturate(surface.y);
        float transmission = saturate(surface.z);
        float translucency = saturate(surface.w);
        float3 diffuseAlbedo = albedo * (1.0 - metallic) *
                               (1.0 - transmission);
        float specularWeight = saturate(UnpackPayloadSpecularWeight(record.packedIorType));
        float ior = UnpackPayloadIor(record.packedIorType);
        float3 transmissionTint = UnpackPayloadTransmissionColor(record.packedTransmission);
        float3 specularColor = UnpackPayloadSpecularColor(record.packedSpecular);
        specularAlbedo = ComputeWavefrontSpecularThroughput(
            albedo, metallic, ior, specularWeight, specularColor, transmission);
        {
            float3 f0 = ComputeWavefrontSurfaceF0(albedo, metallic, ior,
                                                  specularWeight,
                                                  specularColor);
            float nDotV = saturate(dot(normal, -rayDir));
            rrSpecularAlbedo = EnvBRDFApprox2(f0,
                                              roughness * roughness,
                                              nDotV);
        }
        motion = ComputeWavefrontSurfaceMotion(hitPos, currScreen);

        float3 forwardDir = normalize(camForward);
        float viewZ = dot(hitPos - camPos, forwardDir);
        if (viewZ > 0.0) {
            linearDepth = viewZ;
            if (dlssRayReconstruction > 0.5) {
                depth = viewZ;
            } else {
                float A = farZ / (farZ - nearZ);
                float B = (-nearZ * farZ) / (farZ - nearZ);
                depth = saturate(A + (B / viewZ));
            }
        }

        uint surfaceStatPrevious = 0u;
        InterlockedAdd(g_wavefrontStats[9], 1u, surfaceStatPrevious);

        if (!primarySurfaceOnly) {
            RNG rng;
            rng.state = state.rngState ^ (pathIndex * 0x9E3779B9u) ^ 0xB5297A4Du;
            const uint maxSpecularBounceCount =
                (maxSpecularBounces > 0.0) ? (uint)maxSpecularBounces : 0u;
            const uint maxRefractiveBounceCount =
                (maxRefractiveBounces > 0.0) ? (uint)maxRefractiveBounces : 0u;
            const uint maxDiffuseBounceCount =
                (maxGIBounces > 0.0) ? (uint)maxGIBounces : 0u;

            uint nextRayType = RAY_TYPE_DIFFUSE;
            float3 nextDirection = normal;
            float3 nextThroughput = state.throughput * max(albedo, 0.02.xxx);
            
            // Evaluate probabilities for path continuation to avoid hard cut-offs
            float transmissionProb = 0.0;
            float reflectionProb = 0.0;
            float diffuseProb = 0.0;
            ComputeWavefrontLobeProbabilities(normal, -rayDir,
                              albedo, metallic, transmission,
                              translucency, ior, specularWeight,
                              specularColor,
                              reflectionProb, diffuseProb,
                              transmissionProb);
            
            float rnd = next_float(rng);
            if (transmissionProb > 0.0 && rnd < transmissionProb) {
                nextRayType = RAY_TYPE_REFRACTION;
                nextDirection = BuildTransmissionContinuation(rayDir, normal, ior);
                nextThroughput = state.throughput * max(transmissionTint, 0.02.xxx) * max(transmission, 0.1) / max(transmissionProb, 1.0e-4);
            } else if (reflectionProb > 0.0 && rnd < (transmissionProb + reflectionProb)) {
                nextRayType = RAY_TYPE_REFLECTION;
                if (BuildSpecularContinuation(rayDir, normal, roughness, rng,
                                              nextDirection)) {
                    nextThroughput = state.throughput * max(specularAlbedo, 0.04.xxx) / max(reflectionProb, 1.0e-4);
                } else {
                    nextThroughput = float3(0.0, 0.0, 0.0);
                }
            } else {
                nextRayType = RAY_TYPE_DIFFUSE;
                nextDirection = BuildDiffuseContinuation(normal, rng);
                nextThroughput = state.throughput * max(albedo, 0.02.xxx) * saturate(dot(normal, nextDirection)) / max(diffuseProb, 1.0e-4);
            }
            nextThroughput = max(nextThroughput, 0.0);

            {
                diReservoir = LoadWavefrontDiReservoir(pixel);
                WavefrontLightSamplerContext lightSampler =
                    WavefrontCreateLightSampler(hitPos);
                const uint numLights = lightSampler.availableLights;
                WavefrontLightSample finalSample;
                finalSample.direction = float3(0.0, 1.0, 0.0);
                finalSample.maxDistance = 0.0;
                finalSample.radiance = float3(0.0, 0.0, 0.0);
                finalSample.packedLightIndex =
                    WavefrontPackLightSampleMetadata(WAVEFRONT_LIGHT_SAMPLE_DIRECTIONAL, 0u);
                if (diReservoir.lightIndex == 0xFFFFFFFFu) {
                    finalSample = WavefrontSampleDirectionalLight(1.0);
                } else if (diReservoir.lightIndex < numLights) {
                    finalSample = WavefrontSampleFlatLight(hitPos,
                                                           diReservoir.lightIndex,
                                                           1.0);
                }
                selectedDirectLightSample = finalSample;
                selectedDirectLightWeight = diReservoir.W;
            }

        uint previousValue = 0u;
        if (nextRayType == RAY_TYPE_REFRACTION) {
            InterlockedAdd(g_wavefrontStats[12], 1u, previousValue);
        } else if (nextRayType == RAY_TYPE_REFLECTION) {
            InterlockedAdd(g_wavefrontStats[11], 1u, previousValue);
        } else {
            InterlockedAdd(g_wavefrontStats[10], 1u, previousValue);
        }

        const bool allowContinuation =
            any(nextThroughput > 1.0e-4) &&
            WavefrontHasBounceBudget(state.packedState,
                                     nextRayType,
                                     maxSpecularBounceCount,
                                     maxRefractiveBounceCount,
                                     maxDiffuseBounceCount);
        if (allowContinuation) {
            uint secondaryIndex = 0u;
            InterlockedAdd(g_wavefrontQueueCounters[kWavefrontSecondaryQueueCounter], 1u,
                           secondaryIndex);
            if (secondaryIndex < pathQueueCapacity) {
                EmitWavefrontSecondaryPath(
                    secondaryIndex,
                    record.pixelIndex,
                    hitPos + nextDirection * kWavefrontRayBias,
                    nextDirection,
                    rng.state,
                    nextThroughput,
                    WavefrontAdvancePackedState(state.packedState, nextRayType));
                InterlockedAdd(g_wavefrontStats[16], 1u, previousValue);
                if (nextRayType == RAY_TYPE_REFRACTION) {
                    InterlockedAdd(g_wavefrontStats[19], 1u, previousValue);
                } else if (nextRayType == RAY_TYPE_REFLECTION) {
                    InterlockedAdd(g_wavefrontStats[18], 1u, previousValue);
                } else {
                    InterlockedAdd(g_wavefrontStats[17], 1u, previousValue);
                }
            } else {
                InterlockedAdd(g_wavefrontStats[21], 1u, previousValue);
            }
        }

        float3 shadowWeight = state.throughput *
                              ComputeWavefrontDirectLightingWeight(
                                  record, normal, hitPos,
                                  selectedDirectLightSample.direction) *
                              selectedDirectLightWeight;
        WavefrontLightSample explicitSunSample =
            WavefrontSampleDirectionalLight(1.0);
        float3 sunShadowWeight = state.throughput *
                                 ComputeWavefrontDirectLightingWeight(
                                     record, normal, hitPos,
                                     explicitSunSample.direction);
        if (any(sunShadowWeight > 1.0e-4)) {
            uint shadowIndex = 0u;
            InterlockedAdd(g_wavefrontQueueCounters[kWavefrontShadowQueueCounter],
                           1u, shadowIndex);
            if (shadowIndex < shadowQueueCapacity) {
                EmitWavefrontShadowTask(
                    shadowIndex,
                    hitPos + normal * kWavefrontRayBias,
                    explicitSunSample.direction,
                    explicitSunSample.maxDistance,
                    explicitSunSample.packedLightIndex,
                    sunShadowWeight,
                    record.pixelIndex);
                InterlockedAdd(g_wavefrontStats[20], 1u, previousValue);
            } else {
                InterlockedAdd(g_wavefrontStats[22], 1u, previousValue);
            }
        }
        if (WavefrontGetLightSampleType(selectedDirectLightSample.packedLightIndex) !=
                WAVEFRONT_LIGHT_SAMPLE_DIRECTIONAL &&
            selectedDirectLightWeight > 0.0 && any(shadowWeight > 1.0e-4)) {
            uint shadowIndex = 0u;
            InterlockedAdd(g_wavefrontQueueCounters[kWavefrontShadowQueueCounter],
                           1u, shadowIndex);
            if (shadowIndex < shadowQueueCapacity) {
                EmitWavefrontShadowTask(
                    shadowIndex,
                    hitPos + normal * kWavefrontRayBias,
                    selectedDirectLightSample.direction,
                    selectedDirectLightSample.maxDistance,
                    selectedDirectLightSample.packedLightIndex,
                    shadowWeight,
                    record.pixelIndex);
                InterlockedAdd(g_wavefrontStats[20], 1u, previousValue);
            } else {
                InterlockedAdd(g_wavefrontStats[22], 1u, previousValue);
            }
        }

        giReservoir = GenerateWavefrontGiCandidate(hitPos, normal,
                                                   diffuseAlbedo, rng);
        color += state.throughput *
                 EvaluateWavefrontGiReservoirContribution(
                     giReservoir, hitPos, normal, diffuseAlbedo);
        }
    } else {
        uint previousValue = 0u;
        InterlockedAdd(g_wavefrontStats[13], 1u, previousValue);
    }

    if (primarySurfaceOnly || isMiss) {
        StoreWavefrontDiReservoir(pixel, init_reservoir());
    }
    if (primarySurfaceOnly || isMiss) {
        ClearWavefrontGiReservoir(pixel);
    } else {
        StoreWavefrontGiReservoir(pixel, giReservoir);
    }

    if (dlssRayReconstruction > 0.5) {
        if (WavefrontHitRecordGuideIsMiss(record)) {
            float3 guideSkyDir = normalize(record.guideDirection);
            depth = farZ;
            linearDepth = farZ;
            motion = ComputeWavefrontSkyMotion(guideSkyDir, currScreen);
            normal = float3(0.0, 1.0, 0.0);
            albedo = float3(0.0, 0.0, 0.0);
            roughness = 1.0;
            rrSpecularAlbedo = float3(0.0, 0.0, 0.0);
        } else {
            float3 guideDir = normalize(record.guideDirection);
            float3 guideHitPos = record.guideOrigin + guideDir * record.guideHitT;
            float3 guideNormal =
                UnpackNormalOctahedron(record.guidePackedNormal);
            float3 guideAlbedo = UnpackPayloadAlbedo(record.guidePackedAlbedo);
            float4 guideSurface =
                UnpackPayloadSurface(record.guidePackedSurface);
            float guideRoughness = saturate(guideSurface.x);
            float guideMetallic = saturate(guideSurface.y);
            float guideSpecularWeight =
                saturate(UnpackPayloadSpecularWeight(
                    record.guidePackedIorType));
            float guideIor = UnpackPayloadIor(record.guidePackedIorType);
            float3 guideSpecularColor =
                UnpackPayloadSpecularColor(record.guidePackedSpecular);

            normal = guideNormal;
            albedo = guideAlbedo;
            roughness = guideRoughness;
            motion = ComputeWavefrontSurfaceMotion(guideHitPos, currScreen);

            float3 forwardDir = normalize(camForward);
            float viewZ = dot(guideHitPos - camPos, forwardDir);
            if (viewZ > 0.0) {
                linearDepth = viewZ;
                depth = viewZ;
            } else {
                linearDepth = farZ;
                depth = farZ;
            }

            float3 guideF0 = ComputeWavefrontSurfaceF0(
                guideAlbedo, guideMetallic, guideIor, guideSpecularWeight,
                guideSpecularColor);
            float guideNdotV = saturate(dot(guideNormal, -guideDir));
            rrSpecularAlbedo =
                EnvBRDFApprox2(guideF0, guideRoughness * guideRoughness,
                               guideNdotV);
        }
    }

    color = max(color, 0.0);
    float4 history = g_accumulation[pixel];
    float historyCount = history.a;
    bool invalidHistory = !isfinite(historyCount) || (historyCount < 1.0) ||
                          any(!isfinite(history.rgb));
    float3 historySum = invalidHistory ? float3(0.0, 0.0, 0.0) : history.rgb;
    float nextCount = invalidHistory ? 1.0 : (historyCount + 1.0);
    float3 nextSum = historySum + color;
    g_accumulation[pixel] = float4(nextSum, nextCount);
    g_output[pixel] = (dlssRayReconstruction > 0.5)
                          ? float4(color, 1.0)
                          : float4(nextSum / max(nextCount, 1.0), 1.0);
    g_depth[pixel] = depth;
    g_linearDepth[pixel] = linearDepth;
    g_motionVectors[pixel] = motion;
    g_albedoOut[pixel] = float4(albedo, 1.0);
    g_normalRoughnessOut[pixel] = float4(normalize(normal), roughness);
    g_specularAlbedo[pixel] = float4(rrSpecularAlbedo, 1.0);
    g_specHitDistance[pixel] = 0.0;
    g_specularMotionVectors[pixel] = any(rrSpecularAlbedo > 0.0) ? motion : kInvalidMvec;
    g_transmissionAccumulation[pixel] = float4(0.0, 0.0, 0.0, 0.0);
    g_transmissionVariance[pixel] = 0.0;
}
