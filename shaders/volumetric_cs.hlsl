// Volumetric ray-march: marches a cooked density volume (3D texture) along the
// view ray and composites the result into the raster HDR color target. Single
// scattering toward the global light + ambient fill (no shadow march in v1).

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
    float4 lightDir;   // xyz = direction towards light
    float4 lightColor; // rgb + intensity in .w
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
    float exportRendering;
    float dxrProceduralSkyBoost;
    float iblIndirectBoost;
    float tonemapAoIntensity;
    float tonemapAoRadiusMeters;
    float tonemapAoMode;
    float triPlanarWorldRotationDegrees;
    float dxrFeatureFlags;
    float verticalTiltCorrection;
    float projectionMode;
    float4x4 shadowMatrix;
    float4x4 viewProj;
    float4x4 invViewProj;
};

cbuffer VolumeParams : register(b1)
{
    float3 boundsMin;
    float densityScale;
    float3 boundsMax;
    float absorption;
    float scatterG;
    float ambient;
    float stepJitter;
    uint  marchSteps;
    float frameSeed;
    float3 _vpad;
};

Texture3D<float> DensityTex : register(t0);
Texture2D<float>  DepthTex   : register(t1);
RWTexture2D<float4> OutputTex : register(u0);
SamplerState linearSampler : register(s0);

float3 GetWorldPos(float2 uv, float depth)
{
    float4 clip = float4(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, depth, 1.0);
    float4 w = mul(clip, invViewProj);
    return w.xyz / w.w;
}

// Ray vs AABB slab test. Returns (tNear, tFar); tNear>tFar means miss.
float2 IntersectAABB(float3 ro, float3 rd, float3 bmin, float3 bmax)
{
    float3 inv = 1.0 / rd;
    float3 t0 = (bmin - ro) * inv;
    float3 t1 = (bmax - ro) * inv;
    float3 tsmall = min(t0, t1);
    float3 tbig = max(t0, t1);
    float tn = max(max(tsmall.x, tsmall.y), tsmall.z);
    float tf = min(min(tbig.x, tbig.y), tbig.z);
    return float2(tn, tf);
}

float HenyeyGreenstein(float cosTheta, float g)
{
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosTheta;
    return (1.0 - g2) / (4.0 * 3.14159265 * pow(max(denom, 1e-4), 1.5));
}

float Hash(uint2 p, float seed)
{
    float h = dot(float2(p), float2(12.9898, 78.233)) + seed * 37.17;
    return frac(sin(h) * 43758.5453);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint width, height;
    OutputTex.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) return;

    float2 uv = (float2(id.xy) + 0.5) / float2(width, height);
    float sceneDepth = DepthTex.Load(int3(id.xy, 0));

    float3 ro = pos;
    float3 farPos = GetWorldPos(uv, 1.0);
    float3 rd = normalize(farPos - ro);

    // Distance to opaque scene geometry (limits the march).
    float tScene = 1e30;
    if (sceneDepth < 1.0)
        tScene = length(GetWorldPos(uv, sceneDepth) - ro);

    float2 t = IntersectAABB(ro, rd, boundsMin, boundsMax);
    float tStart = max(t.x, 0.0);
    float tEnd = min(t.y, tScene);
    if (t.x > t.y || tEnd <= tStart)
        return; // ray misses the volume — leave scene color untouched

    uint steps = max(marchSteps, 1u);
    float dt = (tEnd - tStart) / float(steps);
    float jitter = stepJitter * Hash(id.xy, frameSeed);
    float marchT = tStart + jitter * dt;

    float3 sunDir = normalize(lightDir.xyz);
    float3 sunCol = lightColor.rgb * max(lightColor.w, 0.0);
    float phase = HenyeyGreenstein(dot(rd, sunDir), scatterG);
    float3 extent = max(boundsMax - boundsMin, 1e-4);

    float transmittance = 1.0;
    float3 scattered = float3(0, 0, 0);
    for (uint i = 0; i < steps; ++i)
    {
        float3 p = ro + rd * marchT;
        float3 uvw = (p - boundsMin) / extent;
        float density = DensityTex.SampleLevel(linearSampler, uvw, 0) * densityScale;
        if (density > 1e-4)
        {
            float sigma = density * absorption;
            float aT = exp(-sigma * dt);
            float3 Lin = sunCol * phase + ambient.xxx;
            scattered += transmittance * Lin * density * (1.0 - aT);
            transmittance *= aT;
            if (transmittance < 0.01)
                break;
        }
        marchT += dt;
    }

    float3 sceneColor = OutputTex[id.xy].rgb;
    OutputTex[id.xy] = float4(sceneColor * transmittance + scattered, 1.0);
}
