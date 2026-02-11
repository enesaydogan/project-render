// #include "raytracing/common.hlsli"

// Using same register space/bindings as might be convenient, but we'll define a specific root signature
// u0: Accumulation (float4)
// u1: Variance (float)
// u2: Output Buffer (float, size 1)
// b0: Constants { width, height }

RWTexture2D<float4> g_accumulation : register(u0);
RWTexture2D<float> g_variance : register(u1);
RWStructuredBuffer<float> g_output : register(u2);

cbuffer Constants : register(b0)
{
    uint width;
    uint height;
    float padding[2];
};

[numthreads(1, 1, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    float totalNoise = 0.0;
    float count = 0.0;
    
    // Stride to sample ~1/16th of the image for better accuracy
    const uint stride = 4;
    
    for (uint y = 0; y < height; y += stride)
    {
        for (uint x = 0; x < width; x += stride)
        {
            float4 acc = g_accumulation[uint2(x, y)];
            float n = acc.a;
            
            if (n > 1.0) // Need at least 2 samples for variance
            {
                float accM2 = g_variance[uint2(x, y)];
                float meanLum = dot(acc.rgb, float3(0.2126, 0.7152, 0.0722)) / n;
                
                // Standard Error of Mean: SEM = sqrt(M2) / N
                float sem = sqrt(max(0.0, accM2)) / n;
                
                // Coefficient of Variation (Noise) = SEM / Mean
                float noise = sem / (max(0.01, meanLum) + 0.001);
                
                // Aggregate as Root Mean Square (RMS) to penalize outliers/noisy patches heavily
                totalNoise += noise * noise;
                count += 1.0;
            }
        }
    }
    
    g_output[0] = count > 0.0 ? sqrt(totalNoise / count) : 0.0;
}
