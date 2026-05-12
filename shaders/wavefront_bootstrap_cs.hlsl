#include "raytracing/common.hlsli"

cbuffer WavefrontBootstrapConstants : register(b1)
{
    uint outputWidth;
    uint outputHeight;
    uint backendMode;
    uint maxPathCount;
};

static uint WangHash(uint value)
{
    value = (value ^ 61u) ^ (value >> 16u);
    value *= 9u;
    value = value ^ (value >> 4u);
    value *= 0x27d4eb2du;
    value = value ^ (value >> 15u);
    return value;
}

static const uint kAdaptiveStartSpp = 16u;
static const uint kAdaptiveMinPerPixelSpp = 16u;
static const float kAdaptiveRelScale = 0.90;
static const float kAdaptiveEdgeRelScale = 0.55;
static const float kAdaptiveAbsSemFloor = 5e-4;
static const float kAdaptiveAbsSemScale = 0.0125;
static const float kAdaptiveEdgeAbsSemScale = 0.0055;
static const float kAdaptiveMinKeepProb = 0.10;
static const float kAdaptiveEdgeMinKeepProb = 0.22;
static const float kAdaptiveEdgeContrastThreshold = 0.012;
static const float kAdaptiveMinExpectedRatio = 0.15;
static const float kAdaptiveLagKeepScale = 1.00;

inline void PreserveWavefrontReservoirHistory(uint2 pixel)
{
    const bool flip = (((uint)globalFrameCount) & 1u) == 1u;
    if (flip) {
        g_reservoir1[pixel] = g_reservoir0[pixel];
        g_gi_reservoir_a0[pixel] = g_gi_reservoir_b0[pixel];
        g_gi_reservoir_a1[pixel] = g_gi_reservoir_b1[pixel];
        g_gi_reservoir_a2[pixel] = g_gi_reservoir_b2[pixel];
    } else {
        g_reservoir0[pixel] = g_reservoir1[pixel];
        g_gi_reservoir_b0[pixel] = g_gi_reservoir_a0[pixel];
        g_gi_reservoir_b1[pixel] = g_gi_reservoir_a1[pixel];
        g_gi_reservoir_b2[pixel] = g_gi_reservoir_a2[pixel];
    }
}

inline void WriteWavefrontInactivePixel(uint2 pixel)
{
    float4 acc = g_accumulation[pixel];
    float3 meanColor = (acc.a > 0.0 && all(isfinite(acc.rgb)) &&
                        isfinite(acc.a))
                           ? (acc.rgb / max(acc.a, 1.0))
                           : float3(0.0, 0.0, 0.0);
    g_output[pixel] = float4(meanColor, 0.0);
    PreserveWavefrontReservoirHistory(pixel);
}

inline bool ShouldSkipWavefrontAdaptiveSample(uint2 pixel, uint frame)
{
    const uint accumFrame = (uint)accumulationCount;
    const bool debugViewActive = (debugMode > 0.0) ||
                                 (debugVisualizationMode == 1.0);
    if (debugViewActive || accumFrame <= kAdaptiveStartSpp ||
        useAdaptiveSampling <= 0.5) {
        return false;
    }

    float4 acc = g_accumulation[pixel];
    if (acc.a <= (float)kAdaptiveMinPerPixelSpp ||
        !isfinite(acc.a) || any(!isfinite(acc.rgb))) {
        return false;
    }

    float accM2 = g_variance[pixel];
    float n_acc = acc.a;
    float3 meanColor = acc.rgb / n_acc;
    float meanLum = dot(meanColor, float3(0.2126, 0.7152, 0.0722));
    float sem = sqrt(max(0.0, accM2)) / n_acc;
    float noise = sem / (max(0.01, meanLum) + 0.001);
    float edgeContrast = 0.0;

    if (pixel.x > 0) {
        float4 a = g_accumulation[pixel + uint2(-1, 0)];
        if (a.a > 1.0 && all(isfinite(a.rgb))) {
            edgeContrast = max(edgeContrast,
                               abs(dot(a.rgb / a.a,
                                       float3(0.2126, 0.7152, 0.0722)) -
                                   meanLum));
        }
    }
    if (pixel.x + 1 < outputWidth) {
        float4 a = g_accumulation[pixel + uint2(1, 0)];
        if (a.a > 1.0 && all(isfinite(a.rgb))) {
            edgeContrast = max(edgeContrast,
                               abs(dot(a.rgb / a.a,
                                       float3(0.2126, 0.7152, 0.0722)) -
                                   meanLum));
        }
    }
    if (pixel.y > 0) {
        float4 a = g_accumulation[pixel + uint2(0, -1)];
        if (a.a > 1.0 && all(isfinite(a.rgb))) {
            edgeContrast = max(edgeContrast,
                               abs(dot(a.rgb / a.a,
                                       float3(0.2126, 0.7152, 0.0722)) -
                                   meanLum));
        }
    }
    if (pixel.y + 1 < outputHeight) {
        float4 a = g_accumulation[pixel + uint2(0, 1)];
        if (a.a > 1.0 && all(isfinite(a.rgb))) {
            edgeContrast = max(edgeContrast,
                               abs(dot(a.rgb / a.a,
                                       float3(0.2126, 0.7152, 0.0722)) -
                                   meanLum));
        }
    }

    bool isEdgeRegion = edgeContrast > kAdaptiveEdgeContrastThreshold;
    float relThreshold =
        max(0.001, noiseThreshold *
                         (isEdgeRegion ? kAdaptiveEdgeRelScale
                                       : kAdaptiveRelScale));
    float absSemThreshold =
        max(kAdaptiveAbsSemFloor,
            noiseThreshold * (isEdgeRegion ? kAdaptiveEdgeAbsSemScale
                                           : kAdaptiveAbsSemScale));
    if (noise >= relThreshold && sem >= absSemThreshold) {
        return false;
    }

    if (n_acc < (float)accumFrame * kAdaptiveMinExpectedRatio) {
        return false;
    }
    if (exportRendering > 0.5) {
        return true;
    }

    RNG adaptiveRng =
        init_rng(pixel + uint2(0x9e37u, 0x7f4au), frame ^ 0xA511E9B3u);
    float proximity = min(saturate(noise / relThreshold),
                          saturate(sem / absSemThreshold));
    float keepProb = max(isEdgeRegion ? kAdaptiveEdgeMinKeepProb
                                      : kAdaptiveMinKeepProb,
                         proximity);
    keepProb = max(keepProb,
                   saturate(((float)accumFrame - n_acc) /
                            (float)accumFrame) *
                       kAdaptiveLagKeepScale);
    return next_float(adaptiveRng) > keepProb;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    const uint2 pixel = dispatchThreadID.xy;
    if (pixel.x >= outputWidth || pixel.y >= outputHeight) {
        return;
    }

    const uint pixelIndex = pixel.y * outputWidth + pixel.x;
    const uint totalPathCount = outputWidth * outputHeight;
    const uint clampedPathCount = min(totalPathCount, maxPathCount);
    const bool optimizedBackend = backendMode == WAVEFRONT_BACKEND_OPTIMIZED;

    if (pixelIndex == 0) {
        const uint groupCount = (clampedPathCount + 63u) / 64u;

        g_wavefrontDispatchArgs[0].groupCountX = groupCount;
        g_wavefrontDispatchArgs[0].groupCountY = 1u;
        g_wavefrontDispatchArgs[0].groupCountZ = 1u;
        g_wavefrontDispatchArgs[0].activeCount = clampedPathCount;

        g_wavefrontStats[0] = outputWidth;
        g_wavefrontStats[1] = outputHeight;
        if (!optimizedBackend) {
            g_wavefrontStats[2] = clampedPathCount;
        }
        g_wavefrontStats[3] = backendMode;
        g_wavefrontStats[4] = asuint(globalFrameCount);
        g_wavefrontStats[5] = asuint(accumulationCount);
        g_wavefrontStats[15] = totalPathCount - clampedPathCount;
        g_wavefrontStats[50] = WAVEFRONT_ABI_VERSION;
        if (!optimizedBackend) {
            g_wavefrontQueueCounters[WAVEFRONT_QUEUE_PATH_A] = clampedPathCount;
        }
    }

    if (!optimizedBackend && pixelIndex >= clampedPathCount) {
        return;
    }

    if (optimizedBackend &&
        ShouldSkipWavefrontAdaptiveSample(pixel, (uint)globalFrameCount)) {
        WriteWavefrontInactivePixel(pixel);
        uint skippedPrevious = 0u;
        InterlockedAdd(g_wavefrontStats[51], 1u, skippedPrevious);
        return;
    }

    uint queueIndex = pixelIndex;
    if (optimizedBackend) {
        InterlockedAdd(g_wavefrontQueueCounters[WAVEFRONT_QUEUE_PATH_A], 1u,
                       queueIndex);
        if (queueIndex >= clampedPathCount) {
            WriteWavefrontInactivePixel(pixel);
            uint overflowPrevious = 0u;
            InterlockedAdd(g_wavefrontStats[52], 1u, overflowPrevious);
            return;
        }
        uint activePrevious = 0u;
        InterlockedAdd(g_wavefrontStats[2], 1u, activePrevious);
    }

    const float2 screenDim = float2(outputWidth, outputHeight);
    const float2 uv = (float2(pixel) + 0.5 + float2(jitterX, jitterY)) / screenDim;
    const float2 ndc = uv * 2.0 - 1.0;

    const float fInv = tan(radians(fov) * 0.5);
    const float3 forwardDir = normalize(camForward);
    const float3 rightDir = normalize(cross(forwardDir, camUp));
    const float3 upDir = normalize(cross(rightDir, forwardDir));

    const float yView = (-ndc.y) * fInv;
    const float xView = ndc.x * aspect * fInv;

    WavefrontPathState state;
    state.origin = camPos;
    state.direction = normalize(xView * rightDir + yView * upDir + forwardDir);
    state.pixelIndex = pixelIndex;
    state.rngState = WangHash(pixelIndex ^ asuint(globalFrameCount) ^ 0x9E3779B9u);
    state.throughput = float3(1.0, 1.0, 1.0);
    state.packedState = RAY_TYPE_PRIMARY;

    g_wavefrontPathQueueA[queueIndex] = state;
}
