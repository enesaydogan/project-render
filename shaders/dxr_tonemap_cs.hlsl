Texture2D<float4> g_hdrInput : register(t0);
Texture2D<float> g_depthTexture : register(t1);
Texture2D<float4> g_normalRoughnessTexture : register(t2);
RWTexture2D<float4> g_out : register(u0);

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
    uint2 depthCoord = min(uint2(uv * float2(depthWidth, depthHeight)),
                           uint2(depthWidth - 1, depthHeight - 1));
    uint2 normalCoord = min(uint2(uv * float2(normalWidth, normalHeight)),
                            uint2(normalWidth - 1, normalHeight - 1));

    float centerDepth = g_depthTexture.Load(int3(depthCoord, 0));
    float3 centerNormal = g_normalRoughnessTexture.Load(int3(normalCoord, 0)).xyz;
    float centerNormalLen = length(centerNormal);
    if (centerDepth <= 1.0e-5f || centerNormalLen <= 1.0e-5f) {
        return 1.0f;
    }
    centerNormal /= centerNormalLen;

    float sampleRadiusPixels = clamp((aoRadiusMeters / max(centerDepth, 0.1f)) *
                                         180.0f,
                                     1.0f, 48.0f);
    int radius = (int)ceil(sampleRadiusPixels);
    float depthBias = max(aoRadiusMeters * 0.05f, 0.001f);
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

            uint2 sampleDepthCoord = min(uint2(sampleUv * float2(depthWidth, depthHeight)),
                                         uint2(depthWidth - 1, depthHeight - 1));
            uint2 sampleNormalCoord = min(uint2(sampleUv * float2(normalWidth, normalHeight)),
                                          uint2(normalWidth - 1, normalHeight - 1));

            float sampleDepth = g_depthTexture.Load(int3(sampleDepthCoord, 0));
            float3 sampleNormal = g_normalRoughnessTexture.Load(int3(sampleNormalCoord, 0)).xyz;
            float sampleNormalLen = length(sampleNormal);
            if (sampleDepth <= 1.0e-5f || sampleNormalLen <= 1.0e-5f) {
                continue;
            }
            sampleNormal /= sampleNormalLen;

            float delta = sampleDepth - centerDepth;
            float inward = saturate((-delta - depthBias) / max(aoRadiusMeters, 1.0e-4f));
            float outward = saturate((delta - depthBias) / max(aoRadiusMeters, 1.0e-4f));

            float response = 0.0f;
            if (aoMode == 0) {
                response = inward;
            } else if (aoMode == 1) {
                response = outward;
            } else {
                response = max(inward, outward);
            }

            float normalWeight = saturate(dot(centerNormal, sampleNormal));
            float distanceWeight = 1.0f - saturate(dist / max(sampleRadiusPixels, 1.0f));
            float weight = normalWeight * distanceWeight;
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
