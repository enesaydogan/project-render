// shaders/path_tracer_core.hlsl
// Main Path Tracing RayGen shader with Accumulation and ReSTIR DI

#include "raytracing/common.hlsli"
#include "random_lib.hlsl"
#include "lights_lib.hlsl"
#include "restir_lib.hlsl"

// Additional resources for ReSTIR
StructuredBuffer<Light> g_lights : register(t5000);
RWTexture2D<float> g_variance : register(u20);
RWTexture2D<float4> g_reservoir0 : register(u2);
RWTexture2D<float4> g_reservoir1 : register(u3);
RWTexture2D<float4> g_gi_reservoir_a0 : register(u4);
RWTexture2D<float4> g_gi_reservoir_a1 : register(u5);
RWTexture2D<float4> g_gi_reservoir_a2 : register(u6);
RWTexture2D<float4> g_gi_reservoir_b0 : register(u7);
RWTexture2D<float4> g_gi_reservoir_b1 : register(u8);
RWTexture2D<float4> g_gi_reservoir_b2 : register(u9);

#include "brdf_lib.hlsl"

// Cosine-weighted cone sampling around a direction (very small, stable approximation)
float3 SampleCone(float3 dir, float cosThetaMax, float2 u)
{
    float z = lerp(cosThetaMax, 1.0, u.x);
    float r = sqrt(max(0.0, 1.0 - z * z));
    float phi = 2.0 * PI * u.y;
    float x = r * cos(phi);
    float y = r * sin(phi);

    float3 up = abs(dir.z) < 0.999 ? float3(0,0,1) : float3(1,0,0);
    float3 T = normalize(cross(up, dir));
    float3 B = cross(dir, T);
    return normalize(T * x + B * y + dir * z);
}

GI_Reservoir unpack_gi_reservoir(float4 d0, float4 d1, float4 d2) {
    GI_Reservoir r;
    r.hitPos = d0.xyz;
    r.radiance = d1.xyz;
    r.w_sum = d2.x;
    r.M = asuint(d2.y);
    r.W = d2.z;
    return r;
}

void pack_gi_reservoir(GI_Reservoir r, out float4 d0, out float4 d1, out float4 d2) {
    d0 = float4(r.hitPos, 0.0);
    d1 = float4(r.radiance, 0.0);
    d2 = float4(r.w_sum, asfloat(r.M), r.W, 0.0);
}

// target PDF for ReSTIR DI (luminance of lit surface)
float calculate_p_target(float3 radiance, float3 albedo, float3 f_brdf, float NdotL) {
    float p = length(max(0.0, radiance * f_brdf * NdotL));
    return min(p, 1e10); // Clamp to prevent infinity
}

Reservoir unpack_reservoir(float4 data) {
    Reservoir r;
    r.lightIndex = asuint(data.x);
    // Defensive: if hardware normalized the NaN bits, reset to Sun index
    if (r.lightIndex == 0x7FC00000) r.lightIndex = 0xFFFFFFFF;
    r.w_sum = data.y;
    r.M = asuint(data.z);
    r.W = data.w;
    // Extra safety: clamp state
    if (isnan(r.w_sum) || isinf(r.w_sum)) r.w_sum = 0.0;
    if (isnan(r.W) || isinf(r.W)) r.W = 0.0;
    return r;
}

float4 pack_reservoir(Reservoir r) {
    return float4(asfloat(r.lightIndex), r.w_sum, asfloat(r.M), r.W);
}

bool IsSpatiallyCompatible(uint2 p0, uint2 p1, float ndotMin, float depthTolBase)
{
    float4 n0 = g_normalRoughnessOut[p0];
    float4 n1 = g_normalRoughnessOut[p1];
    float3 nn0 = n0.xyz;
    float3 nn1 = n1.xyz;
    float l0 = dot(nn0, nn0);
    float l1 = dot(nn1, nn1);

    // History is unreliable for spatial reuse when normal/depth buffers are invalid.
    if (l0 <= 0.25 || l1 <= 0.25) return false;
    nn0 = normalize(nn0);
    nn1 = normalize(nn1);
    if (dot(nn0, nn1) < ndotMin) return false;
    if (abs(n0.w - n1.w) > 0.25) return false;

    float d0 = g_depth[p0];
    float d1 = g_depth[p1];
    if (!isfinite(d0) || !isfinite(d1) || d0 <= 0.0 || d1 <= 0.0) return false;
    float depthScale = max(max(abs(d0), abs(d1)), 1.0);
    float depthTol = max(0.0025, depthTolBase * depthScale);
    if (abs(d0 - d1) > depthTol) return false;

    return true;
}

float halton(uint index, uint base)
{
    float f = 1.0;
    float r = 0.0;
    while (index > 0)
    {
        f /= (float)base;
        r += f * (float)(index % base);
        index /= base;
    }
    return r;
}

[shader("raygeneration")]
void RayGen()
{
    uint3 launchIndex = DispatchRaysIndex();
    uint3 launchDim = DispatchRaysDimensions();
    uint frame = (uint)globalFrameCount;
    uint accumFrame = (uint)accumulationCount;
    // Keep per-frame reservoir ping-pong deterministic even for pixels that
    // early-out due to adaptive sampling.
    bool flip = (frame % 2) == 1;
    const uint kAdaptiveStartSpp = 24u;
    const uint kAdaptiveMinPerPixelSpp = 64u;
    const float kAdaptiveRelScale = 0.90;
    const float kAdaptiveEdgeRelScale = 0.55;
    const float kAdaptiveAbsSemFloor = 5e-4;
    const float kAdaptiveAbsSemScale = 0.0125;
    const float kAdaptiveEdgeAbsSemScale = 0.0055;
    const float kAdaptiveMinKeepProb = 0.10;
    const float kAdaptiveEdgeMinKeepProb = 0.22;
    const float kAdaptiveEdgeContrastThreshold = 0.012;
    const float kAdaptiveMinExpectedRatio = 0.95;
    const float kAdaptiveLagKeepScale = 1.00;
    const float kRestirSpatialRadiusPx = (useAdaptiveSampling > 0.5) ? 6.0 : 12.0;
    const bool debugViewActive = (SHADER_DEBUG_MODE > 0.0) || (SHADER_DEBUG_VIS_MODE == 1.0);

    if (!debugViewActive && maxSPP > 0.0 && accumFrame >= (uint)maxSPP) {
        float4 total = g_accumulation[launchIndex.xy];
        if (total.a > 0.0) {
            // Output is always linear HDR; tonemapping happens after DLSS/RR.
            g_output[launchIndex.xy] = float4(total.rgb / total.a, 1.0);
        }
        return;
    }

    // Adaptive Sampling Early Exit
    if (accumFrame > kAdaptiveStartSpp && useAdaptiveSampling > 0.5) {
        float4 acc = g_accumulation[launchIndex.xy];
        float accM2 = g_variance[launchIndex.xy];
        
        if (acc.a > (float)kAdaptiveMinPerPixelSpp) {
            float n = acc.a;
            float3 meanColor = acc.rgb / n;
            float meanLum = dot(meanColor, float3(0.2126, 0.7152, 0.0722));
            
            // Welford-based Standard Error of Mean: SEM = sqrt(M2) / N
            // (Note: var = M2/N, SEM = sqrt(var/N) = sqrt(M2/N^2) = sqrt(M2)/N)
            float sem = sqrt(max(0.0, accM2)) / n;
            float noise = sem / (max(0.01, meanLum) + 0.001);

            // Edge-aware guard from current accumulation neighborhood.
            float edgeContrast = 0.0;
            if (launchIndex.x > 0) {
                uint2 p = uint2(int2(launchIndex.xy) + int2(-1, 0));
                float4 a = g_accumulation[p];
                if (a.a > 1.0) edgeContrast = max(edgeContrast, abs(dot(a.rgb / a.a, float3(0.2126, 0.7152, 0.0722)) - meanLum));
            }
            if (launchIndex.x + 1 < launchDim.x) {
                uint2 p = uint2(int2(launchIndex.xy) + int2(1, 0));
                float4 a = g_accumulation[p];
                if (a.a > 1.0) edgeContrast = max(edgeContrast, abs(dot(a.rgb / a.a, float3(0.2126, 0.7152, 0.0722)) - meanLum));
            }
            if (launchIndex.y > 0) {
                uint2 p = uint2(int2(launchIndex.xy) + int2(0, -1));
                float4 a = g_accumulation[p];
                if (a.a > 1.0) edgeContrast = max(edgeContrast, abs(dot(a.rgb / a.a, float3(0.2126, 0.7152, 0.0722)) - meanLum));
            }
            if (launchIndex.y + 1 < launchDim.y) {
                uint2 p = uint2(int2(launchIndex.xy) + int2(0, 1));
                float4 a = g_accumulation[p];
                if (a.a > 1.0) edgeContrast = max(edgeContrast, abs(dot(a.rgb / a.a, float3(0.2126, 0.7152, 0.0722)) - meanLum));
            }

            bool isEdgeRegion = edgeContrast > kAdaptiveEdgeContrastThreshold;
            float relThreshold = max(0.001, noiseThreshold * (isEdgeRegion ? kAdaptiveEdgeRelScale : kAdaptiveRelScale));
            float absSemThreshold = max(kAdaptiveAbsSemFloor, noiseThreshold * (isEdgeRegion ? kAdaptiveEdgeAbsSemScale : kAdaptiveAbsSemScale));

            // Dual criterion:
            // - relative for regular regions
            // - absolute SEM to avoid over-sampling very dark areas indefinitely.
            bool convergedRelative = (noise < relThreshold);
            bool convergedAbsolute = (sem < absSemThreshold);
            if (convergedRelative || convergedAbsolute) {
                 // Avoid a hard binary "wave" by keeping a small stochastic
                 // fraction of converged pixels actively sampling.
                 RNG adaptiveRng = init_rng(launchIndex.xy + uint2(0x9e37u, 0x7f4au),
                                            frame ^ 0xA511E9B3u);
                 float relProximity = saturate(noise / relThreshold);
                 float absProximity = saturate(sem / absSemThreshold);
                 float proximity = min(relProximity, absProximity);
                 float minKeepProb = isEdgeRegion ? kAdaptiveEdgeMinKeepProb : kAdaptiveMinKeepProb;
                 float keepProb = max(minKeepProb, proximity);
                 float expectedN = max(1.0, accumFrame + 1.0);
                 float deficitRatio = saturate((expectedN - n) / expectedN);
                 // Don't let converged pixels fall too far behind global SPP.
                 if (n < expectedN * kAdaptiveMinExpectedRatio) {
                     keepProb = 1.0;
                 } else {
                     float lagKeepProb = deficitRatio * kAdaptiveLagKeepScale;
                     keepProb = max(keepProb, lagKeepProb);
                 }
                 if (next_float(adaptiveRng) > keepProb) {
                 // Preserve reservoir continuity for neighbors that still resample
                 // this pixel. Without this copy, adaptive early-out leaves stale
                 // ping-pong sides and can make ReSTIR reuse unstable.
                 if (flip) {
                     g_reservoir1[launchIndex.xy] = g_reservoir0[launchIndex.xy];
                     SHADER_COUNTER_ADD(SHADER_COUNTER_RESERVOIR_WRITES, 1);
                     g_gi_reservoir_a0[launchIndex.xy] = g_gi_reservoir_b0[launchIndex.xy];
                     g_gi_reservoir_a1[launchIndex.xy] = g_gi_reservoir_b1[launchIndex.xy];
                     g_gi_reservoir_a2[launchIndex.xy] = g_gi_reservoir_b2[launchIndex.xy];
                 } else {
                     g_reservoir0[launchIndex.xy] = g_reservoir1[launchIndex.xy];
                     SHADER_COUNTER_ADD(SHADER_COUNTER_RESERVOIR_WRITES, 1);
                     g_gi_reservoir_b0[launchIndex.xy] = g_gi_reservoir_a0[launchIndex.xy];
                     g_gi_reservoir_b1[launchIndex.xy] = g_gi_reservoir_a1[launchIndex.xy];
                     g_gi_reservoir_b2[launchIndex.xy] = g_gi_reservoir_a2[launchIndex.xy];
                 }

                 if (SHADER_DEBUG_VIS_MODE == 1.0) {
                     // Debug: Show Converged pixels as Green
                     g_output[launchIndex.xy] = float4(0.0, 1.0, 0.0, 1.0);
                 } else {
                     g_output[launchIndex.xy] = float4(meanColor, 1.0);
                 }
                 return;
                 }
             }
        }
    }

    RNG rng = init_rng(launchIndex.xy, frame);

    // Use jitter from Camera CB (calculated on CPU to match DLSS)
    float2 jitter = float2(jitterX, jitterY);
    float2 uv = (float2(launchIndex.xy) + 0.5 + jitter) / float2(launchDim.xy);
    float2 ndc = uv * 2.0 - 1.0;

    // Non-jittered pixel center for motion vectors / sky reprojection.
    float2 uvCenter = (float2(launchIndex.xy) + 0.5) / float2(launchDim.xy);
    float2 ndcCenter = uvCenter * 2.0 - 1.0;

    float f_inv = tan(radians(fov) * 0.5);
    float3 forward = normalize(camForward);
    float3 R = normalize(cross(forward, camUp));
    float3 U = normalize(cross(R, forward));
    
    float y_view = (-ndc.y) * f_inv;
    float x_view = ndc.x * aspect * f_inv;
    
    float3 rayDir = normalize(x_view * R + y_view * U + forward);
    float3 rayOrigin = camPos;

    // Center ray direction (no jitter).
    float y_view_center = (-ndcCenter.y) * f_inv;
    float x_view_center = ndcCenter.x * aspect * f_inv;
    float3 rayDirCenter = normalize(x_view_center * R + y_view_center * U + forward);

    float3 accumulatedColor = float3(0, 0, 0);
    float3 throughput = float3(1, 1, 1);

    // Primary hit info for DLSS inputs
    bool primaryHit = false;
    float3 primaryPos = float3(0, 0, 0);
    float3 primaryNormal = float3(0, 1, 0);
    float3 primaryAlbedo = float3(0, 0, 0);
    float primaryRoughness = 1.0;
    float primaryViewZ = -1.0;
    float3 primarySpecAlbedo = float3(0, 0, 0);
    float primarySpecHitDist = -1.0;

    int specularBounces = 0;
    int refractiveBounces = 0;
    int giBounces = 0;
    uint currentRayType = RAY_TYPE_PRIMARY;

    for (int bounce = 0; bounce < 32; ++bounce) 
    {
        RayDesc ray;
        ray.Origin = rayOrigin;
        ray.Direction = rayDir;
        ray.TMin = 0.001;
        ray.TMax = 10000.0;

        RayPayload payload;
        payload.color = float3(0,0,0);
        payload.albedo = float3(0,0,0);
        payload.emissive = float3(0,0,0);
        payload.normal = float3(0,0,0);
        payload.position = float3(0,0,0);
        payload.refractionColor = float3(0,0,0);
        payload.ior = 1.0;
        payload.roughness = 1.0;
        payload.metalness = 0.0;
        payload.thinWalled = 0.0;
        payload.translucency = 0.0;
        payload.matIndex = 0;
        payload.t = -1.0;
        payload.rayDepth = (uint)bounce;
        payload.rayType = currentRayType;

        TraceRay(g_accel, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
        SHADER_COUNTER_ADD(SHADER_COUNTER_TRACE_RAYS, 1);


        if (bounce == 0 && payload.t >= 0.0) {
            primaryHit = true;
            primaryPos = payload.position;
            primaryNormal = payload.normal;
            primaryAlbedo = payload.albedo;
            primaryRoughness = max(0.04, payload.roughness); // Increased min roughness for archviz stability
            
            // For DLSS-RR, use the distance along the center ray to avoid depth jitter
            // but use the actual hit position for coordinates.
            float3 toHit = primaryPos - camPos;
            primaryViewZ = dot(toHit, forward); 

            // Specular Albedo calculation for DLSS-RR
            float3 F0 = lerp(float3(0.04, 0.04, 0.04), payload.albedo, payload.metalness);
            float NdotV = saturate(dot(payload.normal, -rayDir));
            primarySpecAlbedo = EnvBRDFApprox2(F0, primaryRoughness * primaryRoughness, NdotV);

            // Trace dedicated specular reflection ray to get hit distance for DLSS-RR
            // Only trace if the surface has significant specular reflectance AND RR is active
            if (dlssRayReconstruction > 0.5 && max(primarySpecAlbedo.r, max(primarySpecAlbedo.g, primarySpecAlbedo.b)) > 0.01) {
                float3 R_spec = reflect(rayDir, payload.normal);
                RayDesc specHitRay;
                specHitRay.Origin = primaryPos + payload.normal * 0.001;
                specHitRay.Direction = R_spec;
                specHitRay.TMin = 0.001;
                specHitRay.TMax = 1000.0;
                RayPayload specHitPayload;
                specHitPayload.t = -1.0;
                specHitPayload.rayDepth = (uint)bounce + 1;
                specHitPayload.rayType = RAY_TYPE_REFLECTION;
                TraceRay(g_accel, RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES, 0xFF, 0, 0, 0, specHitRay, specHitPayload);
                SHADER_COUNTER_ADD(SHADER_COUNTER_SPECULAR_TRACES, 1);
                primarySpecHitDist = (specHitPayload.t > 0) ? specHitPayload.t : 1000.0;
            } else {
                primarySpecHitDist = 0.0;
            }
        }

        if (payload.t < 0.0) {
            // Miss: add sky color and terminate
            // RR is very sensitive to sky shimmer. For the primary ray, sample
            // the environment using a non-jittered ray direction.
            float3 missColor = payload.color;
            if (bounce == 0 && dlssRayReconstruction > 0.5 && cloudRenderingEnabled < 0.5) {
                float2 skyUv = DirectionToUV(rayDirCenter);
                // Slight mip bias helps remove residual HDRI aliasing that shows up
                // as shimmer, especially along silhouettes.
                const float rrSkyLod = 0;
                missColor = envMap.SampleLevel(linearSampler, skyUv, rrSkyLod).rgb * intensity;
            }
            accumulatedColor += throughput * missColor;
            
            // On first bounce miss, update reservoir to empty
            if (bounce == 0) {
                float4 res_data = pack_reservoir(init_reservoir());
                if (flip) { g_reservoir1[launchIndex.xy] = res_data; SHADER_COUNTER_ADD(SHADER_COUNTER_RESERVOIR_WRITES, 1); }
                else      { g_reservoir0[launchIndex.xy] = res_data; SHADER_COUNTER_ADD(SHADER_COUNTER_RESERVOIR_WRITES, 1); }
            }
            break;
        }

        // Legacy material/cloud debug modes (1..16) visualize primary-hit payloads.
        // New accumulation diagnostics (17+) must run full path-tracing flow.
        if (SHADER_DEBUG_MODE > 0.0 && SHADER_DEBUG_MODE <= 16.0) {
            accumulatedColor = payload.color;
            break;
        }

        float3 N = payload.normal;
        float3 P = payload.position;
        float3 V = -rayDir;
        float roughness = max(0.001, payload.roughness);
        float metallic = payload.metalness;
        float transmission = saturate(max(payload.refractionColor.r, max(payload.refractionColor.g, payload.refractionColor.b))) * (1.0 - metallic);
        float3 diffuseAlbedo = payload.albedo * (1.0 - metallic) * (1.0 - transmission);

        // 1. Direct Lighting (Next Event Estimation + ReSTIR for 1st bounce)
        float3 directLighting = float3(0, 0, 0);
        float3 indirectLighting = float3(0, 0, 0);
        
        if (bounce == 0) {
            // --- ReSTIR DI Logic for Primary Hit ---
            Reservoir res = init_reservoir();
            
            // A. Initial Candidate Sampling
            // Sample Sun
            {
                LightSample ls = evaluate_directional_light(lightDir.xyz, lightColor.rgb, lightColor.w);
                float NdotL = saturate(dot(N, ls.L));
                
                // Evaluation for ReSTIR
                float3 F0 = lerp(float3(0.04, 0.04, 0.04), payload.albedo, metallic);
                float3 H = normalize(ls.L + V);
                float3 spec = D_GGX(max(0.0, dot(N, H)), roughness) * V_SmithCorrelated(max(0.0, dot(N, V)), NdotL, roughness) * F_Schlick(max(0.0, dot(H, V)), F0);
                float3 brdf = (diffuseAlbedo / PI) + spec;

                float p_target = calculate_p_target(ls.radiance, payload.albedo, brdf, NdotL);
                update_reservoir(res, 0xFFFFFFFF, p_target, rng);
            }

            // Sample random local light
            uint numLights = (uint)lightCount;
            if (numLights > 0) {
                uint lightIdx = next_uint(rng) % numLights;
                Light l = g_lights[lightIdx];
                float3 L = l.position - P;
                float dist = length(L);
                L /= dist;
                float attenuation = l.intensity / (dist * dist + 1.0);
                float3 radiance = l.color * attenuation;
                float NdotL = saturate(dot(N, L));

                float3 F0 = lerp(float3(0.04, 0.04, 0.04), payload.albedo, metallic);
                float3 H = normalize(L + V);
                float3 spec = D_GGX(max(0.0, dot(N, H)), roughness) * V_SmithCorrelated(max(0.0, dot(N, V)), NdotL, roughness) * F_Schlick(max(0.0, dot(H, V)), F0);
                float3 brdf = (diffuseAlbedo / PI) + spec;

                float p_target = calculate_p_target(radiance, payload.albedo, brdf, NdotL) * (float)numLights;
                update_reservoir(res, lightIdx, p_target, rng);
            }

            // B. Temporal Resampling
            // DLSS-RR prefers minimal temporal correlation; disable temporal reuse
            // while RR is enabled.
            if (frame > 0 && dlssRayReconstruction < 0.5) {
                float4 prev_data;
                if (flip) prev_data = g_reservoir0[launchIndex.xy];
                else      prev_data = g_reservoir1[launchIndex.xy];
                SHADER_COUNTER_ADD(SHADER_COUNTER_RESERVOIR_READS, 1);
                Reservoir prev_res = unpack_reservoir(prev_data);
                prev_res.M = min(prev_res.M, 30);
                
                float3 L_prev;
                float3 radiance_prev;
                if (prev_res.lightIndex == 0xFFFFFFFF) {
                    L_prev = normalize(lightDir.xyz);
                    radiance_prev = lightColor.rgb * lightColor.w;
                } else if (prev_res.lightIndex < numLights) {
                    Light l = g_lights[prev_res.lightIndex];
                    L_prev = l.position - P;
                    float dist = length(L_prev);
                    L_prev /= dist;
                    radiance_prev = l.color * (l.intensity / (dist * dist + 1.0));
                } else {
                    radiance_prev = float3(0,0,0);
                }
                
                float NdotL_prev = saturate(dot(N, L_prev));
                float3 F0 = lerp(float3(0.04, 0.04, 0.04), payload.albedo, metallic);
                float3 H = normalize(L_prev + V);
                float3 spec = D_GGX(max(0.0, dot(N, H)), roughness) * V_SmithCorrelated(max(0.0, dot(N, V)), NdotL_prev, roughness) * F_Schlick(max(0.0, dot(H, V)), F0);
                float3 brdf_prev = (diffuseAlbedo / PI) + spec;

                float p_target_at_curr = calculate_p_target(radiance_prev, payload.albedo, brdf_prev, saturate(dot(N, L_prev)));
                combine_reservoirs(res, prev_res, p_target_at_curr, rng);
            }

            // C. Spatial Resampling (Neighbor Pixels)
            if (frame > 6 && ((frame & 1u) == 0u)) {
                const int spatialReuseCount = 1;
                for (int i = 0; i < spatialReuseCount; ++i) {
                    // Use a disk distribution for better sampling coverage and to avoid banding
                    float angle = next_float(rng) * 2.0 * PI;
                    float radius = sqrt(next_float(rng)) * kRestirSpatialRadiusPx;
                    int2 offset = int2(cos(angle) * radius, sin(angle) * radius);
                    int2 neighborCoords = clamp(int2(launchIndex.xy) + offset, int2(0,0), int2(launchDim.xy)-1);

                    // Always guard spatial reuse at geometric/material edges.
                    if (!IsSpatiallyCompatible(launchIndex.xy, uint2(neighborCoords), 0.96, 0.006)) {
                        continue;
                    }
                    
                    float4 neighbor_data;
                    if (flip) neighbor_data = g_reservoir0[neighborCoords];
                    else      neighbor_data = g_reservoir1[neighborCoords];
                    SHADER_COUNTER_ADD(SHADER_COUNTER_SPATIAL_NEIGHBOR_READS, 1);
                    Reservoir neighbor_res = unpack_reservoir(neighbor_data);
                    
                    // Cap neighbor contribution to prevent fireflies from dominating
                    neighbor_res.M = min(neighbor_res.M, 8); 
                    
                    // Re-evaluate neighbor light candidate at current shading point
                    float3 L_neigh;
                    float3 radiance_neigh;
                    float dist_neigh = 1.0;
                    if (neighbor_res.lightIndex == 0xFFFFFFFF) {
                        L_neigh = normalize(lightDir.xyz);
                        dist_neigh = 1000.0;
                        radiance_neigh = lightColor.rgb * lightColor.w;
                    } else if (neighbor_res.lightIndex < numLights) {
                        Light l = g_lights[neighbor_res.lightIndex];
                        L_neigh = l.position - P;
                        dist_neigh = length(L_neigh);
                        L_neigh /= dist_neigh;
                        radiance_neigh = l.color * (l.intensity / (dist_neigh * dist_neigh + 1.0));
                    } else {
                        radiance_neigh = float3(0,0,0);
                    }

                    float NdotL_neigh = saturate(dot(N, L_neigh));
                    float3 F0 = lerp(float3(0.04, 0.04, 0.04), payload.albedo, metallic);
                    float3 H = normalize(L_neigh + V);
                    float3 spec = D_GGX(max(0.0, dot(N, H)), roughness) * V_SmithCorrelated(max(0.0, dot(N, V)), NdotL_neigh, roughness) * F_Schlick(max(0.0, dot(H, V)), F0);
                    float3 brdf_neigh = (diffuseAlbedo / PI) + spec;

                    float p_target_at_curr = calculate_p_target(radiance_neigh, payload.albedo, brdf_neigh, NdotL_neigh);
                    
                    // Simple Visibility check for spatial reuse significantly reduces block artifacts
                    if (p_target_at_curr > 0.0) {
                        RayDesc spatialRay; spatialRay.Origin = P + N * 0.001; spatialRay.Direction = L_neigh;
                        spatialRay.TMin = 0.001; spatialRay.TMax = max(0.001, dist_neigh - 0.003);
                        RayPayload spatialPayload; spatialPayload.t = 1.0;
                        spatialPayload.rayType = RAY_TYPE_SHADOW;
                        TraceRay(g_accel, RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_FORCE_NON_OPAQUE, 0xFF, 0, 0, 0, spatialRay, spatialPayload);
                        SHADER_COUNTER_ADD(SHADER_COUNTER_TRACE_RAYS, 1);
                        if (spatialPayload.t > 0.0) p_target_at_curr = 0.0; // Occluded
                    }

                    combine_reservoirs(res, neighbor_res, p_target_at_curr, rng);
                }
            }

            // C. Final ReSTIR DI Shading & Finalization
            float3 L_final;
            float dist_final;
            float3 radiance_final;

            if (res.lightIndex == 0xFFFFFFFF) {
                L_final = normalize(lightDir.xyz);
                dist_final = 1000.0;
                radiance_final = lightColor.rgb * lightColor.w;
            } else if (res.lightIndex < numLights) {
                Light l = g_lights[res.lightIndex];
                L_final = l.position - P;
                dist_final = length(L_final);
                L_final /= dist_final;
                radiance_final = l.color * (l.intensity / (dist_final * dist_final + 1.0));
            } else {
                radiance_final = float3(0,0,0);
            }

            float NdotL_final = saturate(dot(N, L_final));
            float3 F0 = lerp(float3(0.04, 0.04, 0.04), payload.albedo, metallic);
            float3 H_f = normalize(L_final + V);
            float3 spec_f = D_GGX(max(0.0, dot(N, H_f)), roughness) * V_SmithCorrelated(max(0.0, dot(N, V)), NdotL_final, roughness) * F_Schlick(max(0.0, dot(H_f, V)), F0);
            float3 brdf_f = (diffuseAlbedo / PI) + spec_f;

            float p_target_final = calculate_p_target(radiance_final, payload.albedo, brdf_f, NdotL_final);
            
            // Finalize reservoir BEFORE storing (so W is valid in next frame)
            finalize_reservoir(res, p_target_final);

            // Store ReSTIR state for next frame
            float4 packed_res = pack_reservoir(res);
            if (flip) { g_reservoir1[launchIndex.xy] = packed_res; SHADER_COUNTER_ADD(SHADER_COUNTER_RESERVOIR_WRITES, 1); }
            else      { g_reservoir0[launchIndex.xy] = packed_res; SHADER_COUNTER_ADD(SHADER_COUNTER_RESERVOIR_WRITES, 1); }

            // D. Apply Visibility for current frame shading
            if (p_target_final > 0.0) {
                RayDesc shadowRay;
                shadowRay.Origin = P + N * 0.001;
                
                // Jitter shadow ray for Sun (Disc Light)
                if (res.lightIndex == 0xFFFFFFFF && lightDir.w > 0.0) {
                     // lightDir.w holds the sun angular radius (half-angle)
                     float2 u_s = next_float2(rng);
                     shadowRay.Direction = SampleCone(L_final, cos(lightDir.w), u_s);
                } else {
                     shadowRay.Direction = L_final;
                }
                
                shadowRay.TMin = 0.001;
                shadowRay.TMax = dist_final - 0.002;
                RayPayload shadowPayload;
                shadowPayload.t = 1.0;
                shadowPayload.rayDepth = (uint)bounce + 1;
                shadowPayload.rayType = RAY_TYPE_SHADOW;
                TraceRay(g_accel, RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_FORCE_NON_OPAQUE, 0xFF, 0, 0, 0, shadowRay, shadowPayload);
                SHADER_COUNTER_ADD(SHADER_COUNTER_TRACE_RAYS, 1);
                SHADER_COUNTER_ADD(SHADER_COUNTER_SHADOW_TRACES, 1);
                
                if (shadowPayload.t < 0.0) {
                    directLighting = radiance_final * brdf_f * NdotL_final * res.W;
                }
            }

            // --- ReSTIR GI (Indirect Illumination) ---
            // Respect GI bounce budget: 0 means disable indirect GI entirely.
            if (maxGIBounces > 0.0) {
                GI_Reservoir gi_res = init_gi_reservoir();
                // A. Initial Candidate
                {
                    float3 nextDir_gi; float pdf_gi; float3 f_brdf_gi; float2 u_gi = next_float2(rng);
                    float3 F0 = lerp(float3(0.04, 0.04, 0.04), payload.albedo, metallic);
                    float3 F = F_Schlick(max(0.0, dot(N, V)), F0);
                    float specProb = max(F.x, max(F.y, F.z));
                    float diffProb = (1.0 - specProb) * (1.0 - metallic);
                    float totalProb = specProb + diffProb;
                    if (next_float(rng) * totalProb < specProb) {
                        float3 H = SampleGGX(u_gi, N, roughness);
                        nextDir_gi = reflect(-V, H);
                        float NdotL = saturate(dot(N, nextDir_gi));
                        pdf_gi = (PDF_GGX(saturate(dot(N,H)), saturate(dot(V,H)), roughness) * specProb) / totalProb;
                        f_brdf_gi = D_GGX(saturate(dot(N, H)), roughness) * G_Smith(max(0.0, dot(N, V)), NdotL, roughness) * F_Schlick(saturate(dot(H, V)), F0) / (4.0 * max(0.0, dot(N, V)) * NdotL + 0.001);
                    } else {
                        nextDir_gi = SampleLambert(u_gi, N);
                        float NdotL = saturate(dot(N, nextDir_gi));
                        pdf_gi = (PDF_Lambert(NdotL) * diffProb) / totalProb;
                        f_brdf_gi = (diffuseAlbedo / PI);
                    }
                    if (pdf_gi > 0.0) {
                        RayDesc giRay; giRay.Origin = P + N * 0.0005; giRay.Direction = nextDir_gi;
                        giRay.TMin = 0.0001; giRay.TMax = 1000.0;
                        RayPayload giPayload; giPayload.color = float3(0,0,0); giPayload.emissive = float3(0,0,0); giPayload.t = -1.0; giPayload.rayDepth = (uint)bounce + 1;
                        giPayload.rayType = RAY_TYPE_DIFFUSE;
                        TraceRay(g_accel, RAY_FLAG_NONE, 0xFF, 0, 0, 0, giRay, giPayload);
                        SHADER_COUNTER_ADD(SHADER_COUNTER_TRACE_RAYS, 1);
                        // Include sky/clouds in GI radiance
                        float3 radiance = (giPayload.t > 0.0) ? (giPayload.color + giPayload.emissive) : giPayload.color;
                        float3 hitPos = (giPayload.t > 0.0) ? giPayload.position : (P + nextDir_gi * 1000.0);
                        
                        float p_target = length(radiance * f_brdf_gi * saturate(dot(N, nextDir_gi)));
                        // Clamp weight to prevent fireflies from rare but bright background samples
                        float ris_weight = min(p_target / max(1e-5, pdf_gi), 1e5);
                        update_gi_reservoir(gi_res, hitPos, radiance, ris_weight, rng);
                    }
                }
                // B. Temporal Resampling
                if (frame > 0 && dlssRayReconstruction < 0.5) {
                    float4 d0, d1, d2;
                    if (flip) { d0 = g_gi_reservoir_b0[launchIndex.xy]; d1 = g_gi_reservoir_b1[launchIndex.xy]; d2 = g_gi_reservoir_b2[launchIndex.xy]; }
                    else      { d0 = g_gi_reservoir_a0[launchIndex.xy]; d1 = g_gi_reservoir_a1[launchIndex.xy]; d2 = g_gi_reservoir_a2[launchIndex.xy]; }
                    GI_Reservoir prev_gi = unpack_gi_reservoir(d0, d1, d2);
                    prev_gi.M = min(prev_gi.M, 15);
                    float3 L_gi = normalize(prev_gi.hitPos - P);
                    float3 F0 = lerp(float3(0.04, 0.04, 0.04), payload.albedo, metallic);
                    float3 H = normalize(L_gi + V);
                    float3 spec = D_GGX(max(0.0, dot(N, H)), roughness) * G_Smith(max(0.0, dot(N, V)), saturate(dot(N, L_gi)), roughness) * F_Schlick(max(0.0, dot(H, V)), F0) / (4.0 * max(0.0, dot(N, V)) * saturate(dot(N, L_gi)) + 0.001);
                    float3 brdf = (diffuseAlbedo / PI) + spec;
                    float p_target_at_curr = length(prev_gi.radiance * brdf * saturate(dot(N, L_gi)));
                    combine_gi_reservoirs(gi_res, prev_gi, p_target_at_curr, rng);
                }
                // C. Spatial Resampling
                if (frame > 6 && ((frame & 1u) == 0u)) {
                    const int spatialReuseCount = 1;
                    for (int i = 0; i < spatialReuseCount; ++i) {
                        float2 unitSample = float2(next_float(rng), next_float(rng)) * 2.0 - 1.0;
                        int2 offset = int2(unitSample * kRestirSpatialRadiusPx);
                        int2 neighborCoords = clamp(int2(launchIndex.xy) + offset, int2(0,0), int2(launchDim.xy)-1);

                        if (!IsSpatiallyCompatible(launchIndex.xy, uint2(neighborCoords), 0.94, 0.010)) {
                            continue;
                        }

                        float4 d0, d1, d2;
                        if (flip) { d0 = g_gi_reservoir_b0[neighborCoords]; d1 = g_gi_reservoir_b1[neighborCoords]; d2 = g_gi_reservoir_b2[neighborCoords]; }
                        else      { d0 = g_gi_reservoir_a0[neighborCoords]; d1 = g_gi_reservoir_a1[neighborCoords]; d2 = g_gi_reservoir_a2[neighborCoords]; }
                        GI_Reservoir neigh_gi = unpack_gi_reservoir(d0, d1, d2);
                        neigh_gi.M = min(neigh_gi.M, 8);
                        float3 L_gi = normalize(neigh_gi.hitPos - P);
                        float3 F0 = lerp(float3(0.04, 0.04, 0.04), payload.albedo, metallic);
                        float3 H = normalize(L_gi + V);
                        float3 spec = D_GGX(max(0.0, dot(N, H)), roughness) * G_Smith(max(0.0, dot(N, V)), saturate(dot(N, L_gi)), roughness) * F_Schlick(max(0.0, dot(H, V)), F0) / (4.0 * max(0.0, dot(N, V)) * saturate(dot(N, L_gi)) + 0.001);
                        float3 brdf = (diffuseAlbedo / PI) + spec;
                        float p_target_at_curr = length(neigh_gi.radiance * brdf * saturate(dot(N, L_gi)));
                        
                        // Spatial Jacobian / Visibility for GI
                        if (p_target_at_curr > 0.0) {
                            RayDesc spatialRay; spatialRay.Origin = P + N * 0.001; spatialRay.Direction = L_gi;
                            spatialRay.TMin = 0.001; spatialRay.TMax = max(0.001, distance(neigh_gi.hitPos, P) - 0.003);
                            RayPayload spatialPayload; spatialPayload.t = 1.0;
                            spatialPayload.rayType = RAY_TYPE_SHADOW;
                            TraceRay(g_accel, RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_FORCE_NON_OPAQUE, 0xFF, 0, 0, 0, spatialRay, spatialPayload);
                            SHADER_COUNTER_ADD(SHADER_COUNTER_TRACE_RAYS, 1);
                            if (spatialPayload.t > 0.0) p_target_at_curr = 0.0;
                        }

                        combine_gi_reservoirs(gi_res, neigh_gi, p_target_at_curr, rng);
                    }
                }
                // Finalize GI
                float3 L_gi_final = normalize(gi_res.hitPos - P);
                float3 F0_gi = lerp(float3(0.04, 0.04, 0.04), payload.albedo, metallic);
                float3 H_gi = normalize(L_gi_final + V);
                float3 spec_gi = D_GGX(max(0.0, dot(N, H_gi)), roughness) * G_Smith(max(0.0, dot(N, V)), saturate(dot(N, L_gi_final)), roughness) * F_Schlick(max(0.0, dot(H_gi, V)), F0_gi) / (4.0 * max(0.0, dot(N, V)) * saturate(dot(N, L_gi_final)) + 0.001);
                float3 brdf_gi_final = (diffuseAlbedo / PI) + spec_gi;
                float p_target_final_gi = length(gi_res.radiance * brdf_gi_final * saturate(dot(N, L_gi_final)));
                finalize_gi_reservoir(gi_res, p_target_final_gi);
                float4 out_d0, out_d1, out_d2; pack_gi_reservoir(gi_res, out_d0, out_d1, out_d2);
                if (flip) { g_gi_reservoir_a0[launchIndex.xy] = out_d0; g_gi_reservoir_a1[launchIndex.xy] = out_d1; g_gi_reservoir_a2[launchIndex.xy] = out_d2; }
                else      { g_gi_reservoir_b0[launchIndex.xy] = out_d0; g_gi_reservoir_b1[launchIndex.xy] = out_d1; g_gi_reservoir_b2[launchIndex.xy] = out_d2; }
                
                if (gi_res.W > 0.0) {
                    // Visibility test for GI reconnection
                    RayDesc giVisRay; giVisRay.Origin = P + N * 0.001; giVisRay.Direction = L_gi_final;
                    giVisRay.TMin = 0.001; giVisRay.TMax = max(0.001, distance(gi_res.hitPos, P) - 0.003);
                    RayPayload giVisPayload; giVisPayload.t = 1.0; giVisPayload.rayDepth = (uint)bounce + 1;
                    giVisPayload.rayType = RAY_TYPE_SHADOW;
                    TraceRay(g_accel, RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_FORCE_NON_OPAQUE, 0xFF, 0, 0, 0, giVisRay, giVisPayload);
                    SHADER_COUNTER_ADD(SHADER_COUNTER_TRACE_RAYS, 1);
                    if (giVisPayload.t < 0.0) {
                        indirectLighting = gi_res.radiance * brdf_gi_final * saturate(dot(N, L_gi_final)) * gi_res.W;
                    }
                }
            } else {
                float4 zero4 = float4(0.0, 0.0, 0.0, 0.0);
                if (flip) {
                    g_gi_reservoir_a0[launchIndex.xy] = zero4;
                    g_gi_reservoir_a1[launchIndex.xy] = zero4;
                    g_gi_reservoir_a2[launchIndex.xy] = zero4;
                } else {
                    g_gi_reservoir_b0[launchIndex.xy] = zero4;
                    g_gi_reservoir_b1[launchIndex.xy] = zero4;
                    g_gi_reservoir_b2[launchIndex.xy] = zero4;
                }
            }
        } 
        else 
        {
            // Simple NEE for subsequent bounces
            uint numLights = (uint)lightCount;
            float3 L_nee;
            float3 radiance_nee;
            float dist_nee;
            if (next_float(rng) < 0.5 || numLights == 0) {
                // Sample Sun
                L_nee = normalize(lightDir.xyz);
                radiance_nee = lightColor.rgb * lightColor.w;
                dist_nee = 1000.0;
            } else {
                // Sample random point light
                uint lightIdx = next_uint(rng) % numLights;
                Light l = g_lights[lightIdx];
                L_nee = l.position - P;
                dist_nee = length(L_nee);
                L_nee /= dist_nee;
                radiance_nee = l.color * (l.intensity / (dist_nee * dist_nee + 1.0)) * (float)numLights;
            }

            float NdotL_nee = saturate(dot(N, L_nee));
            if (NdotL_nee > 0) {
                RayDesc shadowRay;
                shadowRay.Origin = P + N * 0.001;
                shadowRay.Direction = L_nee;
                shadowRay.TMin = 0.001;
                shadowRay.TMax = dist_nee - 0.001;
                RayPayload shadowPayload;
                shadowPayload.t = 1.0;
                shadowPayload.rayDepth = (uint)bounce + 1;
                shadowPayload.rayType = RAY_TYPE_SHADOW;
                TraceRay(g_accel, RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_FORCE_NON_OPAQUE, 0xFF, 0, 0, 0, shadowRay, shadowPayload);
                SHADER_COUNTER_ADD(SHADER_COUNTER_TRACE_RAYS, 1);
                SHADER_COUNTER_ADD(SHADER_COUNTER_SHADOW_TRACES, 1);
                if (shadowPayload.t < 0.0) {
                     float3 F0 = lerp(float3(0.04, 0.04, 0.04), payload.albedo, metallic);
                     float3 H = normalize(L_nee + V);
                     float3 spec = D_GGX(max(0.0, dot(N, H)), roughness) * V_SmithCorrelated(max(0.0, dot(N, V)), NdotL_nee, roughness) * F_Schlick(max(0.0, dot(H, V)), F0);
                     float3 brdf = (diffuseAlbedo / PI) + spec;
                     directLighting = brdf * radiance_nee * NdotL_nee * 2.0; // *2 because of 50/50 sun/lights
                }
            }
        }

        accumulatedColor += throughput * (directLighting + indirectLighting + payload.emissive);

        // 2. Indirect Lighting Ray Generation
        float3 nextDir;
        float pdf;
        float3 f_brdf;
        float2 u = float2(next_float(rng), next_float(rng));

        // Refraction / Glass logic
        bool isRefractive = length(payload.refractionColor) > 0.01;
        if (isRefractive) {
            float3 glassL;
            bool refracted = false;

            // Thin-walled mode: window glass approximation (no bending)
            if (payload.thinWalled > 0.5) {
                float cosTheta = abs(dot(V, N));
                float F = FresnelDielectric(cosTheta, payload.ior);
                if (u.x < F) {
                    refracted = false;
                    glassL = reflect(-V, N);
                } else {
                    refracted = true;
                    glassL = rayDir; // straight-through
                }
            } else {
                refracted = SampleGlass(V, N, payload.ior, u, glassL);
            }

            // Rough transmission/reflection blur (cheap approximation)
            float rgh = max(payload.roughness, 0.0);
            if (rgh > 0.02) {
                float cosMax = saturate(1.0 - rgh * rgh);
                float2 ucone = float2(next_float(rng), next_float(rng));
                glassL = SampleCone(glassL, cosMax, ucone);
            }

            if (refracted) {
                if (refractiveBounces >= (int)maxRefractiveBounces) break;
                refractiveBounces++;
                nextDir = glassL;
                f_brdf = payload.refractionColor;
                currentRayType = RAY_TYPE_REFRACTION;
            } else {
                if (specularBounces >= (int)maxSpecularBounces) break;
                specularBounces++;
                nextDir = glassL;
                f_brdf = float3(1,1,1);
                currentRayType = RAY_TYPE_DIFFUSE;
            }
            pdf = 1.0;
            rayOrigin = P + nextDir * 0.001; 
            // For glass, the cosine term and PDF often cancel out in simple path tracers,
            // but we'll manually update throughput here to ensure it's correct.
            throughput *= f_brdf;
            // Skip the standard PBR throughput update
            rayDir = nextDir;
            continue; 
        } else {
            // Metallic / Diffuse PBR sampling (+ optional diffuse translucency)
            float3 F0 = lerp(float3(0.04, 0.04, 0.04), payload.albedo, metallic);
            float3 F = F_Schlick(max(0.0, dot(N, V)), F0);

            float specProb = max(F.x, max(F.y, F.z));
            float baseDiffProb = (1.0 - specProb) * (1.0 - metallic) * (1.0 - transmission);
            float transProb = baseDiffProb * saturate(payload.translucency);
            float diffProb = max(0.0, baseDiffProb - transProb);
            float totalProb = specProb + diffProb + transProb;

            float pick = next_float(rng) * totalProb;
            float cosineTerm = 1.0;

            if (pick < specProb) {
                // Specular GGX
                if (specularBounces >= (int)maxSpecularBounces) break;
                specularBounces++;

                float3 H = SampleGGX(u, N, roughness);
                nextDir = reflect(-V, H);
                float NdotL = saturate(dot(N, nextDir));
                float NdotH = saturate(dot(N, H));
                float VdotH = saturate(dot(V, H));

                pdf = (PDF_GGX(NdotH, VdotH, roughness) * specProb) / totalProb;
                f_brdf = D_GGX(NdotH, roughness) * V_SmithCorrelated(max(0.0, dot(N, V)), NdotL, roughness) * F_Schlick(VdotH, F0);
                rayOrigin = P + N * 0.001;
                cosineTerm = NdotL;
                currentRayType = RAY_TYPE_DIFFUSE;
            } else if (pick < (specProb + diffProb)) {
                // Diffuse Lambert
                if (giBounces >= (int)maxGIBounces) break;
                giBounces++;

                nextDir = SampleLambert(u, N);
                float NdotL = saturate(dot(N, nextDir));
                pdf = (PDF_Lambert(NdotL) * diffProb) / totalProb;
                f_brdf = diffuseAlbedo / PI;
                rayOrigin = P + N * 0.001;
                cosineTerm = NdotL;
                currentRayType = RAY_TYPE_REFLECTION;
            } else {
                // Diffuse translucency (transmission) Lambert
                if (giBounces >= (int)maxGIBounces) break;
                giBounces++;

                nextDir = SampleLambert(u, -N);
                float NdotL_t = saturate(dot(-N, nextDir));
                pdf = (PDF_Lambert(NdotL_t) * transProb) / totalProb;
                f_brdf = (payload.albedo / PI) * (1.0 - metallic);
                rayOrigin = P - N * 0.001;
                cosineTerm = NdotL_t;
                currentRayType = RAY_TYPE_REFLECTION;
            }

            if (!(pdf > 0.0)) break;
            throughput *= (f_brdf * cosineTerm) / pdf;
            rayDir = nextDir;
            
            // Russian Roulette
            if (bounce > 2) {
                float p = max(throughput.x, max(throughput.y, throughput.z));
                if (p <= 0.0 || next_float(rng) > p) break;
                throughput /= p;
            }

            continue;
        }

        if (!(pdf > 0.0)) break;

        throughput *= (f_brdf * saturate(dot(N, nextDir))) / pdf;
        rayDir = nextDir;

        // Russian Roulette
        if (bounce > 2) {
            float p = max(throughput.x, max(throughput.y, throughput.z));
            if (p <= 0.0 || next_float(rng) > p) break;
            throughput /= p;
        }
    }

    // Final result with aggressive firefly suppression for Archviz
    if (any(isnan(accumulatedColor)) || any(isinf(accumulatedColor))) accumulatedColor = float3(0,0,0);

    // Radiance must be non-negative. Clamp numerical underflow/instability.
    float3 finalColor = clamp(accumulatedColor * intensity, 0.0, 1000.0);
    if (any(isnan(finalColor)) || any(isinf(finalColor))) finalColor = float3(0, 0, 0);

    // Write DLSS inputs
    static const float2 kInvalidMvec = float2(-1e6, -1e6);
    float2 currScreen = float2(launchIndex.xy) + 0.5;
    float2 screenDim = float2(launchDim.xy);
    float2 screenMin = float2(-0.5, -0.5);
    float2 screenMax = screenDim + float2(0.5, 0.5);

    if (!primaryHit || primaryViewZ <= 0.0) {
        // Depth semantics depend on the Streamline feature:
        // - DLSS-SR: HW/NDC depth in [0,1] (far plane = 1)
        // - DLSS-RR: linear view-space depth (positive forward)
        g_depth[launchIndex.xy] = (dlssRayReconstruction > 0.5) ? farZ : 1.0;

        // Stabilize sky/background: compute motion from camera rotation (and translation has no effect at infinity).
        float2 mvecSky = float2(0.0, 0.0);
        if (prevValid > 0.5) {
            float3 forwardP = normalize(prevForward);
            float3 Rp = normalize(cross(forwardP, prevUp));
            float3 Up = normalize(cross(Rp, forwardP));
            float f_inv_p = tan(radians(prevFov) * 0.5);

            float vxP = dot(rayDirCenter, Rp);
            float vyP = dot(rayDirCenter, Up);
            float vzP = dot(rayDirCenter, forwardP);
            if (vzP > 0.001) {
                float ndcXP = vxP / (vzP * prevAspect * f_inv_p);
                float ndcYP = -vyP / (vzP * f_inv_p);
                float2 prevScreen = (float2(ndcXP, ndcYP) * 0.5 + 0.5) * float2(launchDim.xy);
                if (any(prevScreen < screenMin) || any(prevScreen > screenMax)) {
                    mvecSky = kInvalidMvec;
                } else {
                    mvecSky = prevScreen - currScreen;
                }
            }
        }
        g_motionVectors[launchIndex.xy] = mvecSky;
        g_albedoOut[launchIndex.xy] = float4(0.0, 0.0, 0.0, 1.0);
        g_normalRoughnessOut[launchIndex.xy] = float4(0.0, 1.0, 0.0, 1.0);
        g_specularAlbedo[launchIndex.xy] = float4(0.0, 0.0, 0.0, 1.0);
        g_specHitDistance[launchIndex.xy] = 0.0;
        g_specularMotionVectors[launchIndex.xy] = mvecSky;
    } else {
        if (dlssRayReconstruction > 0.5) {
            // Linear view-space depth (positive forward)
            g_depth[launchIndex.xy] = primaryViewZ;
        } else {
            // HW/NDC depth in [0,1] compatible with cameraViewToClip.
            float nearZc = nearZ;
            float farZc = farZ;
            float A = farZc / (farZc - nearZc);
            float B = (-nearZc * farZc) / (farZc - nearZc);
            float ndcZ = A + (B / primaryViewZ);
            g_depth[launchIndex.xy] = saturate(ndcZ);
        }

        // Motion vectors: avoid using jittered hit position (primaryPos) since
        // that bakes jitter into mvec and causes visible shaking with DLSS.
        float2 mvec = kInvalidMvec;
        float2 specMvec = kInvalidMvec;

        if (prevValid > 0.5) {
            float3 forwardP = normalize(prevForward);
            float3 Rp = normalize(cross(forwardP, prevUp));
            float3 Up = normalize(cross(Rp, forwardP));
            float f_inv_p = tan(radians(prevFov) * 0.5);

            // Reconstruct a stable world point from pixel center + depth (no jitter).
            float viewZc = primaryViewZ;
            float3 P_world = camPos + R * (x_view_center * viewZc) + U * (y_view_center * viewZc) + forward * viewZc;

            float3 relP = P_world - prevPos;
            float vxP = dot(relP, Rp);
            float vyP = dot(relP, Up);
            float vzP = dot(relP, forwardP);
            if (vzP > 0.001) {
                float ndcXP = vxP / (vzP * prevAspect * f_inv_p);
                float ndcYP = -vyP / (vzP * f_inv_p);
                float2 prevScreen = (float2(ndcXP, ndcYP) * 0.5 + 0.5) * float2(launchDim.xy);
                if (any(prevScreen < screenMin) || any(prevScreen > screenMax)) {
                    mvec = kInvalidMvec;
                } else {
                    mvec = prevScreen - currScreen;
                }
            }

            // Specular MV: approximate with surface MV for stability.
            if (any(primarySpecAlbedo > 0.0)) {
                specMvec = mvec;
            }
        }
        g_motionVectors[launchIndex.xy] = mvec;
        g_specularMotionVectors[launchIndex.xy] = specMvec;
        g_albedoOut[launchIndex.xy] = float4(primaryAlbedo, 1.0);
        g_normalRoughnessOut[launchIndex.xy] = float4(normalize(primaryNormal), primaryRoughness);
        g_specularAlbedo[launchIndex.xy] = float4(primarySpecAlbedo, 1.0);
        g_specHitDistance[launchIndex.xy] = primarySpecHitDist;
    }

    // Debug: Motion Vectors (debug mode index = 8)
    // Visualizes g_motionVectors in pixel units. Yellow-ish means near-zero MV.
    if (SHADER_DEBUG_MODE == 8.0) {
        float2 mv = g_motionVectors[launchIndex.xy];
        // Handle our invalid sentinel.
        if (abs(mv.x) > 1e5 || abs(mv.y) > 1e5) {
            mv = float2(0.0, 0.0);
        }
        // Visualize in a fixed pixel scale so typical camera motion is visible.
        // 32 pixels = full scale.
        const float kMvPixelsForFullScale = 32.0;
        float2 mvVis = mv / kMvPixelsForFullScale;
        mvVis = clamp(mvVis, float2(-1.0, -1.0), float2(1.0, 1.0));
        float mag = saturate(length(mv) / kMvPixelsForFullScale);
        float3 col = float3(0.5 + 0.5 * mvVis.x, 0.5 + 0.5 * mvVis.y, mag);
        g_output[launchIndex.xy] = float4(col, 1.0);
        return;
    }

    // Debug: Specular Hit Distance (debug mode index = 9)
    if (SHADER_DEBUG_MODE == 9.0) {
        float d = g_specHitDistance[launchIndex.xy];
        float v = saturate(d / max(farZ, 1e-3));
        g_output[launchIndex.xy] = float4(v, v, v, 1.0);
        return;
    }

    // Debug: Specular Motion Vectors (debug mode index = 10)
    if (SHADER_DEBUG_MODE == 10.0) {
        float2 mv = g_specularMotionVectors[launchIndex.xy];
        // Handle our invalid sentinel.
        if (abs(mv.x) > 1e5 || abs(mv.y) > 1e5) {
            mv = float2(0.0, 0.0);
        }
        float2 mvNorm = mv / float2(launchDim.xy);
        float3 col = float3(0.5 + 0.5 * mvNorm.x, 0.5 + 0.5 * mvNorm.y,
                            saturate(length(mvNorm) * 2.0));
        g_output[launchIndex.xy] = float4(col, 1.0);
        return;
    }

    // Final NaN/Inf check and clamping before accumulation
    if (!any(isfinite(finalColor))) finalColor = float3(0,0,0);
    finalColor = clamp(finalColor, 0.0, 100.0); // Hard clamp to prevent firefly corruption

    float lum = dot(finalColor, float3(0.2126, 0.7152, 0.0722));
    if (!isfinite(lum)) lum = 0.0;
    bool historyRepairedThisFrame = false;

    if (accumFrame == 0) {
        g_accumulation[launchIndex.xy] = float4(finalColor, 1.0);
        g_variance[launchIndex.xy] = 0.0;
        g_output[launchIndex.xy] = float4(finalColor, 1.0);
    } else {
        float4 prev_accum = g_accumulation[launchIndex.xy];
        float n = prev_accum.a;
        float3 oldSum = prev_accum.rgb;

        bool invalidHistory = !isfinite(n) || (n < 1.0) || any(!isfinite(oldSum));
        if (invalidHistory) {
            historyRepairedThisFrame = true;
            n = 1.0;
            oldSum = finalColor;
        }

        float safeN = max(n, 1.0);
        float oldMeanLum = dot(oldSum / safeN, float3(0.2126, 0.7152, 0.0722));
        
        float next_n = safeN + 1.0;
        float3 nextSum = oldSum + finalColor;
        float3 nextMean = nextSum / next_n;
        float nextMeanLum = dot(nextMean, float3(0.2126, 0.7152, 0.0722));
        
        // Welford's Online Variance: M2_n = M2_{n-1} + (x - mu_{n-1})(x - mu_n)
        float prev_M2 = invalidHistory ? 0.0 : g_variance[launchIndex.xy];
        float next_M2 = prev_M2 + (lum - oldMeanLum) * (lum - nextMeanLum);
        if (isnan(next_M2) || isinf(next_M2)) next_M2 = 0.0;
        next_M2 = max(0.0, next_M2);
        
        g_accumulation[launchIndex.xy] = float4(nextSum, next_n);
        g_variance[launchIndex.xy] = next_M2;
        
        if (SHADER_DEBUG_VIS_MODE == 1.0) {
             // Standard Error of Mean: SEM = sqrt(M2) / N
             float sem = sqrt(next_M2) / next_n;
             // Coefficient of Variation
             float noise = sem / (max(0.01, nextMeanLum) + 0.001); 
             
             // Visualize: 0% = Black, 20% = White (Scaled 5x)
             float vis = saturate(noise * 5.0);
             // Active (Noisy) pixels in Red-ish/Gray to contrast with Green converged pixels
             g_output[launchIndex.xy] = float4(vis, vis * 0.5, vis * 0.5, 1.0);
        } else {
            g_output[launchIndex.xy] = float4(nextMean, 1.0);
        }
    }

    // Debug: Accumulation sample count N (debug mode index = 17)
    if (SHADER_DEBUG_MODE == 17.0) {
        float n = g_accumulation[launchIndex.xy].a;
        float maxN = max(maxSPP, 1.0);
        float v = saturate(n / maxN);
        // Blue->Cyan->White ramp for quick low/high N detection.
        float3 col = lerp(float3(0.0, 0.0, 0.35), float3(0.2, 0.9, 1.0), sqrt(v));
        g_output[launchIndex.xy] = float4(col, 1.0);
        return;
    }

    // Debug: History validity / corruption mask (debug mode index = 18)
    if (SHADER_DEBUG_MODE == 18.0) {
        float4 accDbg = g_accumulation[launchIndex.xy];
        float varDbg = g_variance[launchIndex.xy];
        float n = accDbg.a;
        bool invalid = (n < 0.0) || isnan(n) || isinf(n) ||
                       any(isnan(accDbg.rgb)) || any(isinf(accDbg.rgb)) ||
                       isnan(varDbg) || isinf(varDbg) || (varDbg < 0.0);
        // Red = invalid, green = valid and initialized, blue = n==0 (not yet accumulated).
        float3 col = invalid ? float3(1.0, 0.0, 0.0) : ((n <= 0.5) ? float3(0.0, 0.0, 1.0) : float3(0.0, 1.0, 0.0));
        g_output[launchIndex.xy] = float4(col, 1.0);
        return;
    }

    // Debug: Per-pixel estimated noise from history (debug mode index = 19)
    if (SHADER_DEBUG_MODE == 19.0) {
        float4 accDbg = g_accumulation[launchIndex.xy];
        float n = max(accDbg.a, 1.0);
        float meanLum = dot(accDbg.rgb / n, float3(0.2126, 0.7152, 0.0722));
        float m2 = max(0.0, g_variance[launchIndex.xy]);
        float sem = sqrt(m2) / n;
        float noise = sem / (max(0.01, meanLum) + 0.001);
        float v = saturate(noise * 5.0);
        // Black->Orange->Red heatmap.
        float3 col = lerp(float3(0.0, 0.0, 0.0), float3(1.0, 0.35, 0.0), v);
        g_output[launchIndex.xy] = float4(col, 1.0);
        return;
    }

    // Debug: Sample deficit vs expected history count (debug mode index = 20)
    if (SHADER_DEBUG_MODE == 20.0) {
        float n = g_accumulation[launchIndex.xy].a;
        float expectedN = max(1.0, accumFrame + 1.0);
        float deficit = max(0.0, expectedN - n);
        float v = saturate(deficit / expectedN);
        // Black = on-track, Magenta/White = lagging/reset pixels.
        float3 col = lerp(float3(0.0, 0.0, 0.0), float3(1.0, 0.0, 1.0), sqrt(v));
        g_output[launchIndex.xy] = float4(col, 1.0);
        return;
    }

    // Debug: Recent reset mask (debug mode index = 21)
    if (SHADER_DEBUG_MODE == 21.0) {
        float n = g_accumulation[launchIndex.xy].a;
        bool recentReset = (accumFrame > 4.0) && (n <= 2.0);
        // Yellow = history repaired this frame, Red = very low sample count.
        float3 col = historyRepairedThisFrame ? float3(1.0, 1.0, 0.0) :
                     (recentReset ? float3(1.0, 0.0, 0.0) : float3(0.0, 0.0, 0.0));
        g_output[launchIndex.xy] = float4(col, 1.0);
        return;
    }
}


