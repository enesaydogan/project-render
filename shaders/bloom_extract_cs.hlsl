Texture2D<float4> InputTex : register(t0);
RWTexture2D<float4> OutputTex : register(u0);

cbuffer BloomParams : register(b0)
{
    float threshold;
    float intensity;
    float2 _pad;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint inputWidth, inputHeight;
    uint outputWidth, outputHeight;
    InputTex.GetDimensions(inputWidth, inputHeight);
    OutputTex.GetDimensions(outputWidth, outputHeight);
    if (id.x >= outputWidth || id.y >= outputHeight) return;

    float2 uv = (float2(id.xy) + 0.5) / float2(outputWidth, outputHeight);
    uint2 srcCoord = min(uint2(uv * float2(inputWidth, inputHeight)),
                         uint2(inputWidth - 1, inputHeight - 1));

    float4 color = InputTex[srcCoord];
    float brightness = dot(color.rgb, float3(0.2126, 0.7152, 0.0722));
    
    if (brightness > threshold) {
        OutputTex[id.xy] = color * intensity;
    } else {
        OutputTex[id.xy] = float4(0, 0, 0, 0);
    }
}
