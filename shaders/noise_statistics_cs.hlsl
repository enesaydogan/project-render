// #include "raytracing/common.hlsli"

// Using same register space/bindings as might be convenient, but we'll define a specific root signature
// u0: Accumulation (float4)
// u1: Variance (float)
// u2: Output Buffer (float, size 1)
// b0: Constants { width, height }

RWTexture2D<float4> g_accumulation : register(u0);
RWTexture2D<float> g_variance : register(u1);
// one float per sampled texel; shader writes noise^2 or 0
RWStructuredBuffer<float> g_output : register(u2);

cbuffer Constants : register(b0)
{
    uint width;
    uint height;
    float padding[2];
};

// dispatch a 2D grid; each thread handles one sampled texel (stride steps)
[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    const uint stride = 4;
    uint x = dispatchThreadID.x * stride;
    uint y = dispatchThreadID.y * stride;
    if (x >= width || y >= height)
        return;

    float4 acc = g_accumulation[uint2(x, y)];
    float n = acc.a;
    float result = 0.0;
    if (n > 1.0) {
        float accM2 = g_variance[uint2(x, y)];
        float meanLum = dot(acc.rgb, float3(0.2126, 0.7152, 0.0722)) / n;
        float sem = sqrt(max(0.0, accM2)) / n;
        float noise = sem / (max(0.01, meanLum) + 0.001);
        result = noise * noise;
    }

    // compute flat index into output buffer
    uint gridW = (width + stride - 1) / stride;
    uint idx = dispatchThreadID.y * gridW + dispatchThreadID.x;
    g_output[idx] = result;
}
