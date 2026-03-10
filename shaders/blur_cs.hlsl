Texture2D<float4> InputTex : register(t0);
RWTexture2D<float4> OutputTex : register(u0);

cbuffer BlurParams : register(b0)
{
    uint horizontal; // 1 for horizontal, 0 for vertical
    uint width;
    uint height;
    float _pad;
};

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= width || id.y >= height) return;

    float weights[5] = {0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216};
    float4 result = InputTex[id.xy] * weights[0];

    if (horizontal == 1)
    {
        for (int i = 1; i < 5; ++i)
        {
            result += InputTex[uint2(min(id.x + i, width - 1), id.y)] * weights[i];
            result += InputTex[uint2(max((int)id.x - i, 0), id.y)] * weights[i];
        }
    }
    else
    {
        for (int i = 1; i < 5; ++i)
        {
            result += InputTex[uint2(id.x, min(id.y + i, height - 1))] * weights[i];
            result += InputTex[uint2(id.x, max((int)id.y - i, 0))] * weights[i];
        }
    }

    OutputTex[id.xy] = result;
}
