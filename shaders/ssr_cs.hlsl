#define MAX_STEPS 64
#define STEP_SIZE 0.2
#define THICKNESS 0.1

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

Texture2D<float4> ColorTex : register(t0);
Texture2D<float4> NormalTex : register(t1);
Texture2D<float> DepthTex : register(t2);
RWTexture2D<float4> OutputTex : register(u0);

SamplerState linearSampler : register(s0);

float3 GetWorldPos(float2 uv, float depth)
{
    float4 clipPos = float4(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, depth, 1.0);
    float4 worldPos = mul(invViewProj, clipPos);
    return worldPos.xyz / worldPos.w;
}

float2 GetUV(float3 worldPos)
{
    float4 clipPos = mul(viewProj, float4(worldPos, 1.0));
    clipPos.xyz /= clipPos.w;
    return float2(clipPos.x * 0.5 + 0.5, 0.5 - clipPos.y * 0.5);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint width, height;
    OutputTex.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) return;

    float2 uv = (float2(id.xy) + 0.5) / float2(width, height);
    float4 sceneColor = ColorTex[id.xy];
    float4 normalData = NormalTex.SampleLevel(linearSampler, uv, 0);
    float3 N = normalize(normalData.xyz * 2.0 - 1.0);
    float depth = DepthTex.SampleLevel(linearSampler, uv, 0);

    if (depth >= 1.0) {
        OutputTex[id.xy] = sceneColor;
        return;
    }

    float3 worldPos = GetWorldPos(uv, depth);
    float3 V = normalize(worldPos - pos);
    float3 R = reflect(V, N);

    // Simple ray march
    float3 hitColor = float3(0,0,0);
    float hitWeight = 0.0;
    
    float3 currentPos = worldPos + R * 0.1;
    for (int i = 0; i < MAX_STEPS; i++)
    {
        currentPos += R * STEP_SIZE;
        float2 currentUV = GetUV(currentPos);
        
        if (any(currentUV < 0) || any(currentUV > 1)) break;
        
        float sampledDepth = DepthTex.SampleLevel(linearSampler, currentUV, 0);
        float3 sampledWorldPos = GetWorldPos(currentUV, sampledDepth);
        
        float depthDiff = distance(pos, currentPos) - distance(pos, sampledWorldPos);
        
        if (depthDiff > 0 && depthDiff < THICKNESS)
        {
            hitColor = ColorTex.SampleLevel(linearSampler, currentUV, 0).rgb;
            hitWeight = 1.0;
            // Fade out near edges
            float edgeFade = 1.0 - saturate(length(currentUV - 0.5) * 2.0);
            hitWeight *= edgeFade;
            break;
        }
    }

    // Blend SSR with scene color using a basic fresnel
    float fresnel = pow(1.0 - saturate(dot(N, -V)), 5.0);
    float reflectionAmount = 0.5 * fresnel * hitWeight;
    
    OutputTex[id.xy] = float4(lerp(sceneColor.rgb, hitColor, reflectionAmount), 1.0);
}
