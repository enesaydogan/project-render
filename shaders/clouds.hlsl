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

    int shadowSteps;
    float shadowStepSize;
    float shadowLod;

    int maxSteps;
    float verticalStepMeters;
    int shadowEvery;
    float shadowDensityThreshold;

    float timeSeconds;

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

// Dual Henyey-Greenstein for realistic cloud scattering
// (Forward peak + slight backward peak)
float PhaseHG(float cosTheta, float g) {
    float g2 = g * g;
    float fwd = (1.0f - g2) / (4.0f * CLOUDS_PI * pow(1.0f + g2 - 2.0f * g * cosTheta, 1.5f));
    
    // Mix with backward scattering for silver lining and fuller look
    float gBack = -0.2f;
    float g2Back = gBack * gBack;
    float back = (1.0f - g2Back) / (4.0f * CLOUDS_PI * pow(1.0f + g2Back - 2.0f * gBack * cosTheta, 1.5f));
    
    return lerp(fwd, back, 0.4f);
}

float Remap(float x, float a, float b, float c, float d) {
    return c + (saturate((x - a) / (b - a))) * (d - c);
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
    // Stable Spherical Altitude Calculation
    float3 relP = p - PLANET_CENTER;
    float distToCenter = length(relP);
    
    // Accurate height Above ground: h = sqrt(p.x^2 + (p.y+R)^2 + p.z^2) - R
    // Using (h+R)^2 = p.x^2 + p.y^2 + 2p.yR + R^2 + R^2
    // h = (p.x^2 + p.y^2 + 2.0*p.y*R) / (dist + R)
    float heightAboveGround = (dot(p, p) + 2.0 * p.y * EARTH_RADIUS) / (distToCenter + EARTH_RADIUS);
    
    // Check bounds
    if (heightAboveGround < CloudCB.cloudBottom || heightAboveGround > CloudCB.cloudTop) return 0.0;
    
    float heightPct = saturate((heightAboveGround - CloudCB.cloudBottom) / (CloudCB.cloudTop - CloudCB.cloudBottom));

    // 1. Base Coordinates with Rotation to break tracking
    float3 basePos = p * CloudCB.baseScale;
    basePos.xz += CloudCB.timeSeconds * CloudCB.windSpeed * float2(0.001, 0.0005); // Diagonal wind
    
    // Domain Warp (Low Frequency)
    float3 warpPos = RotateDomain(basePos) * 0.5f;
    float warp = SampleWarp(warpPos, lod);
    basePos += (warp - 0.5f) * CloudCB.warpStrength * 1.5f;

    // 2. Base Shape (Perlin-Worley)
    // Rotate sampling to avoid axis streaks
    float3 noiseCoord = RotateDomain(basePos);
    // Base R is Channel-Packed Perlin-Worley from C++ generation
    float baseCloud = NoiseTex.SampleLevel(LinearWrapSampler, noiseCoord, lod).r;
    
    // 3. Density Gradient
    float heightGrad = HeightGradient(heightPct);
    baseCloud *= heightGrad;

    // 4. Coverage
    // Apply coverage by eroding the density signal
    
    // Coverage should map naturally from clear sky (0) to overcast (1).
    float effectiveCoverage = saturate(CloudCB.coverage);
    // High threshold at low coverage removes most clouds; low threshold at high
    // coverage produces overcast-like fill.
    float densityThreshold = lerp(0.9f, 0.05f, effectiveCoverage);

    // Standard Schneider remap:
    float covRemap = Remap(baseCloud, densityThreshold, 1.0f, 0.0f, 1.0f);
    baseCloud = covRemap; 
    
    // 5. Cloud Type / Weather variation (simulated by large scale noise)
    // Acts as a "probability to spawn cloud here"
    float3 coveragePos = p * CloudCB.coverageScale + float3(CloudCB.timeSeconds * CloudCB.windSpeed * 0.005, 0, 0);
    
    // Use 2D noise for weather map to prevent vertical streaking artifacts from 3D sampling
    // 3D noise at large scales can look like vertical columns if the Z variation is slow
    float2 weatherUV = coveragePos.xz;
    // Map weatherUV to spherical cap? For now, large scale planar offset is fine locally.
    float weatherNoise = NoiseTex.SampleLevel(LinearWrapSampler, float3(weatherUV, 0.5f), lod + 2.0).r;
    
    // Sync weather mask with coverage so we don't punch holes in "full" coverage
    // Use effectiveCoverage here to ensure consistency
    float weatherThreshold = lerp(0.85f, 0.0f, effectiveCoverage);
    // Smoother transition for weather mask to avoid hard cloud cuts
    float weatherMask = smoothstep(weatherThreshold - 0.2f, weatherThreshold + 0.2f, weatherNoise);
    baseCloud *= weatherMask;

    // 6. Detail Erosion (High Frequency)
    if (baseCloud > 0.0) {
        float3 detailPos = p * CloudCB.detailScale;
        detailPos.xz += CloudCB.timeSeconds * CloudCB.windSpeed * 0.002;
        // Rotate detail too
        detailPos = RotateDomain(detailPos);
        
        float3 detailNoise = DetailTex.SampleLevel(LinearWrapSampler, detailPos, lod).rgb;
        float highFreqFBM = detailNoise.r * 0.625 + detailNoise.g * 0.25 + detailNoise.b * 0.125;
        
        // Erode edges more than center
        float modifier = lerp(highFreqFBM, 1.0 - highFreqFBM, saturate(heightPct * 5.0)); // Invert at bottom?
        
        // Remap density based on detail
        float erosion = CloudCB.erosion * 0.5; // Scale erosion
        baseCloud = Remap(baseCloud, highFreqFBM * erosion, 1.0, 0.0, 1.0);
    }
    
    return saturate(baseCloud) * CloudCB.density;
}

// Raymarch function returning accumulated cloud color (rgb) and transmittance (a)
// tMin/tMax: Intersection distance with cloud shell
float4 RaymarchClouds(float3 rayOrigin, float3 rayDir, float tMin, float tMax, float3 sunDir, float3 lightColor, uint rayType, uint rayDepth) {
    // 1. Setup Intersection for Spherical Shell
    float innerRadius = EARTH_RADIUS + CloudCB.cloudBottom;
    float outerRadius = EARTH_RADIUS + CloudCB.cloudTop;
    
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
    
    if (tEnd <= tStart) return float4(0,0,0,1);

    float thickness = tEnd - tStart;
    
    // Adaptive stepping
    float verticalThickness = max(1.0f, CloudCB.cloudTop - CloudCB.cloudBottom);
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
    #ifdef RAYTRACING_COMMON_H
    // Low amplitude jitter helps break bands without causing massive chunks
    jitter = InterleavedGradientNoise(DispatchRaysIndex().xy, (uint)globalFrameCount);
    jitter = 0.3 + 0.4 * jitter; // Center around 0.5
    #endif
    pos += rayDir * (jitter * stepSize);

    float3 sum = float3(0,0,0);
    float transmittance = 1.0f;
    
    // Phase Function
    float cosAngle = dot(rayDir, sunDir);
    float phase = PhaseHG(cosAngle, CloudCB.scattering);

    // Setup Ambient Color
    float3 ambientSkyColor = float3(0.0, 0.0, 0.0);
    float ambientWeight = 0.0f;
    
    #ifdef RAYTRACING_COMMON_H
    // Use the global ambient color passed from the CPU (usually derived from Skybox/EnvMap)
    ambientSkyColor = ambientColor.rgb;
    ambientWeight = ambientColor.w; // Weight determining how much to use global vs procedural
    #endif

    // Fallback/Bias colors (Neutral Grey/White base)
    // We keep these strictly neutral so they don't fight with the sky color
    float3 kSkyZenith = float3(0.5, 0.55, 0.65); // Brighter blue-grey
    float3 kSkyHorizon = float3(0.8, 0.8, 0.82); // Brighter Light grey
    
    #ifdef RAYTRACING_COMMON_H
    // If we have access to the Raytracing Common header, we can sample the actual Skybox
    // for a much better approximation of the ambient environment.
    
    // Sample Zenith (Up) and Horizon (Side) from the EnvMap
    // Use a high mip level (e.g., 6-8) to get the blurred/irradiance color
    float2 uvZenith = DirectionToUV(float3(0.0, 1.0, 0.0));
    float2 uvHorizon = DirectionToUV(float3(1.0, 0.0, 0.0));
    
    // Scale down the sampled values significantly because the Prague Sky Model outputs raw radiance units (very high).
    // Factor 0.001f brings the ~4000-10000 range down to ~4-10 range max
    float skyScale = 0.001f; 

    float3 realZenith = envMap.SampleLevel(linearSampler, uvZenith, 8.0).rgb * intensity * skyScale;
    float3 realHorizon = envMap.SampleLevel(linearSampler, uvHorizon, 8.0).rgb * intensity * skyScale;
    
    // Blend towards real sky colors based on weight
    // If weight is high, we match the skybox perfectly.
    kSkyZenith = lerp(kSkyZenith, realZenith, saturate(ambientWeight));
    kSkyHorizon = lerp(kSkyHorizon, realHorizon, saturate(ambientWeight));
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

        float density = SampleDensity(pos, densityLodBias);
        
        if (density > 0.001f) {
            float extinction = density * CloudCB.absorption;
            float stepTrans = exp(-extinction * stepSize);

            // Light Energy Calculation
            // 1. Direct Sun (with shadow ray)
            
            // Manual scaling to fix "blown out" sun color from Prague Sky Model without touching C++
            float3 sunColorScaled = lightColor * 0.000025f; 

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
                   shadowTermCached = exp(-lDens * lStep * CloudCB.absorption);
               }
               shadowTerm = shadowTermCached;
            } else {
               shadowTermCached = 1.0f;
               shadowTerm = 1.0f;
            }
            
            // Powder effect: Darken edges facing away from sun, brighten edges facing sun?
            // Simple Beer-Powder approximation for realism
            float powder = 1.0f - exp(-density * 2.0f);
            float directLight = shadowTerm * phase * lerp(1.0f, 2.0f * powder, 0.5f);
            
            // 2. Ambient Light
            // Height based gradient
            float hPct = (pos.y - CloudCB.cloudBottom) / (CloudCB.cloudTop - CloudCB.cloudBottom);
            // kSkyHorizon and kSkyZenith now contain the actual sampled sky colors (if weighted)
            float3 ambient = lerp(kSkyHorizon, kSkyZenith, hPct);
            
            // Ambient occlusion based on density: deeper = darker
            ambient *= exp(-density * 1.0f);
            ambient *= 0.6f; // Overall Ambient intensity boost
            
            float3 source = (CloudCB.sunIntensity * directLight * sunColorScaled) + ambient;
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
    sum = clamp(sum, 0.0, 64.0);
    transmittance = saturate(transmittance);
    return float4(sum, transmittance);
}

float4 RaymarchClouds(float3 rayOrigin, float3 rayDir, float tMin, float tMax, float3 sunDir, float3 lightColor) {
    return RaymarchClouds(rayOrigin, rayDir, tMin, tMax, sunDir, lightColor, 0u, 0u);
}
#endif
