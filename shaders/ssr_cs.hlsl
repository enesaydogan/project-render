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

cbuffer SSRSettings : register(b1)
{
    float stepSize;
    float thickness;
    float ssrIntensity;
    float minSmoothness;
    uint maxSteps;
    float3 _ssrPad;
};

Texture2D<float4> ColorTex : register(t0);
Texture2D<float4> NormalTex : register(t1);
Texture2D<float> DepthTex : register(t2);
RWTexture2D<float4> OutputTex : register(u0);

SamplerState linearSampler : register(s0);

float LoadDepth(uint2 coord)
{
    return DepthTex.Load(int3(coord, 0));
}

float4 LoadColor(uint2 coord)
{
    return ColorTex.Load(int3(coord, 0));
}

float4 LoadNormalData(uint2 coord)
{
    return NormalTex.Load(int3(coord, 0));
}

float3 GetWorldPos(float2 uv, float depth)
{
    float4 clipPos = float4(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, depth, 1.0);
    float4 worldPos = mul(clipPos, invViewProj);
    return worldPos.xyz / worldPos.w;
}

float2 GetUV(float3 worldPos)
{
    float4 clipPos = mul(float4(worldPos, 1.0), viewProj);
    if (clipPos.w <= 1e-5)
    {
        return float2(-1.0, -1.0);
    }
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
    float4 sceneColor = LoadColor(id.xy);
    float4 normalData = LoadNormalData(id.xy);
    float3 N = normalize(normalData.xyz * 2.0 - 1.0);
    float roughness = saturate(normalData.w);
    float smoothness = 1.0 - roughness;
    float depth = LoadDepth(id.xy);

    if (depth >= 1.0) {
        OutputTex[id.xy] = sceneColor;
        return;
    }

    if (smoothness <= minSmoothness)
    {
        OutputTex[id.xy] = sceneColor;
        return;
    }

    float3 worldPos = GetWorldPos(uv, depth);
    float3 V = normalize(worldPos - pos);
    float3 R = reflect(V, N);
    if (dot(R, N) <= 1e-4)
    {
        OutputTex[id.xy] = sceneColor;
        return;
    }

    // Simple ray march
    float3 hitColor = float3(0,0,0);
    float hitWeight = 0.0;
    
    float3 currentPos = worldPos + N * max(thickness, 0.01) * 2.0 + R * max(stepSize, 0.02);
    uint steps = max(maxSteps, 1u);
    for (uint i = 0; i < steps; ++i)
    {
        currentPos += R * stepSize;
        float2 currentUV = GetUV(currentPos);
        
        if (any(currentUV < 0) || any(currentUV > 1)) break;
        
        uint2 sampleCoord = min(uint2(currentUV * float2(width, height)), uint2(width - 1, height - 1));
        float sampledDepth = LoadDepth(sampleCoord);
        if (sampledDepth >= 1.0)
        {
            continue;
        }

        float2 sampleCenterUV = (float2(sampleCoord) + 0.5) / float2(width, height);
        float3 sampledWorldPos = GetWorldPos(sampleCenterUV, sampledDepth);
        float3 toSample = sampledWorldPos - worldPos;
        float alongRay = dot(toSample, R);
        float hitDistance = distance(currentPos, sampledWorldPos);

        if (alongRay > 0.0 && hitDistance < max(thickness, stepSize * 1.5))
        {
            hitColor = LoadColor(sampleCoord).rgb;
            hitWeight = 1.0;
            // Fade only very close to the screen border. A radial center fade
            // suppresses exactly the floor reflections we want near the bottom.
            float2 borderDistance = min(sampleCenterUV, 1.0 - sampleCenterUV);
            float edgeFade = saturate(min(borderDistance.x, borderDistance.y) / 0.03);
            hitWeight *= edgeFade;
            break;
        }
    }

    // The raster path only stores roughness in the GBuffer, so use a stronger
    // practical reflection response here instead of a tiny dielectric-only term.
    float fresnel = pow(1.0 - saturate(dot(N, -V)), 5.0);
    float smoothWeight = saturate((smoothness - minSmoothness) / max(1e-3, 1.0 - minSmoothness));
    float reflectionAmount = ssrIntensity * hitWeight * smoothWeight * lerp(0.35, 1.0, fresnel);
    reflectionAmount = saturate(reflectionAmount);
    
    OutputTex[id.xy] = float4(lerp(sceneColor.rgb, hitColor, reflectionAmount), 1.0);
}
