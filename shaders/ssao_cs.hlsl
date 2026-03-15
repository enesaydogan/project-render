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
    float exportRendering;
    float4x4 shadowMatrix;
    float4x4 viewProj;
    float4x4 invViewProj;
};

cbuffer SSAOSettings : register(b1)
{
    float radius;
    float bias;
    float strength;
    uint sampleCount;
};

Texture2D<float4> NormalTex : register(t0);
Texture2D<float> DepthTex : register(t1);
RWTexture2D<float> OutputTex : register(u0);

SamplerState linearSampler : register(s0);

float LoadDepth(uint2 coord)
{
    return DepthTex.Load(int3(coord, 0));
}

float3 LoadNormal(uint2 coord)
{
    return NormalTex.Load(int3(coord, 0)).xyz * 2.0 - 1.0;
}

float3 GetWorldPos(float2 uv, float depth)
{
    float4 clipPos = float4(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, depth, 1.0);
    float4 worldPos = mul(invViewProj, clipPos);
    return worldPos.xyz / worldPos.w;
}

float Hash1(float2 uv)
{
    float f = sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453;
    return frac(f);
}

float2 Hammersley(uint i, uint n)
{
    uint bits = (i << 16u) | (i >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    float radicalInverse = float(bits) * 2.3283064365386963e-10;
    return float2((float(i) + 0.5) / float(n), radicalInverse);
}

float3 SampleHemisphere(float2 xi)
{
    float phi = 6.28318530718 * xi.x;
    float cosTheta = sqrt(1.0 - xi.y);
    float sinTheta = sqrt(xi.y);
    return float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint width, height;
    OutputTex.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) return;

    float2 uv = (float2(id.xy) + 0.5) / float2(width, height);
    float depth = LoadDepth(id.xy);
    if (depth >= 1.0) {
        OutputTex[id.xy] = 1.0;
        return;
    }

    float3 worldPos = GetWorldPos(uv, depth);
    float3 N = normalize(LoadNormal(id.xy));
    float randomAngle = Hash1(float2(id.xy)) * 6.28318530718;
    float2 randomDir = float2(cos(randomAngle), sin(randomAngle));
    
    float3 tangent = normalize(abs(N.z) < 0.999 ? cross(float3(0.0, 0.0, 1.0), N)
                                               : cross(float3(0.0, 1.0, 0.0), N));
    float3 bitangent = cross(N, tangent);
    tangent = tangent * randomDir.x + bitangent * randomDir.y;
    bitangent = cross(N, tangent);
    float3x3 TBN = float3x3(tangent, bitangent, N);

    float occlusion = 0.0;
    float validSampleCount = 0.0;
    uint count = max(sampleCount, 1u);
    for (uint i = 0; i < count; ++i)
    {
        float2 xi = Hammersley(i, count);
        float3 sampleDir = mul(SampleHemisphere(xi), TBN);
        float sampleScale = lerp(0.15, 1.0, xi.x * xi.x);
        
        float3 samplePos = worldPos + sampleDir * (radius * sampleScale);
        
        float4 clipPos = mul(viewProj, float4(samplePos, 1.0));
        if (clipPos.w <= 1e-5)
        {
            continue;
        }

        clipPos.xyz /= clipPos.w;
        float2 sampleUV = float2(clipPos.x * 0.5 + 0.5, 0.5 - clipPos.y * 0.5);

        if (any(sampleUV < 0.0) || any(sampleUV > 1.0))
        {
            continue;
        }

        uint2 sampleCoord = min(uint2(sampleUV * float2(width, height)), uint2(width - 1, height - 1));
        float2 sampleCenterUV = (float2(sampleCoord) + 0.5) / float2(width, height);
        
        float sampledDepth = LoadDepth(sampleCoord);
        if (sampledDepth >= 1.0)
        {
            continue;
        }

        validSampleCount += 1.0;

        float3 sampledWorldPos = GetWorldPos(sampleCenterUV, sampledDepth);
        float3 sampledNormal = normalize(LoadNormal(sampleCoord));

        float expectedDistance = dot(samplePos - worldPos, N);
        float actualDistance = dot(sampledWorldPos - worldPos, N);
        float worldDistance = distance(worldPos, sampledWorldPos);
        float rangeWeight = 1.0 - smoothstep(radius, radius * 1.5, worldDistance);
        float normalWeight = saturate(dot(N, sampledNormal));
        normalWeight *= normalWeight;
        float thickness = expectedDistance - actualDistance;
        float thicknessWeight = 1.0 - smoothstep(bias, max(radius * 0.5, bias + 1e-4), thickness);

        if (actualDistance + bias < expectedDistance)
        {
            occlusion += rangeWeight * normalWeight * thicknessWeight;
        }
    }

    float sampleNorm = max(validSampleCount, 1.0);
    float ao = 1.0 - (occlusion / sampleNorm);
    float2 edgeDistance = min(float2(id.xy) + 0.5, float2(width, height) - (float2(id.xy) + 0.5));
    float edgeFade = saturate(min(edgeDistance.x, edgeDistance.y) / 24.0);
    ao = lerp(1.0, ao, edgeFade);
    OutputTex[id.xy] = saturate(1.0 - (1.0 - ao) * strength);
}
