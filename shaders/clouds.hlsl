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
SamplerState LinearWrapSampler : register(s10);

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

// Base cloud height profile: flatter bottom, puffier mid, eroded top.
float HeightGradient(float h) {
    // Stronger base, softer top (cumulus-ish)
    float g0 = smoothstep(0.0, 0.08, h);
    float g1 = 1.0 - smoothstep(0.68, 1.0, h);
    float mid = smoothstep(0.10, 0.55, h) * (1.0 - smoothstep(0.55, 0.95, h));
    return saturate(g0 * g1) * lerp(0.65, 1.0, mid);
}

float SampleDensity(float3 p, float lod) {
    float heightPct = (p.y - CloudCB.cloudBottom) / (CloudCB.cloudTop - CloudCB.cloudBottom);
    if (heightPct < 0.0 || heightPct > 1.0) return 0.0;

    // World -> noise space
    float3 baseUVW = p * CloudCB.baseScale;

    // Wind scroll + slight shear with height
    float windT = CloudCB.timeSeconds * CloudCB.windSpeed;
    baseUVW.xz += windT * float2(0.0022, 0.0016);
    baseUVW.x += heightPct * CloudCB.windSpeed * 0.00035f;

    // Domain warp to break up blobs
    float3 warpUVW = baseUVW * 0.65f + float3(17.0, 3.0, 11.0);
    float wx = SampleWarp(warpUVW + float3(0.0, 0.0, 0.0), 2.0);
    float wy = SampleWarp(warpUVW + float3(13.5, 7.2, 5.1), 2.0);
    float wz = SampleWarp(warpUVW + float3(2.8, 19.3, 11.7), 2.0);
    float3 warp = (float3(wx, wy, wz) * 2.0f - 1.0f) * CloudCB.warpStrength;
    baseUVW += warp * 0.35f;

    // Packed noise
    float4 nBase = NoiseTex.SampleLevel(LinearWrapSampler, baseUVW, lod);
    float perlin = nBase.r;       // billowy base
    float worley = nBase.g;       // cellular breakup

    // Large-scale coverage modulation (acts like a cheap 2D coverage map).
    // IMPORTANT: Coverage must be linear and predictable:
    // - coverage=0 => no clouds
    // - coverage=1 => full cover
    // We achieve this by using a uniform-ish noise field as a *mask* instead of
    // using coverage as a hard threshold inside the base density field.
    float coverage = saturate(CloudCB.coverage);
    float2 covUV = p.xz * CloudCB.coverageScale + windT * float2(0.00035, 0.00027);
    // Use a more uniform-ish noise distribution for the coverage mask.
    // Perlin alone tends to be biased, causing "dead" slider ranges.
    float4 covN = NoiseTex.SampleLevel(LinearWrapSampler, float3(covUV, 0.5f), 3.0f);
    float covNoise = covN.g * 0.70f + covN.r * 0.30f; // worley base + a bit of perlin
    covNoise = saturate((covNoise - 0.5f) * 1.75f + 0.5f); // contrast to spread values

    // Edge softness (coverageVariation controls transition width).
    // Keep this fairly small so coverage 0.4->0.9 actually changes the mask.
    float covEdge = lerp(0.0020f, 0.035f, saturate(CloudCB.coverageVariation));
    float a = max(0.0f, covNoise - covEdge);
    float b = min(1.0f, covNoise + covEdge);
    float coverageMask = smoothstep(a, b, coverage);

    // Only treat it as "overcast" very near 1.0.
    float overcast = smoothstep(0.92f, 1.0f, coverage);

    // Perlin-Worley style base shape.
    // At high coverage, fade out Worley breakup so Coverage=1 can approach overcast.
    float baseShape = saturate(perlin * 1.8f - 0.2f);
    float worleyMask = saturate(worley * 1.2f + 0.1f);
    worleyMask = lerp(worleyMask, 1.0f, overcast);
    baseShape *= worleyMask;
    // At near-overcast, reduce "clumpiness" so it fills in more uniformly.
    float effectiveShapePower = lerp(CloudCB.shapePower, 1.15f, overcast);
    baseShape = pow(baseShape, effectiveShapePower);

    // Overcast bias: enforce a soft floor so coverage=1 can approach full cover.
    // This prevents Perlin low regions from punching bald holes when coverage is max.
    float overcastFloor = overcast * 0.35f;
    baseShape = max(baseShape, overcastFloor);

    // Erosion/detail (stronger towards the top)
    float3 detailUVW = p * CloudCB.detailScale + warp * 0.5f;
    detailUVW.xz += windT * float2(0.0045, 0.0031);
    float4 nDetail = NoiseTex.SampleLevel(LinearWrapSampler, detailUVW, lod);
    float detail = saturate(nDetail.b * 1.2f - 0.1f);
    float erosionAmt = CloudCB.erosion * lerp(0.15f, 1.0f, smoothstep(0.25f, 1.0f, heightPct));
    // Keep erosion responsive; only damp slightly at true overcast.
    erosionAmt *= lerp(1.0f, 0.8f, overcast);

    // Stronger, more obvious erosion response.
    baseShape = saturate(baseShape - detail * erosionAmt * 0.9f);
    baseShape = saturate(baseShape - (1.0f - baseShape) * detail * erosionAmt * 0.45f);

    // Height shaping
    float heightFade = HeightGradient(heightPct);

    float density = baseShape * coverageMask;
    density *= heightFade;

    return density * CloudCB.density;
}

// Raymarch function returning accumulated cloud color (rgb) and transmittance (a)
// tMin/tMax: Intersection distance with cloud shell
float4 RaymarchClouds(float3 rayOrigin, float3 rayDir, float tMin, float tMax, float3 sunDir, float3 lightColor) {
    if (tMax <= tMin) return float4(0,0,0,1); // Full transmittance

    // Intersect with the cloud altitude slab [cloudBottom, cloudTop] in world-space Y.
    // This prevents enormous step sizes (and missed clouds) when tMin/tMax are very wide.
    float slabEnter = 0.0f;
    float slabExit = -1.0f;
    float dy = rayDir.y;
    if (abs(dy) < 1e-5f) {
        // Ray parallel to slab planes
        if (rayOrigin.y >= CloudCB.cloudBottom && rayOrigin.y <= CloudCB.cloudTop) {
            slabEnter = 0.0f;
            slabExit = tMax;
        } else {
            return float4(0,0,0,1);
        }
    } else {
        float t0 = (CloudCB.cloudBottom - rayOrigin.y) / dy;
        float t1 = (CloudCB.cloudTop - rayOrigin.y) / dy;
        slabEnter = min(t0, t1);
        slabExit = max(t0, t1);
    }

    float tStart = max(max(0.0f, tMin), slabEnter);
    float tEnd = min(min(tMax, 100000.0f), slabExit);
    if (tEnd <= tStart) {
        #ifdef RAYTRACING_COMMON_H
        int dbg = (int)debugMode;
        if (dbg == 11) return float4(0, 0, 0, 1); // slab mask
        #endif
        return float4(0,0,0,1);
    }

    float thickness = tEnd - tStart;

    #ifdef RAYTRACING_COMMON_H
    int dbg = (int)debugMode;
    if (dbg == 11) {
        // Slab mask (should be white anywhere the ray intersects the cloud layer)
        return float4(1, 1, 1, 1);
    }
    if (dbg == 12) {
        // CB sanity check: show normalized bottom/top/coverage
        float b = saturate(CloudCB.cloudBottom / 1000.0f);
        float t = saturate(CloudCB.cloudTop / 1000.0f);
        return float4(b, t, saturate(CloudCB.coverage), 1);
    }
    if (dbg == 13) {
        // Noise sanity check at the first point inside the slab
        float3 p0 = rayOrigin + rayDir * tStart;
        float3 uvw = p0 * CloudCB.baseScale;
        float4 n = NoiseTex.SampleLevel(LinearWrapSampler, uvw, 0.0f);
        return float4(n.rgb, 1);
    }
    if (dbg == 14) {
        // Density sanity at mid-slab (avoid boundary fade = 0)
        float tMid = 0.5f * (tStart + tEnd);
        float3 pMid = rayOrigin + rayDir * tMid;
        float d = SampleDensity(pMid, 0.0f);
        return float4(d, d, d, 1);
    }
    if (dbg == 16) {
        // Base shape sanity (pre-coverage threshold) at mid-slab
        float tMid = 0.5f * (tStart + tEnd);
        float3 pMid = rayOrigin + rayDir * tMid;

        float heightPct = (pMid.y - CloudCB.cloudBottom) / (CloudCB.cloudTop - CloudCB.cloudBottom);
        float3 baseUVW = pMid * CloudCB.baseScale;
        float windT = CloudCB.timeSeconds * CloudCB.windSpeed;
        baseUVW.xz += windT * float2(0.0022, 0.0016);
        baseUVW.x += heightPct * CloudCB.windSpeed * 0.00035f;

        float4 nBase = NoiseTex.SampleLevel(LinearWrapSampler, baseUVW, 0.0f);
        float perlin = nBase.r;
        float worley = nBase.g;

        float baseShape = saturate(perlin * 1.8f - 0.2f);
        baseShape *= saturate(worley * 1.2f + 0.1f);
        baseShape = pow(baseShape, CloudCB.shapePower);

        return float4(baseShape, baseShape, baseShape, 1);
    }
    #endif

    // Sampling resolution based on *vertical* traversal.
    // For near-horizon rays, thickness along the ray can be tens of km; if we
    // keep a fixed step count, we undersample and get speckles / no clouds.
    float verticalThickness = max(1.0f, CloudCB.cloudTop - CloudCB.cloudBottom);
    float verticalStepMeters = max(1.0f, CloudCB.verticalStepMeters);
    float rayDirYAbs = max(0.02f, abs(rayDir.y));
    int targetSteps = (int)ceil((verticalThickness / verticalStepMeters) / rayDirYAbs);
    int steps = clamp(max(CloudCB.steps, targetSteps), 8, max(8, CloudCB.maxSteps));
    float stepSize = thickness / (float)steps;
    float3 pos = rayOrigin + rayDir * tStart;
    
    float3 sum = float3(0,0,0);
    float transmittance = 1.0f;
    
    float cosAngle = dot(rayDir, sunDir);
    float phase = PhaseHG(cosAngle, CloudCB.scattering);

    // Dither start to reduce banding.
    // Disabled by default because it can look like speckle without strong temporal accumulation.
    // With the adaptive step count below, banding is already minimal.
    // float jitter = 0.0f;
    // #ifdef RAYTRACING_COMMON_H
    // jitter = InterleavedGradientNoise(DispatchRaysIndex().xy, globalFrameCount);
    // #endif
    // pos += rayDir * (jitter * stepSize);

    // Ambient: prefer the sky's ambient if available (common.hlsli), else a fallback gradient.
    float3 ambientSky = float3(0.0, 0.0, 0.0);
    float ambientW = 0.0f;
    #ifdef RAYTRACING_COMMON_H
    ambientSky = ambientColor.rgb;
    ambientW = ambientColor.w;
    #endif

    float3 ambientTop = float3(0.55, 0.68, 0.90);
    float3 ambientBottom = float3(0.62, 0.66, 0.70);

    float cachedLightTrans = 1.0f;
    int shadowCountdown = 0;

    for(int i = 0; i < steps; ++i) {
        if (transmittance < 0.01f) break;

        float density = SampleDensity(pos, 0.0f);
        if (density > 0.001f) {
            // Shadowing is expensive: only recompute occasionally and only when
            // density is significant.
            if (shadowCountdown <= 0 && density >= CloudCB.shadowDensityThreshold) {
                float shadowDens = 0.0f;
                float3 lpos = pos;
                float lstep = CloudCB.shadowStepSize;
                [loop]
                for (int s = 0; s < CloudCB.shadowSteps; ++s) {
                    lpos += sunDir * lstep;
                    float d = SampleDensity(lpos, CloudCB.shadowLod);
                    shadowDens += d * lstep;
                    if (shadowDens * CloudCB.absorption > 6.0f) break;
                }
                cachedLightTrans = exp(-shadowDens * CloudCB.absorption);
                shadowCountdown = max(1, CloudCB.shadowEvery);
            }
            shadowCountdown--;
            
            // Powder effect (dark edges) - approximation
            float powder = (1.0f - exp(-density * 2.0f)) * CloudCB.powderStrength;
            
            float heightPct = (pos.y - CloudCB.cloudBottom) / (CloudCB.cloudTop - CloudCB.cloudBottom);
            float3 ambientGrad = lerp(ambientBottom, ambientTop, heightPct);
            float3 ambient = lerp(ambientGrad, ambientSky, saturate(ambientW));
            ambient *= 0.35f;
            
            // Direct Sun Lighting
            // Use slightly reduced powder effect on direct light to prevent too much darkening
            float3 sunLight = lightColor * CloudCB.sunIntensity * cachedLightTrans * phase * lerp(1.0, powder, 0.65);
            
            // Scattering integral integration
            // Energy = (Sun + Ambient) * density
            float3 incoming = (sunLight + ambient) * density;
            
            float extinction = density * CloudCB.absorption;
            float stepTrans = exp(-extinction * stepSize);
            
            // Analytic integration over step
            sum += incoming * transmittance * (1.0f - stepTrans) / max(0.0001f, extinction);
            transmittance *= stepTrans;
        }
        pos += rayDir * stepSize;
    }

    #ifdef RAYTRACING_COMMON_H
    if (dbg == 15) {
        float o = saturate(1.0f - transmittance);
        return float4(o, o, o, 1);
    }
    #endif

    return float4(sum, transmittance);
}
#endif
