#ifndef CLOUDS_HLSL
#define CLOUDS_HLSL

struct CloudParams {
    float density;
    float absorption;
    float coverage;
    float scattering;
    int steps;
    float sunIntensity;
    float cloudTop;
    float cloudBottom;
    float windSpeed;

    float baseScale;
    float detailScale;
    float coverageScale;
    float coverageVariation;
    float erosion;
    float warpStrength;
    float shapePower;
    float powderStrength;
    float cirrusAmount;
    float cloudShadowStrength;
    float _pad0;
    float _pad1;

    int shadowSteps;
    float shadowStepSize;
    float shadowLod;

    int maxSteps;
    float verticalStepMeters;
    int shadowEvery;
    float shadowDensityThreshold;

    float timeSeconds;

    int previewBakeSamples;
    int finalBakeSamples;
    float bakeJitterStrength;
    float multiScatterBoost;

    float silverLiningStrength;
    float cloudType;
    float groundBounceStrength;
    float shadowSoftness;

    float3 _pad;
};

// Bindings: Register Space 2 to avoid collision with standard descriptors
ConstantBuffer<CloudParams> CloudCB : register(b10, space2);
Texture3D<float4> NoiseTex : register(t10, space2);
Texture3D<float4> DetailTex : register(t11, space2);
SamplerState LinearWrapSampler : register(s0, space2);

#ifndef CLOUDS_PI
static const float CLOUDS_PI = 3.14159265f;
#endif

// Cloud absorption parameter is authored in artist-friendly UI units.
// Normalize to physical-scene march units so typical values (0.2..1.0)
// remain usable after switching to physical lighting.
static const float CLOUD_ABSORPTION_SCALE = 0.01f;
static const float CLOUD_SKY_RADIANCE_SCALE = 0.18f;
static const float CLOUD_SUN_ILLUMINANCE_SCALE = 0.0028f;
static const float CLOUD_MS_SUN_SCALE = 0.0012f;
static const float CLOUD_SHADOW_ABSORPTION_SCALE = 0.0065f;

// Dual Henyey-Greenstein for realistic cloud scattering
// (Forward peak + slight backward peak)
float PhaseHG(float cosTheta, float g) {
    float g2 = g * g;
    float fwd = (1.0f - g2) / (4.0f * CLOUDS_PI * pow(1.0f + g2 - 2.0f * g * cosTheta, 1.5f));
    
    // Mix with backward scattering for silver lining and fuller look
    float gBack = -0.2f;
    float g2Back = gBack * gBack;
    float back = (1.0f - g2Back) / (4.0f * CLOUDS_PI * pow(1.0f + g2Back - 2.0f * gBack * cosTheta, 1.5f));
    
    float backMix = lerp(0.30f, 0.48f, saturate(CloudCB.silverLiningStrength));
    return lerp(fwd, back, backMix);
}

float Remap(float x, float a, float b, float c, float d) {
    return c + (saturate((x - a) / (b - a))) * (d - c);
}

float CloudHash12(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float2 CloudHash22(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * float3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.xx + p3.yz) * p3.zy);
}

float CloudValueNoise2D(float2 p)
{
    float2 cell = floor(p);
    float2 fracPart = frac(p);
    float2 smooth = fracPart * fracPart * (3.0 - 2.0 * fracPart);

    float a = CloudHash12(cell);
    float b = CloudHash12(cell + float2(1.0, 0.0));
    float c = CloudHash12(cell + float2(0.0, 1.0));
    float d = CloudHash12(cell + float2(1.0, 1.0));

    return lerp(lerp(a, b, smooth.x), lerp(c, d, smooth.x), smooth.y);
}

float CloudWeatherNoise2D(float2 p)
{
    float value = 0.0;
    float weight = 0.55;
    float totalWeight = 0.0;
    float2 q = p;

    [unroll]
    for (int octave = 0; octave < 4; ++octave) {
        value += CloudValueNoise2D(q) * weight;
        totalWeight += weight;
        q = q * 2.07 + float2(19.31, 7.17);
        weight *= 0.5;
    }

    return (totalWeight > 1e-5) ? (value / totalWeight) : 0.0;
}

float CloudCellularCumulus(float2 uv, float radiusScale)
{
    float2 cell = floor(uv);
    float2 f = frac(uv);
    float result = 0.0;

    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            float2 offset = float2((float)x, (float)y);
            float2 rnd = CloudHash22(cell + offset);
            float2 center = offset + lerp(float2(0.18f, 0.18f), float2(0.82f, 0.82f), rnd);
            float d = length(f - center);
            float radius = lerp(0.28f, 0.58f, rnd.x) * radiusScale;
            float softness = radius * lerp(0.45f, 0.72f, rnd.y);
            float blob = smoothstep(radius, max(0.001f, radius - softness), d);
            result = max(result, blob * lerp(0.65f, 1.0f, rnd.y));
        }
    }

    return saturate(result);
}

float2 CloudNonPeriodicOffset(float2 uv)
{
    float x = CloudWeatherNoise2D(uv + float2(17.7f, -4.2f));
    float y = CloudWeatherNoise2D(uv * 1.37f + float2(-8.1f, 21.9f));
    return float2(x, y) - 0.5f;
}

// Small hash for jitter (works in raytracing shaders when common.hlsli is present)
float InterleavedGradientNoise(uint2 pix, float frame) {
    // https://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare
    // (classic interleaved gradient noise variant)
    float3 magic = float3(0.06711056, 0.00583715, 52.9829189);
    float v = dot(float3(pix, frame), magic);
    return frac(magic.z * frac(v));
}

float3 SampleNoise(float3 uvw, float lod) {
    float4 n = NoiseTex.SampleLevel(LinearWrapSampler, uvw, lod);
    return n.rgb;
}

float SampleWarp(float3 uvw, float lod) {
    return NoiseTex.SampleLevel(LinearWrapSampler, uvw, lod).a;
}

// Breaking grid alignment
float3 RotateDomain(float3 p) {
    // 45 degrees
    float c = 0.70710678; 
    float s = 0.70710678;
    return float3(p.x * c + p.z * s, p.y, p.z * c - p.x * s);
}

// Improved height profile for realistic cloud shapes
float HeightGradient(float h) {
   // Remap 0..1 to shape
   // Bottom fade (sharp)
   float bottom = smoothstep(0.0, 0.05, h);
   // Top fade (round/anvil)
   float top = smoothstep(0.95, 0.65, h);
   return bottom * top; 
}

// Stable Ray-Sphere Intersection
// Returns float2(tNear, tFar). If no hit, returns float2(-1, -1).
float2 RaySphereIntersect(float3 ro, float3 rd, float3 sphereCenter, float sphereRadius) {
    float3 oc = ro - sphereCenter;
    float b = dot(oc, rd);
    
    // c = (oc^2 - R^2). Use (a-b)(a+b) for stability at large scales.
    float dist = length(oc);
    float c = (dist - sphereRadius) * (dist + sphereRadius);
    
    float h = b * b - c;
    if (h < 0.0) return float2(-1.0, -1.0); // No hit
    h = sqrt(h);
    return float2(-b - h, -b + h);
}

static const float EARTH_RADIUS = 6360000.0;
static const float3 PLANET_CENTER = float3(0.0, -EARTH_RADIUS, 0.0);

float SampleDensity(float3 p, float lod) {
    float layerTop = max(CloudCB.cloudTop, CloudCB.cloudBottom + 10.0f);
    // Stable Spherical Altitude Calculation
    float3 relP = p - PLANET_CENTER;
    float distToCenter = length(relP);
    
    // Accurate height Above ground: h = sqrt(p.x^2 + (p.y+R)^2 + p.z^2) - R
    // Using (h+R)^2 = p.x^2 + p.y^2 + 2p.yR + R^2 + R^2
    // h = (p.x^2 + p.y^2 + 2.0*p.y*R) / (dist + R)
    float heightAboveGround = (dot(p, p) + 2.0 * p.y * EARTH_RADIUS) / (distToCenter + EARTH_RADIUS);
    
    // Check bounds
    if (heightAboveGround < CloudCB.cloudBottom || heightAboveGround > layerTop) return 0.0;
    
    float heightPct = saturate((heightAboveGround - CloudCB.cloudBottom) / (layerTop - CloudCB.cloudBottom));

    float windMeters = CloudCB.timeSeconds * CloudCB.windSpeed;
    float2 advectedXZ = p.xz + windMeters * float2(32.0f, 11.0f);
    float coverageScale = max(CloudCB.coverageScale, 1.0e-6f);

    // Non-periodic world-space placement: broad weather, cellular cloud groups,
    // and smaller puffs. The tiled 3D textures below should only supply local
    // volume detail; this field owns where cloud islands exist.
    float2 macroUv = advectedXZ * coverageScale;
    float2 macroUvA = float2(macroUv.x * 0.82f - macroUv.y * 0.57f,
                             macroUv.x * 0.57f + macroUv.y * 0.82f);
    float2 macroUvB = float2(macroUv.x * 1.13f + macroUv.y * 0.41f,
                            -macroUv.x * 0.41f + macroUv.y * 1.13f);
    float weatherNoiseA = CloudWeatherNoise2D(macroUvA + float2(11.7f, 3.1f));
    float weatherNoiseB = CloudWeatherNoise2D(macroUvB * 0.53f + float2(-5.2f, 8.4f));
    float weatherNoise = lerp(weatherNoiseA, weatherNoiseB, 0.35f);
    float broadFill = CloudWeatherNoise2D(macroUv * 0.28f + float2(-31.4f, 6.8f));
    float largeCells = CloudCellularCumulus(macroUv * 0.52f + float2(2.7f, -9.1f), 0.92f);
    float midCells = CloudCellularCumulus(macroUv * 1.24f + float2(-5.4f, 13.2f), 0.78f);
    float tinyCells = CloudCellularCumulus(macroUv * 2.60f + float2(19.5f, 4.4f), 0.58f);
    float variety = saturate(CloudCB.coverageVariation);
    float cellField = saturate(max(largeCells, midCells * lerp(0.28f, 0.58f, variety)) +
                               tinyCells * lerp(0.02f, 0.16f, variety));
    float macroField = saturate(cellField * lerp(0.66f, 1.05f, weatherNoise) +
                                weatherNoise * lerp(0.02f, 0.10f, variety));

    // 1. Base Coordinates with decorrelated local offsets to break tile tracking.
    float2 localOffset = CloudNonPeriodicOffset(macroUv * 0.31f);
    float3 basePos = p * CloudCB.baseScale;
    basePos.xz += windMeters * float2(0.0010f, 0.0005f);
    basePos.xz += localOffset * 0.42f;
    basePos.y += dot(localOffset, float2(0.21f, -0.12f));
    
    // Domain Warp (Low Frequency)
    float3 warpPos = RotateDomain(basePos) * 0.5f;
    float warp = SampleWarp(warpPos, lod);
    basePos += (warp - 0.5f) * CloudCB.warpStrength * 1.5f;

    // 2. Base Shape (Perlin-Worley)
    // Rotate sampling to avoid axis streaks
    float3 noiseCoord = RotateDomain(basePos);
    float3 altNoiseCoord = RotateDomain(basePos * float3(1.73f, 1.11f, 1.47f) +
                                        float3(0.37f, 0.19f, 0.71f));
    // Base R/G are channel-packed Perlin-Worley variants. Blend two transformed
    // samples so the underlying 3D tile does not read as a repeated stamp.
    float baseA = NoiseTex.SampleLevel(LinearWrapSampler, noiseCoord, lod).r;
    float baseB = NoiseTex.SampleLevel(LinearWrapSampler, altNoiseCoord, lod + 0.65f).g;
    float baseCloud = saturate(baseA * 0.78f + baseB * 0.28f);
    
    // 3. Density Gradient + Cloud Type Profile
    float heightGrad = HeightGradient(heightPct);

    // Spatial cloud type field: 0 = puffy cumulus, 1 = flatter stratiform.
    float typeNoise = NoiseTex.SampleLevel(
        LinearWrapSampler,
        noiseCoord * 0.23f + float3(0.0f, CloudCB.timeSeconds * 0.0002f, 0.0f),
        lod + 2.0f).g;
    float authoredType = saturate(CloudCB.cloudType);
    float overcastBlend = saturate(CloudCB.cloudType - 1.0f);
    float typeBlend = saturate(CloudCB.coverageVariation) * smoothstep(0.52f, 0.92f, typeNoise);
    typeBlend = saturate(max(typeBlend, authoredType * 0.82f));

    float cumulusProfile = pow(saturate(heightGrad), max(0.35f, CloudCB.shapePower));
    float stratusProfile = smoothstep(0.0f, 0.08f, heightPct) * smoothstep(0.68f, 0.26f, heightPct);
    float overcastProfile = smoothstep(0.0f, 0.04f, heightPct) * smoothstep(0.96f, 0.54f, heightPct);
    float verticalProfile = lerp(cumulusProfile, stratusProfile, typeBlend * 0.35f);
    verticalProfile = lerp(verticalProfile, overcastProfile, overcastBlend);
    baseCloud *= verticalProfile;

    // Shape curve: keep cumulus a bit punchier, stratus a bit softer.
    float shapeCurve = lerp(1.05f, 0.82f, typeBlend);
    baseCloud = pow(saturate(baseCloud), shapeCurve);

    // 4. Coverage
    // Apply coverage by eroding the density signal
    
    // Coverage should map naturally from clear sky (0) to overcast (1).
    float effectiveCoverage = saturate(CloudCB.coverage);
    // Perceptual remap: make low-mid slider values produce visible cloud amount.
    float coverageLinear = pow(effectiveCoverage, 0.65f);
    float highCoverage = smoothstep(0.58f, 0.96f, effectiveCoverage);
    // High threshold at low coverage removes most clouds; low threshold at high
    // coverage produces overcast-like fill.
    float densityThreshold = lerp(0.84f, 0.045f, coverageLinear);
    densityThreshold = lerp(densityThreshold, 0.025f, overcastBlend);

    // Standard Schneider remap:
    float macroMask = smoothstep(lerp(0.58f, 0.24f, coverageLinear),
                                 lerp(0.80f, 0.44f, coverageLinear),
                                 macroField);
    float highCoverageMask = smoothstep(lerp(0.62f, 0.16f, coverageLinear),
                                        lerp(0.82f, 0.34f, coverageLinear),
                                        saturate(0.62f * weatherNoise + 0.38f * broadFill));
    macroMask = saturate(max(macroMask, highCoverageMask * max(highCoverage, overcastBlend)));
    float cloudSignal = baseCloud * lerp(0.04f, lerp(1.05f, 1.48f, highCoverage), macroMask);
    float covRemap = Remap(cloudSignal, densityThreshold, 1.0f, 0.0f, 1.0f);
    baseCloud = covRemap; 
    baseCloud *= lerp(0.0f, 1.0f, macroMask);

    // 6. Detail Erosion (High Frequency)
    if (baseCloud > 0.0) {
        float3 detailPos = p * CloudCB.detailScale;
        detailPos.xz += windMeters * 0.002f;
        detailPos.xz += localOffset * 0.74f;
        // Rotate detail too
        detailPos = RotateDomain(detailPos);
        
        float3 detailNoise = DetailTex.SampleLevel(LinearWrapSampler, detailPos, lod).rgb;
        float3 detailNoiseB = DetailTex.SampleLevel(
            LinearWrapSampler,
            RotateDomain(detailPos * float3(1.91f, 1.27f, 1.53f) + float3(0.23f, 0.67f, 0.41f)),
            lod + 0.85f).rgb;
        float highFreqFBM = dot(detailNoise, float3(0.55f, 0.28f, 0.17f));
        highFreqFBM = lerp(highFreqFBM, dot(detailNoiseB, float3(0.45f, 0.35f, 0.20f)), 0.42f);
        
        // Directional erosion adds wind-sheared edge complexity.
        float modifier = lerp(highFreqFBM, 1.0 - highFreqFBM, saturate(heightPct * 5.0));
        float windPhase = 0.5f + 0.5f * sin(dot(detailPos.xz, float2(0.11f, 0.07f)) + CloudCB.timeSeconds * CloudCB.windSpeed * 0.05f);
        float directionalErosion = lerp(highFreqFBM, modifier, windPhase);
        
        // Remap density based on detail
        float erosion = CloudCB.erosion * lerp(0.36f, 0.72f, 1.0f - typeBlend);
        erosion *= lerp(1.0f, 0.58f, overcastBlend);
        baseCloud = Remap(baseCloud, directionalErosion * erosion, 1.0, 0.0, 1.0);
    }
    
    return saturate(baseCloud) * CloudCB.density;
}

float CloudSunTransmittance(float3 worldPos, float3 sunDir)
{
    if (CloudCB.density <= 0.001f ||
        CloudCB.coverage <= 0.001f ||
        CloudCB.cloudShadowStrength <= 0.001f) {
        return 1.0f;
    }

    sunDir = normalize(sunDir);
    float innerRadius = EARTH_RADIUS + CloudCB.cloudBottom;
    float outerRadius = EARTH_RADIUS + max(CloudCB.cloudTop, CloudCB.cloudBottom + 10.0f);
    float distToCenter = length(worldPos - PLANET_CENTER);
    float2 hitInner = RaySphereIntersect(worldPos, sunDir, PLANET_CENTER, innerRadius);
    float2 hitOuter = RaySphereIntersect(worldPos, sunDir, PLANET_CENTER, outerRadius);

    float tStart = 0.0f;
    float tEnd = 0.0f;

    if (distToCenter < innerRadius) {
        if (hitInner.y < 0.0f || hitOuter.y < 0.0f) {
            return 1.0f;
        }
        tStart = max(hitInner.y, 0.0f);
        tEnd = max(hitOuter.y, 0.0f);
    } else if (distToCenter < outerRadius) {
        float dInner = (hitInner.x > 0.0f) ? hitInner.x : ((hitInner.y > 0.0f) ? hitInner.y : 1e9f);
        float dOuter = (hitOuter.y > 0.0f) ? hitOuter.y : 1e9f;
        tStart = 0.0f;
        tEnd = min(dInner, dOuter);
    } else {
        if (hitOuter.x < 0.0f) {
            return 1.0f;
        }
        tStart = max(hitOuter.x, 0.0f);
        tEnd = max(hitOuter.y, 0.0f);
    }

    if (tEnd <= tStart) {
        return 1.0f;
    }

    int steps = clamp(CloudCB.shadowSteps, 4, 24);
    float thickness = min(tEnd - tStart, max(CloudCB.shadowStepSize * (float)steps, 1.0f));
    float stepSize = thickness / (float)steps;
    float jitter = CloudHash12(worldPos.xz * 0.037f + worldPos.yy * 0.011f);
    float3 p = worldPos + sunDir * (tStart + stepSize * jitter);

    float densitySum = 0.0f;
    float lod = max(CloudCB.shadowLod, 1.0f);
    [loop]
    for (int i = 0; i < steps; ++i) {
        densitySum += SampleDensity(p, lod);
        p += sunDir * stepSize;
    }

    float opticalDepth = densitySum * stepSize *
                         CloudCB.absorption *
                         CLOUD_SHADOW_ABSORPTION_SCALE *
                         CloudCB.cloudShadowStrength;
    float tr = exp(-opticalDepth);
    tr = pow(saturate(tr), lerp(1.0f, 0.68f, saturate(CloudCB.shadowSoftness)));
    return lerp(1.0f, tr, saturate(CloudCB.cloudShadowStrength));
}

float4 EvaluateCirrusLayer(float3 rayOrigin, float3 rayDir, float3 sunDir, float3 sunIlluminance,
                           float3 skyZenith, float3 skyHorizon)
{
    float amount = saturate(CloudCB.cirrusAmount);
    if (amount <= 0.001f || rayDir.y <= 0.045f) {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    const float cirrusHeight = 8200.0f;
    float t = (cirrusHeight - rayOrigin.y) / rayDir.y;
    if (t <= 0.0f || t > 200000.0f) {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    float3 p = rayOrigin + rayDir * t;
    float2 wind = float2(CloudCB.timeSeconds * CloudCB.windSpeed * 0.000035f,
                         CloudCB.timeSeconds * CloudCB.windSpeed * 0.000011f);
    float2 uv = p.xz * 0.000043f + wind;
    float2 streakUv = float2(uv.x * 0.28f + uv.y * 0.04f, uv.y * 3.60f);
    float longStreaks = CloudWeatherNoise2D(streakUv + float2(21.3f, -5.7f));
    float feather = CloudWeatherNoise2D(streakUv * 4.0f + float2(-9.1f, 14.6f));
    float breakup = CloudWeatherNoise2D(uv * 13.0f + float2(3.2f, 18.4f));

    float wisps = smoothstep(0.54f, 0.88f, longStreaks) *
                  smoothstep(0.24f, 0.78f, feather) *
                  lerp(0.55f, 1.0f, breakup);
    float viewFade = smoothstep(0.08f, 0.35f, rayDir.y);
    float opacity = saturate(wisps * amount * 0.13f * viewFade);

    float cosAngle = dot(rayDir, sunDir);
    float phase = PhaseHG(cosAngle, 0.72f);
    float silver = pow(saturate(cosAngle), 8.0f) * saturate(CloudCB.silverLiningStrength);
    float3 skyTint = lerp(skyHorizon, skyZenith, saturate(rayDir.y));
    float3 sunTint = sunIlluminance * (CLOUD_MS_SUN_SCALE * 0.42f);
    float3 color = lerp(skyTint * 0.55f, sunTint, saturate(phase * 5.0f + silver)) * opacity;
    return float4(color, 1.0f - opacity);
}

// Raymarch function returning accumulated cloud color (rgb) and transmittance (a)
// tMin/tMax: Intersection distance with cloud shell
float4 RaymarchClouds(float3 rayOrigin, float3 rayDir, float tMin, float tMax, float3 sunDir, float3 lightColor, uint rayType, uint rayDepth) {
    // 1. Setup Intersection for Spherical Shell
    float innerRadius = EARTH_RADIUS + CloudCB.cloudBottom;
    float outerRadius = EARTH_RADIUS + max(CloudCB.cloudTop, CloudCB.cloudBottom + 10.0f);
    
    float2 hitInner = RaySphereIntersect(rayOrigin, rayDir, PLANET_CENTER, innerRadius);
    float2 hitOuter = RaySphereIntersect(rayOrigin, rayDir, PLANET_CENTER, outerRadius);

    float tStart = 0.0;
    float tEnd = 0.0;
    // Determine the march interval
    // Case 1: Camera below cloud layer (Ground view)
    float distToCenter = length(rayOrigin - PLANET_CENTER);
    if (distToCenter < innerRadius) {
         // Ray must hit inner sphere (exit) and outer sphere (exit) to see clouds?
         // No, looking up from ground:
         // Ray usually enters layer at hitInner.y (far hit of inner sphere?)
         // Wait, if inside, 'far hit' is the boundary in front of us. 'near hit' is behind us.
         // Let's assume t > 0.
         
         // Looking up: Ray hits inner shell at tInner (enter clouds), then hits outer shell at tOuter (exit clouds).
         // hitInner.y should be the positive intersection (since we are inside, one is neg, one is pos).
         tStart = max(0.0, hitInner.y);
         tEnd = max(0.0, hitOuter.y);
    } 
    // Case 2: Camera inside cloud layer
    else if (distToCenter < outerRadius) {
        // We are inside the shell.
        // tStart is 0.
        // tEnd is min(dist to inner sphere, dist to outer sphere).
        // If we look down, we hit inner sphere. If we look up, we hit outer sphere.
        tStart = 0.0;
        float dInner = (hitInner.x > 0) ? hitInner.x : ((hitInner.y > 0) ? hitInner.y : 1e9);
        float dOuter = (hitOuter.y > 0) ? hitOuter.y : 1e9;
        tEnd = min(dInner, dOuter);
    }
    // Case 3: Camera above clouds (Space) - not handled, assuming ground/air
    else {
        // Outside looking down
        tStart = hitOuter.x;
        tEnd = max(hitInner.x > 0 ? hitInner.x : hitOuter.y, 0.0);
        // ... complex logic, simplified for now
        if (hitOuter.x < 0) return float4(0,0,0,1);
        tEnd = hitOuter.y; // Simplified
    }
    
    // Clip by scene depth (tMax)
    // tStart = max(tStart, max(0.0f, tMin)); // Don't clip start by tMin if tMin is scene geometry? tMin usually 0 or NearZ
    // tMax comes from depth buffer.
    tEnd = min(tEnd, (tMax > 0.0) ? tMax : 100000.0);
    tEnd = min(tEnd, tStart + lerp(45000.0f, 100000.0f, smoothstep(0.06f, 0.28f, abs(rayDir.y))));
    
    if (tEnd <= tStart) return float4(0,0,0,1);

    float thickness = tEnd - tStart;
    
    // Adaptive stepping
    float layerTop = max(CloudCB.cloudTop, CloudCB.cloudBottom + 10.0f);
    float verticalThickness = max(1.0f, layerTop - CloudCB.cloudBottom);
    float verticalStepMeters = max(1.0f, CloudCB.verticalStepMeters);
    // Rough approx of vertical component for step count
    float rayDirYAbs = max(0.05f, abs(rayDir.y)); 
    
    // Smooth stepping logic (preserved from fix)
    float targetStepsF = (verticalThickness / verticalStepMeters) / rayDirYAbs;
    float smoothSteps = clamp(max((float)CloudCB.steps, targetStepsF), 10.0f, (float)CloudCB.maxSteps);
    int steps = (int)ceil(smoothSteps);
    bool isSecondarySpecular = (rayType == 2u || rayType == 3u) && (rayDepth > 0u);
    if (isSecondarySpecular) {
        steps = max(8, (int)ceil(smoothSteps * 0.75f));
    }
    float stepSize = thickness / (float)steps; 

    float3 pos = rayOrigin + rayDir * tStart;
    
    // Dither start (Using stable IGN but very low amplitude)
    float jitter = 0.5;
    #if defined(RAYTRACING_COMMON_H) && !defined(CLOUDS_NO_RAYTRACING_INTRINSICS)
    // Low amplitude jitter helps break bands without causing massive chunks
    jitter = InterleavedGradientNoise(DispatchRaysIndex().xy, (uint)globalFrameCount);
    jitter = 0.3 + 0.4 * jitter; // Center around 0.5
    #endif
    pos += rayDir * (jitter * stepSize);

    float3 sum = float3(0,0,0);
    float transmittance = 1.0f;
    float horizonDensityFade = smoothstep(0.035f, 0.18f, rayDir.y);
    
    // Phase Function (sun-angle aware)
    float cosAngle = dot(rayDir, sunDir);
    float basePhaseG = clamp(CloudCB.scattering, -0.95f, 0.95f);
    float sunElevation = saturate(sunDir.y * 0.5f + 0.5f);

    // Fallback/Bias colors (Neutral Grey/White base)
    // We keep these strictly neutral so they don't fight with the sky color
    float3 kSkyZenith = float3(0.5, 0.55, 0.65); // Brighter blue-grey
    float3 kSkyHorizon = float3(0.8, 0.8, 0.82); // Brighter Light grey
    
    #ifdef RAYTRACING_COMMON_H
    // If we have access to the Raytracing Common header, we can sample the actual Skybox
    // for a much better approximation of the ambient environment.
    
    // Sample Zenith (Up) and Horizon (Side) from the EnvMap
    // Use a high mip level (e.g., 6-8) to get the blurred/irradiance color
    float2 uvZenith = DirectionToUVRotated(float3(0.0, 1.0, 0.0));
    float2 uvHorizon = DirectionToUVRotated(float3(1.0, 0.0, 0.0));

    float3 realZenith = envMap.SampleLevel(linearSampler, uvZenith, 8.0).rgb * CLOUD_SKY_RADIANCE_SCALE;
    float3 realHorizon = envMap.SampleLevel(linearSampler, uvHorizon, 8.0).rgb * CLOUD_SKY_RADIANCE_SCALE;
    // Use real sky colors directly.
    kSkyZenith = realZenith;
    kSkyHorizon = realHorizon;
    #endif
    
    // Lighting loop
    float shadowTermCached = 1.0f;
    uint shadowEvery = (uint)max(1, CloudCB.shadowEvery);
    if (isSecondarySpecular) {
        shadowEvery = max(1u, shadowEvery * 2u);
    }
    float densityLodBias = isSecondarySpecular ? 0.5f : 0.0f;
    float shadowLod = CloudCB.shadowLod + densityLodBias;
    int shadowSteps = max(1, CloudCB.shadowSteps);

    for(int i = 0; i < steps; ++i) {
        if (transmittance < 0.01f) break;

        float density = SampleDensity(pos, densityLodBias) * horizonDensityFade;
        
        if (density > 0.001f) {
            float denseMask = saturate(density * 1.25f);
            float absorptionCoeff = CloudCB.absorption * CLOUD_ABSORPTION_SCALE;
            // Softer extinction at wispy edges, close to user value in dense cores.
            float viewAbsorption = absorptionCoeff * lerp(0.72f, 1.00f, denseMask);
            float extinction = density * viewAbsorption;
            float stepTrans = exp(-extinction * stepSize);

            // Light Energy Calculation
            // 1. Direct Sun (with shadow ray)
            
            // Scene-linear cloud sun calibration. Keep this independent of
            // camera exposure; exposure is applied once in tonemap.
            float sunset = saturate((0.35f - sunElevation) / 0.35f);
            float3 sunsetTint = lerp(float3(1.0f, 1.0f, 1.0f), float3(1.0f, 0.86f, 0.72f), sunset * 0.7f);
            float3 sunColorScaled = lightColor * CLOUD_SUN_ILLUMINANCE_SCALE * sunsetTint;

            float shadowTerm = shadowTermCached;
            if (density > CloudCB.shadowDensityThreshold) {
               if (((uint)i % shadowEvery) == 0u) {
                   float3 lPos = pos;
                   float lDens = 0.0;
                   float lStep = CloudCB.shadowStepSize;

                   // Offset randomized slightly to break banding
                   lPos += sunDir * (lStep * jitter);

                   [loop]
                   for(int s = 0; s < shadowSteps; ++s) {
                       lPos += sunDir * lStep;
                       lDens += SampleDensity(lPos, shadowLod);
                   }
                   // Stronger absorption for light/shadow integration restores
                   // internal cloud structure without over-darkening edge wisps.
                   float shadowAbsorption = absorptionCoeff * lerp(1.08f, 1.70f, denseMask);
                   shadowTermCached = exp(-lDens * lStep * shadowAbsorption);
                   shadowTermCached = pow(saturate(shadowTermCached),
                                          lerp(1.0f, 0.68f, saturate(CloudCB.shadowSoftness)));
               }
               shadowTerm = shadowTermCached;
            } else {
               shadowTermCached = 1.0f;
               shadowTerm = 1.0f;
            }
            
            // Sun-angle + density dependent phase keeps silver-lining and avoids flat shading.
            float gLocal = clamp(basePhaseG + lerp(0.03f, 0.14f, denseMask), -0.95f, 0.93f);
            float phaseLocal = PhaseHG(cosAngle, gLocal);
            float edgeLight = pow(saturate(cosAngle), lerp(14.0f, 5.5f, saturate(CloudCB.silverLiningStrength)));
            phaseLocal += edgeLight * saturate(CloudCB.silverLiningStrength) * lerp(0.025f, 0.11f, 1.0f - denseMask);
            float backScatter = pow(saturate(-cosAngle), 2.0f) * (1.0f - sunElevation) * 0.18f;
            phaseLocal += backScatter;

            // Beer-powder approximation (user-controlled via powderStrength)
            float powder = 1.0f - exp(-density * (1.5f + 1.5f * CloudCB.powderStrength));
            float directLight = shadowTerm * phaseLocal * lerp(1.0f, 1.0f + 1.8f * CloudCB.powderStrength * powder, 0.65f);
            
            // 2. Ambient Light
            // Height based gradient
            float hPct = (pos.y - CloudCB.cloudBottom) / (layerTop - CloudCB.cloudBottom);
            // kSkyHorizon and kSkyZenith now contain the actual sampled sky colors (if weighted)
            float3 ambient = lerp(kSkyHorizon, kSkyZenith, hPct);
            
            // Ambient occlusion based on density: deeper = darker
            ambient *= exp(-density * 1.0f);
            ambient *= 0.6f;

            // Ground bounce gives cloud bottoms warmer/filled response.
            float groundWeight = (1.0f - saturate(hPct)) * (0.35f + 0.65f * denseMask);
            float3 groundBounce = kSkyHorizon * (0.12f * groundWeight * CloudCB.groundBounceStrength);
            ambient += groundBounce;

            // Prevent full-black cloud silhouettes under low ambient exposure.
            float3 ambientFloor = 0.02f * (kSkyZenith + kSkyHorizon) * 0.5f;
            ambient = max(ambient, ambientFloor);
            
            float shadowOcclusion = 1.0f - shadowTerm;
            float msStep = (1.0f - stepTrans) * shadowOcclusion;
            float msBoost = max(0.0f, CloudCB.multiScatterBoost);
            float3 multiScatter = ambient * ((0.55f + 0.35f * msBoost) * CloudCB.powderStrength * msStep);
            multiScatter += sunColorScaled * ((0.020f + 0.018f * msBoost) * CloudCB.powderStrength * msStep * powder);

            float3 source = (CloudCB.sunIntensity * directLight * sunColorScaled) + ambient + multiScatter;
            source = max(source, 0.0);
            
            // Integation: Energy = Source * Density * (Integral of T over step)
            // Integra(T) = (1 - stepTrans) / extinction
            float3 integ = source * density * (1.0f - stepTrans) / max(1e-4f, extinction);
            
            sum += integ * transmittance;
            transmittance *= stepTrans;
        }
        
        pos += rayDir * stepSize;
    }

    if (any(isnan(sum)) || any(isinf(sum))) sum = float3(0.0, 0.0, 0.0);
    if (isnan(transmittance) || isinf(transmittance)) transmittance = 1.0f;

    // Multiple-scattering tail (dense-core weighted, sky/sun tinted).
    float opacity = 1.0f - saturate(transmittance);
    float denseCore = pow(saturate(opacity), 1.7f);
    float sunset = saturate((0.35f - sunElevation) / 0.35f);
    float3 msSkyTint = lerp(kSkyHorizon, kSkyZenith, 0.35f);
    float3 msSunTint = lightColor * CLOUD_MS_SUN_SCALE *
                       lerp(float3(1.0f, 1.0f, 1.0f), float3(1.0f, 0.85f, 0.70f), sunset * 0.7f);
    float3 msColor = lerp(msSkyTint, msSunTint, 0.38f);
    float msStrength = 0.012f + 0.030f * CloudCB.powderStrength + 0.018f * max(0.0f, CloudCB.multiScatterBoost);
    sum += msColor * (msStrength * denseCore);

    float4 cirrus = EvaluateCirrusLayer(rayOrigin, rayDir, sunDir, lightColor, kSkyZenith, kSkyHorizon);
    sum = sum * cirrus.a + cirrus.rgb;
    transmittance *= cirrus.a;

    sum = clamp(sum, 0.0, 50000.0);
    transmittance = saturate(transmittance);
    return float4(sum, transmittance);
}

float4 RaymarchClouds(float3 rayOrigin, float3 rayDir, float tMin, float tMax, float3 sunDir, float3 lightColor) {
    return RaymarchClouds(rayOrigin, rayDir, tMin, tMax, sunDir, lightColor, 0u, 0u);
}
#endif
