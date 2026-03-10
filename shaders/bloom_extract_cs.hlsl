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
    uint width, height;
    InputTex.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) return;

    float4 color = InputTex[id.xy];
    float brightness = dot(color.rgb, float3(0.2126, 0.7152, 0.0722));
    
    if (brightness > threshold) {
        OutputTex[id.xy] = color * intensity;
    } else {
        OutputTex[id.xy] = float4(0, 0, 0, 0);
    }
}
