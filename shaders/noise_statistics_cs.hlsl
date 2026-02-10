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
    
    // Stride to sample ~1/64th of the image for performance
    const uint stride = 8;
    
    for (uint y = 0; y < height; y += stride)
    {
        for (uint x = 0; x < width; x += stride)
        {
            float4 acc = g_accumulation[uint2(x, y)];
            float n = acc.a;
            
            if (n > 1.0) // Need at least 2 samples for variance
            {
                float accSq = g_variance[uint2(x, y)];
                
                float meanLum = dot(acc.rgb, float3(0.2126, 0.7152, 0.0722)) / n;
                float meanSq = accSq / n;
                
                // Variance = E[x^2] - (E[x])^2
                float var = max(0.0, meanSq - meanLum * meanLum);
                
                // Standard Error of Mean = sqrt(Var / N)
                float sem = sqrt(var / n);
                
                // Coefficient of Variation (Noise) = SEM / Mean
                // Avoid divide by zero
                float noise = sem / (meanLum + 0.001);
                
                totalNoise += noise;
                count += 1.0;
            }
        }
    }
    
    g_output[0] = count > 0.0 ? (totalNoise / count) : 0.0;
}
