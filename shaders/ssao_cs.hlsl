#define SAMPLES 16
#define RADIUS 0.5
#define BIAS 0.025

cbuffer CameraCB : register(b0)
{
    float3 pos;
    float debugMode;
    float3 forward;
    float _pad1;
    float3 up;
    float _pad2;
    float fov;
    float aspect;
    float nearZ;
    float farZ;
    float intensity;
    float frameCount;
    float lightCount;
    float maxSpecularBounces;
    float maxRefractiveBounces;
    float maxGIBounces;
    float maxSPP;
    float accumulationCount;

    // Global Lighting
    float4 lightDir; // xyz = direction towards light
    float4 lightColor; // rgb + intensity in .w

    // Alignment
    float3 prevPos;
    float prevValid;
    float3 prevForward;
    float dlssEnabled;
    float3 prevUp;
    float dlssRayReconstruction;
    float prevFov;
    float prevAspect;
    float prevNearZ;
    float prevFarZ;
    float noiseThreshold;
    float useAdaptiveSampling;
    float debugVisualizationMode;
    float cloudRenderingEnabled;
    float iblRotationDegrees;
    float sampleEnvSolidAngle;
    float nrdEnabled;
    float _pad3;
    float4x4 shadowMatrix;
    float4x4 viewProj;
    float4x4 invViewProj;
};

Texture2D<float4> NormalTex : register(t0);
Texture2D<float> DepthTex : register(t1);
RWTexture2D<float> OutputTex : register(u0);

SamplerState linearSampler : register(s0);

float3 GetWorldPos(float2 uv, float depth)
{
    float4 clipPos = float4(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, depth, 1.0);
    float4 worldPos = mul(invViewProj, clipPos);
    return worldPos.xyz / worldPos.w;
}

float3 GetRandom(float2 uv)
{
   // Simple hash
   float f = sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453;
   return normalize(float3(frac(f), frac(f*1.21), frac(f*1.54)) * 2.0 - 1.0);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint width, height;
    OutputTex.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) return;

    float2 uv = (float2(id.xy) + 0.5) / float2(width, height);
    float depth = DepthTex.SampleLevel(linearSampler, uv, 0);
    if (depth >= 1.0) {
        OutputTex[id.xy] = 1.0;
        return;
    }

    float3 worldPos = GetWorldPos(uv, depth);
    float3 N = normalize(NormalTex.SampleLevel(linearSampler, uv, 0).xyz * 2.0 - 1.0);
    float3 rand = GetRandom(uv);
    
    float3 tangent = normalize(rand - N * dot(rand, N));
    float3 bitangent = cross(N, tangent);
    float3x3 TBN = float3x3(tangent, bitangent, N);

    float occlusion = 0.0;
    for (int i = 0; i < SAMPLES; i++)
    {
        // Simple hemispherical sample (could use a kernel)
        float3 sampleDir = GetRandom(uv + float(i) * 0.1);
        if (dot(sampleDir, N) < 0) sampleDir = -sampleDir;
        
        float3 samplePos = worldPos + sampleDir * RADIUS;
        
        float4 clipPos = mul(viewProj, float4(samplePos, 1.0));
        clipPos.xyz /= clipPos.w;
        float2 sampleUV = float2(clipPos.x * 0.5 + 0.5, 0.5 - clipPos.y * 0.5);
        
        sampleUV = saturate(sampleUV);
        
        float sampledDepth = DepthTex.SampleLevel(linearSampler, sampleUV, 0);
        float3 sampledWorldPos = GetWorldPos(sampleUV, sampledDepth);
        
        float dist = distance(pos, samplePos);
        float sampledDist = distance(pos, sampledWorldPos);
        
        if (sampledDist + BIAS < dist && distance(worldPos, sampledWorldPos) < RADIUS * 2.0)
        {
            occlusion += 1.0;
        }
    }

    OutputTex[id.xy] = 1.0 - (occlusion / float(SAMPLES));
}
