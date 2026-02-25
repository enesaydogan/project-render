// shaders/avg_luminance_cs.hlsl
// Calculates average luminance of a texture and writes to a structured buffer.

Texture2D<float4> g_input : register(t0);
RWStructuredBuffer<float2> g_output : register(u0);

cbuffer Constants : register(b0)
{
    uint width;
    uint height;
    float padding[2];
};

// Use a large stride to reduce the number of pixels we read on the CPU if we do it there.
// Or we can do a proper reduction. For now, let's follow the noise stats approach 
// which uses a stride and then averages on CPU. It's simple and relatively fast.

[numthreads(16, 16, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    const uint stride = 8; // Even larger stride for speed
    uint x = dispatchThreadID.x * stride;
    uint y = dispatchThreadID.y * stride;
    
    if (x >= width || y >= height)
        return;

    float3 color = g_input.Load(int3(x, y, 0)).rgb;
    
    // Log-luminance for geometric mean (log-average).
    // Using max(..., 1e-4) to avoid log(0) and provide a floor for dark pixels.
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
    float logLuminance = log(max(luma, 1e-4f));

    uint gridW = (width + stride - 1) / stride;
    uint idx = dispatchThreadID.y * gridW + dispatchThreadID.x;
    g_output[idx] = float2(logLuminance, luma);
}
