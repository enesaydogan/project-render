Texture2D<float4> g_previewTexture : register(t0);
SamplerState g_linearClampSampler : register(s0);

struct VSOut {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOut VSMain(uint vertexId : SV_VertexID)
{
    float2 positions[3] = {
        float2(-1.0f, -1.0f),
        float2(-1.0f,  3.0f),
        float2( 3.0f, -1.0f)
    };

    VSOut output;
    float2 p = positions[vertexId];
    output.position = float4(p, 0.0f, 1.0f);
    output.uv = float2(p.x * 0.5f + 0.5f, 0.5f - p.y * 0.5f);
    return output;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    return g_previewTexture.Sample(g_linearClampSampler, saturate(input.uv));
}
