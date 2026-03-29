Texture2D<float4> g_hdrInput : register(t0);
Texture2D<float> g_depthTexture : register(t1);
Texture2D<float4> g_normalRoughnessTexture : register(t2);
RWTexture2D<float4> g_out : register(u0);
SamplerState g_linearClampSampler : register(s0);

cbuffer TonemapCB : register(b0)
{
    uint outWidth;
    uint outHeight;
    float exposure;
    float vignette;
    float saturation;
    float contrast;
    float aoIntensity;
    float aoRadiusMeters;
    uint aoMode;
    float _pad0;
};

float3 ToneMapFilmic(float3 x)
{
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float SampleDepthUv(float2 uv)
{
    return g_depthTexture.SampleLevel(g_linearClampSampler, saturate(uv), 0.0f);
}

float3 SampleUnitNormalUv(float2 uv)
{
    float3 n = g_normalRoughnessTexture.SampleLevel(
        g_linearClampSampler, saturate(uv), 0.0f).xyz;
    float lenSq = dot(n, n);
    if (lenSq <= 1.0e-8f) {
        return float3(0.0f, 0.0f, 0.0f);
    }
    return n * rsqrt(lenSq);
}

float ComputeDxrTonemapAo(uint2 pixel)
{
    if (aoIntensity <= 1.0e-4f || aoRadiusMeters <= 1.0e-4f) {
        return 1.0f;
    }

    uint depthWidth = 0;
    uint depthHeight = 0;
    g_depthTexture.GetDimensions(depthWidth, depthHeight);
    uint normalWidth = 0;
    uint normalHeight = 0;
    g_normalRoughnessTexture.GetDimensions(normalWidth, normalHeight);
    if (depthWidth == 0 || depthHeight == 0 ||
        normalWidth == 0 || normalHeight == 0) {
        return 1.0f;
    }

    float2 uv = (float2(pixel) + 0.5f) / float2(outWidth, outHeight);
    float centerDepth = SampleDepthUv(uv);
    float3 centerNormal = SampleUnitNormalUv(uv);
    if (centerDepth <= 1.0e-5f || dot(centerNormal, centerNormal) <= 1.0e-8f) {
        return 1.0f;
    }

    float sampleRadiusPixels = clamp((aoRadiusMeters / max(centerDepth, 0.1f)) *
                                         180.0f,
                                     1.0f, 48.0f);
    int radius = (int)ceil(sampleRadiusPixels);
    float depthBias = max(aoRadiusMeters * 0.05f, 0.001f);
    float planeFitNormalCos = 0.92f;
    float minSampleNormalCos = 0.35f;
    float2 depthTexelUv = 1.0f / float2(depthWidth, depthHeight);
    float2 normalTexelUv = 1.0f / float2(normalWidth, normalHeight);

    float leftDepth = SampleDepthUv(uv + float2(-depthTexelUv.x, 0.0f));
    float rightDepth = SampleDepthUv(uv + float2(depthTexelUv.x, 0.0f));
    float upDepth = SampleDepthUv(uv + float2(0.0f, -depthTexelUv.y));
    float downDepth = SampleDepthUv(uv + float2(0.0f, depthTexelUv.y));

    float3 leftNormal = SampleUnitNormalUv(uv + float2(-normalTexelUv.x, 0.0f));
    float3 rightNormal = SampleUnitNormalUv(uv + float2(normalTexelUv.x, 0.0f));
    float3 upNormal = SampleUnitNormalUv(uv + float2(0.0f, -normalTexelUv.y));
    float3 downNormal = SampleUnitNormalUv(uv + float2(0.0f, normalTexelUv.y));

    float gradX = 0.0f;
    float gradY = 0.0f;
    float gradXWeight = 0.0f;
    float gradYWeight = 0.0f;

    if (leftDepth > 1.0e-5f && dot(centerNormal, leftNormal) >= planeFitNormalCos) {
        gradX += centerDepth - leftDepth;
        gradXWeight += 1.0f;
    }
    if (rightDepth > 1.0e-5f && dot(centerNormal, rightNormal) >= planeFitNormalCos) {
        gradX += rightDepth - centerDepth;
        gradXWeight += 1.0f;
    }
    if (upDepth > 1.0e-5f && dot(centerNormal, upNormal) >= planeFitNormalCos) {
        gradY += centerDepth - upDepth;
        gradYWeight += 1.0f;
    }
    if (downDepth > 1.0e-5f && dot(centerNormal, downNormal) >= planeFitNormalCos) {
        gradY += downDepth - centerDepth;
        gradYWeight += 1.0f;
    }

    gradX = gradXWeight > 0.0f ? gradX / gradXWeight : 0.0f;
    gradY = gradYWeight > 0.0f ? gradY / gradYWeight : 0.0f;

    float accumulation = 0.0f;
    float weightSum = 0.0f;

    [loop]
    for (int y = -radius; y <= radius; ++y)
    {
        [loop]
        for (int x = -radius; x <= radius; ++x)
        {
            if (x == 0 && y == 0) {
                continue;
            }

            float2 offset = float2(x, y);
            float dist = length(offset);
            if (dist > sampleRadiusPixels || dist < 1.0e-4f) {
                continue;
            }

            float2 sampleUv = uv + offset / float2(outWidth, outHeight);
            if (any(sampleUv <= 0.0f) || any(sampleUv >= 1.0f)) {
                continue;
            }

            float sampleDepth = SampleDepthUv(sampleUv);
            float3 sampleNormal = SampleUnitNormalUv(sampleUv);
            float normalSimilarity = dot(centerNormal, sampleNormal);
            if (sampleDepth <= 1.0e-5f || dot(sampleNormal, sampleNormal) <= 1.0e-8f ||
                normalSimilarity <= minSampleNormalCos) {
                continue;
            }

            float planeBlend = saturate((normalSimilarity - planeFitNormalCos) /
                                        max(1.0f - planeFitNormalCos, 1.0e-3f));
            float expectedDepth = lerp(centerDepth,
                                       centerDepth + gradX * offset.x + gradY * offset.y,
                                       planeBlend);
            float delta = sampleDepth - expectedDepth;
            float slopeBias =
                (abs(gradX) + abs(gradY)) * dist * 0.5f;
            float localBias = max(depthBias, slopeBias);
            float inward = saturate((-delta - localBias) / max(aoRadiusMeters, 1.0e-4f));
            float outward = saturate((delta - localBias) / max(aoRadiusMeters, 1.0e-4f));

            float response = 0.0f;
            if (aoMode == 0) {
                response = inward;
            } else if (aoMode == 1) {
                response = outward;
            } else {
                response = max(inward, outward);
            }

            float normalWeight = saturate((normalSimilarity - minSampleNormalCos) /
                                          max(1.0f - minSampleNormalCos, 1.0e-3f));
            normalWeight = lerp(0.35f, 1.0f, normalWeight * normalWeight);
            float edgeBoost = lerp(1.25f, 1.0f, planeBlend);
            float distanceWeight =
                1.0f - saturate(dist / max(sampleRadiusPixels, 1.0f));
            distanceWeight *= distanceWeight;
            float weight = normalWeight * distanceWeight;
            response *= edgeBoost;
            accumulation += response * weight;
            weightSum += weight;
        }
    }

    if (weightSum <= 1.0e-5f) {
        return 1.0f;
    }

    float occlusion = saturate(accumulation / weightSum);
    return saturate(1.0f - occlusion * aoIntensity);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= outWidth || id.y >= outHeight) {
        return;
    }

    float3 hdr = g_hdrInput.Load(int3(id.xy, 0)).rgb;
    hdr *= ComputeDxrTonemapAo(id.xy);
    hdr *= exposure;

    float3 mapped = ToneMapFilmic(hdr);
    mapped = saturate((mapped - 0.5f) * contrast + 0.5f);

    float luma = dot(mapped, float3(0.2126f, 0.7152f, 0.0722f));
    mapped = lerp(luma.xxx, mapped, saturation);

    float2 centerUV = (float2(id.xy) + 0.5f) / float2(outWidth, outHeight);
    float d = distance(centerUV, float2(0.5f, 0.5f));
    float v = smoothstep(0.8f, 0.8f - vignette * 0.5f, d);
    mapped *= v;

    float3 srgb = pow(max(mapped, 0.0f), 1.0f / 2.2f);
    g_out[id.xy] = float4(srgb, 1.0f);
}
