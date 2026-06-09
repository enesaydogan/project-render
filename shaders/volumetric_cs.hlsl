// Volumetric ray-march: marches a cooked density volume along the view ray and
// composites the result into the raster HDR color target. The volume occupies a
// unit cube [0,1]^3 in local space; the scene node's transform (worldToLocal)
// places/scales/rotates it. Single scattering toward the global light + ambient.

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
    float4 lightDir;
    float4 lightColor;
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
    float4x4 worldToLocal; // maps world space into the volume's unit cube
    float densityScale;
    float absorption;
    float scatterG;
    float ambient;
    float stepJitter;
    uint  marchSteps;
    float frameSeed;
    uint  lightSteps;
    float3 volumeColor;
    float emissionStrength;
    float3 emissionColor;
    float _materialPad;
    float temperatureMin;
    float temperatureInvRange;
};

Texture3D<float2> VolumeTex : register(t0);
Texture2D<float>  DepthTex   : register(t1);
RWTexture2D<float4> OutputTex : register(u0);
SamplerState linearSampler : register(s0);

void BuildPerspectiveCameraBasis(out float3 projectionForward,
                                 out float3 projectionRight,
                                 out float3 projectionUp,
                                 out float verticalCenterShift)
{
    float3 forwardDir = normalize(forward);
    projectionRight = normalize(cross(forwardDir, up));
    projectionUp = normalize(cross(projectionRight, forwardDir));
    projectionForward = forwardDir;
    verticalCenterShift = 0.0;

    if (verticalTiltCorrection <= 0.5)
        return;

    const float3 worldUp = float3(0.0, 1.0, 0.0);
    float3 levelForward = forwardDir - worldUp * dot(forwardDir, worldUp);
    float levelLengthSq = dot(levelForward, levelForward);
    if (levelLengthSq <= 1.0e-6)
        return;

    projectionForward = levelForward * rsqrt(levelLengthSq);
    projectionRight = normalize(cross(projectionForward, worldUp));
    projectionUp = normalize(cross(projectionRight, projectionForward));
    verticalCenterShift =
        clamp(dot(forwardDir, projectionUp) /
                  max(dot(forwardDir, projectionForward), 0.025),
              -40.0, 40.0);
}

float3 BuildPerspectiveCameraDirection(
    float2 uv, float3 projectionForward, float3 projectionRight,
    float3 projectionUp, float verticalCenterShift)
{
    float2 ndc = uv * 2.0 - 1.0;
    float fInv = tan(radians(fov) * 0.5);
    float xView = ndc.x * aspect * fInv;
    float yView = (-ndc.y) * fInv + verticalCenterShift;
    return normalize(xView * projectionRight + yView * projectionUp +
                     projectionForward);
}

float3 BuildSphericalCameraDirection(float2 uv)
{
    float3 forwardDir = normalize(forward);
    float3 rightDir = normalize(cross(forwardDir, up));
    float3 upDir = normalize(cross(rightDir, forwardDir));
    float azimuth = (uv.x - 0.5) * (2.0 * 3.14159265);
    float elevation = (0.5 - saturate(uv.y)) * 3.14159265;
    float sinAzimuth;
    float cosAzimuth;
    float sinElevation;
    float cosElevation;
    sincos(azimuth, sinAzimuth, cosAzimuth);
    sincos(elevation, sinElevation, cosElevation);
    float3 horizonDir =
        cosAzimuth * forwardDir + sinAzimuth * rightDir;
    return normalize(horizonDir * cosElevation + upDir * sinElevation);
}

// Ray vs unit-cube [0,1]^3. Returns (tNear, tFar); tNear>tFar means miss.
float2 IntersectUnitCube(float3 ro, float3 rd)
{
    float3 inv = 1.0 / rd;
    float3 t0 = (float3(0, 0, 0) - ro) * inv;
    float3 t1 = (float3(1, 1, 1) - ro) * inv;
    float3 tsmall = min(t0, t1);
    float3 tbig = max(t0, t1);
    return float2(max(max(tsmall.x, tsmall.y), tsmall.z),
                  min(min(tbig.x, tbig.y), tbig.z));
}

float HenyeyGreenstein(float cosTheta, float g)
{
    g = clamp(g, -0.95, 0.95);
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosTheta;
    return (1.0 - g2) / (4.0 * 3.14159265 * pow(max(denom, 1e-4), 1.5));
}

float Hash(uint2 p, float seed)
{
    float h = dot(float2(p), float2(12.9898, 78.233)) + seed * 37.17;
    return frac(sin(h) * 43758.5453);
}

float TraceLightTransmittance(float3 localPos, float3 localSunDir,
                              uint steps, float jitter)
{
    float2 hit = IntersectUnitCube(localPos, localSunDir);
    float distanceToExit = hit.y;
    if (distanceToExit <= 1.0e-5)
        return 1.0;

    steps = clamp(steps, 1u, 32u);
    float dt = distanceToExit / float(steps);
    float lightT = (0.5 + 0.5 * jitter) * dt;
    float opticalDepth = 0.0;

    [loop]
    for (uint i = 0; i < steps; ++i)
    {
        float3 uvw = localPos + localSunDir * lightT;
        float density =
            VolumeTex.SampleLevel(linearSampler, saturate(uvw), 0).x *
            densityScale;
        opticalDepth += density * absorption * dt;
        if (opticalDepth >= 12.0)
            return 0.0;
        lightT += dt;
    }
    return exp(-opticalDepth);
}

float3 FireColor(float temperature)
{
    float t = saturate((temperature - temperatureMin) *
                       temperatureInvRange);
    float3 red = float3(1.0, 0.025, 0.001);
    float3 orange = float3(1.0, 0.22, 0.015);
    float3 yellow = float3(1.0, 0.72, 0.12);
    float3 white = float3(1.0, 0.96, 0.82);
    float3 low = lerp(red, orange, smoothstep(0.0, 0.35, t));
    float3 high = lerp(yellow, white, smoothstep(0.70, 1.0, t));
    return lerp(low, high, smoothstep(0.30, 0.78, t));
}

void RenderVolume(uint3 id, bool linearDepthInput)
{
    uint width, height;
    OutputTex.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) return;

    float2 uv = (float2(id.xy) + 0.5) / float2(width, height);
    float sceneDepth = DepthTex.Load(int3(id.xy, 0));

    float3 ro = pos;
    float3 projectionForward;
    float3 projectionRight;
    float3 projectionUp;
    float verticalCenterShift;
    BuildPerspectiveCameraBasis(projectionForward, projectionRight,
                                projectionUp, verticalCenterShift);
    bool spherical = projectionMode >= 0.5;
    float3 rd = spherical
        ? BuildSphericalCameraDirection(uv)
        : BuildPerspectiveCameraDirection(
              uv, projectionForward, projectionRight, projectionUp,
              verticalCenterShift);

    float tScene = 1e30;
    if (linearDepthInput)
    {
        if (sceneDepth < farZ)
        {
            tScene = spherical
                ? sceneDepth
                : sceneDepth /
                      max(dot(rd, projectionForward), 1.0e-6);
        }
    }
    else if (sceneDepth < 1.0)
    {
        // Convert hardware depth back to view-space Z using the same projection
        // equation as pbr_mesh.hlsl, then project that onto this pixel's ray.
        float A = farZ / (farZ - nearZ);
        float B = -nearZ * farZ / (farZ - nearZ);
        float viewZ = B / min(sceneDepth - A, -1.0e-6);
        tScene = viewZ / max(dot(rd, projectionForward), 1.0e-6);
    }

    // Transform the ray into the volume's local (unit-cube) space. The direction
    // is transformed without normalization so t stays in world units.
    float3 lro = mul(worldToLocal, float4(ro, 1.0)).xyz;
    float3 lrd = mul(worldToLocal, float4(rd, 0.0)).xyz;

    float2 t = IntersectUnitCube(lro, lrd);
    float tStart = max(t.x, 0.0);
    float tEnd = min(t.y, tScene);
    if (t.x > t.y || tEnd <= tStart)
        return;

    uint steps = max(marchSteps, 1u);
    float dt = (tEnd - tStart) / float(steps);
    float jitter = stepJitter * Hash(id.xy, frameSeed);
    float marchT = tStart + jitter * dt;

    float3 sunCol = lightColor.rgb * max(lightColor.w, 0.0);
    bool hasSun = dot(lightDir.xyz, lightDir.xyz) > 1.0e-8 &&
                  any(sunCol > 1.0e-8);
    float3 sunDir = hasSun ? normalize(lightDir.xyz) : float3(0.0, 1.0, 0.0);
    float phase =
        hasSun ? HenyeyGreenstein(dot(rd, sunDir), scatterG) : 0.0;
    float3 localSunDir = hasSun
        ? mul(worldToLocal, float4(sunDir, 0.0)).xyz
        : float3(0.0, 0.0, 0.0);

    float transmittance = 1.0;
    float3 scattered = float3(0, 0, 0);
    for (uint i = 0; i < steps; ++i)
    {
        float3 uvw = lro + lrd * marchT; // already in [0,1] cube space
        float2 volumeSample =
            VolumeTex.SampleLevel(linearSampler, saturate(uvw), 0);
        float density = volumeSample.x * densityScale;
        float fireMask = temperatureInvRange > 0.0
            ? saturate((volumeSample.y - temperatureMin) *
                       temperatureInvRange)
            : 0.0;
        if (fireMask > 1.0e-4 && emissionStrength > 0.0)
        {
            float3 fire =
                FireColor(volumeSample.y) * max(emissionColor, 0.0);
            scattered += transmittance * fire *
                         (fireMask * fireMask) * emissionStrength * dt;
        }
        if (density > 1e-4)
        {
            float sigma = density * absorption;
            float aT = exp(-sigma * dt);
            float lightTransmittance = hasSun
                ? TraceLightTransmittance(
                      uvw, localSunDir, lightSteps,
                      Hash(id.xy + uint2(i * 17u, i * 31u), frameSeed))
                : 0.0;
            float3 Lin =
                sunCol * (phase * lightTransmittance) + ambient.xxx;
            float3 source = Lin * max(volumeColor, 0.0);
            scattered += transmittance * source * (1.0 - aT);
            transmittance *= aT;
            if (transmittance < 0.01)
                break;
        }
        marchT += dt;
    }

    float3 sceneColor = OutputTex[id.xy].rgb;
    OutputTex[id.xy] = float4(sceneColor * transmittance + scattered, 1.0);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    RenderVolume(id, false);
}

[numthreads(8, 8, 1)]
void CSMainDXR(uint3 id : SV_DispatchThreadID)
{
    RenderVolume(id, true);
}
