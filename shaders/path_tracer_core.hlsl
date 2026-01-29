// shaders/path_tracer_core.hlsl
// Main Path Tracing RayGen shader with Accumulation and ReSTIR DI

#include "raytracing/common.hlsli"
#include "random_lib.hlsl"
#include "lights_lib.hlsl"
#include "restir_lib.hlsl"

// Additional resources for ReSTIR
StructuredBuffer<Light> g_lights : register(t5000);
RWTexture2D<float4> g_reservoir0 : register(u2);
RWTexture2D<float4> g_reservoir1 : register(u3);
RWTexture2D<float4> g_gi_reservoir_a0 : register(u4);
RWTexture2D<float4> g_gi_reservoir_a1 : register(u5);
RWTexture2D<float4> g_gi_reservoir_a2 : register(u6);
RWTexture2D<float4> g_gi_reservoir_b0 : register(u7);
RWTexture2D<float4> g_gi_reservoir_b1 : register(u8);
RWTexture2D<float4> g_gi_reservoir_b2 : register(u9);

#include "brdf_lib.hlsl"

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
    r.w_sum = data.y;
    r.M = asuint(data.z);
    r.W = data.w;
    return r;
}

float4 pack_reservoir(Reservoir r) {
    return float4(asfloat(r.lightIndex), r.w_sum, asfloat(r.M), r.W);
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
    uint frame = (uint)frameCount;

    if (maxSPP > 0.0 && frame >= (uint)maxSPP) {
        float4 total = g_accumulation[launchIndex.xy];
        if (total.a > 0.0) {
            g_output[launchIndex.xy] = float4(LinearToSRGB(ToneMap(total.rgb / total.a)), 1.0);
        }
        return;
    }

    RNG rng = init_rng(launchIndex.xy, frame);

    // Swap reservoirs per frame for ReSTIR
    bool flip = (frame % 2) == 1;

    // Deterministic per-frame jitter (pixel units) for DLSS/TAA friendliness
    float2 jitter = float2(halton(frame + 1, 2), halton(frame + 1, 3)) - 0.5;
    float2 uv = (float2(launchIndex.xy) + 0.5 + jitter) / float2(launchDim.xy);
    float2 ndc = uv * 2.0 - 1.0;

    float f_inv = tan(radians(fov) * 0.5);
    float3 forward = normalize(camForward);
    float3 R = normalize(cross(forward, camUp));
    float3 U = normalize(cross(R, forward));
    
    float y_view = (-ndc.y) * f_inv;
    float x_view = ndc.x * aspect * f_inv;
    
    float3 rayDir = normalize(x_view * R + y_view * U + forward);
    float3 rayOrigin = camPos;

    float3 accumulatedColor = float3(0, 0, 0);
    float3 throughput = float3(1, 1, 1);

    // Primary hit info for DLSS inputs
    bool primaryHit = false;
    float3 primaryPos = float3(0, 0, 0);
    float3 primaryNormal = float3(0, 1, 0);
    float3 primaryAlbedo = float3(0, 0, 0);
    float primaryRoughness = 1.0;
    float primaryViewZ = -1.0;
    
    int specularBounces = 0;
    int refractiveBounces = 0;
    int giBounces = 0;

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
        payload.matIndex = 0;
        payload.t = -1.0;

        TraceRay(g_accel, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);

        if (bounce == 0 && payload.t >= 0.0) {
            primaryHit = true;
            primaryPos = payload.position;
            primaryNormal = payload.normal;
            primaryAlbedo = payload.albedo;
            primaryRoughness = max(0.001, payload.roughness);
            // View-space z (positive forward)
            primaryViewZ = dot(primaryPos - camPos, forward);
        }

        if (payload.t < 0.0) {
            // Miss: add sky color and terminate
            accumulatedColor += throughput * payload.color;
            
            // On first bounce miss, update reservoir to empty
            if (bounce == 0) {
                float4 res_data = pack_reservoir(init_reservoir());
                if (flip) g_reservoir1[launchIndex.xy] = res_data;
                else      g_reservoir0[launchIndex.xy] = res_data;
            }
            break;
        }

        if (debugMode != 0) {
            accumulatedColor = payload.color;
            break;
        }

        float3 N = payload.normal;
        float3 P = payload.position;
        float3 V = -rayDir;
        float roughness = max(0.001, payload.roughness);
        float metallic = payload.metalness;

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
                float3 brdf = (payload.albedo / PI) * (1.0 - metallic) + spec;

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
                float3 brdf = (payload.albedo / PI) * (1.0 - metallic) + spec;

                float p_target = calculate_p_target(radiance, payload.albedo, brdf, NdotL) * (float)numLights;
                update_reservoir(res, lightIdx, p_target, rng);
            }

            // B. Temporal Resampling
            // ... (keep current temporal resampling, but use refined brdf for candidate evaluation if possible) ...
            if (frame > 0) {
                float4 prev_data;
                if (flip) prev_data = g_reservoir0[launchIndex.xy];
                else      prev_data = g_reservoir1[launchIndex.xy];
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
                float3 brdf_prev = (payload.albedo / PI) * (1.0 - metallic) + spec;

                float p_target_at_curr = calculate_p_target(radiance_prev, payload.albedo, brdf_prev, saturate(dot(N, L_prev)));
                combine_reservoirs(res, prev_res, p_target_at_curr, rng);
            }

            // C. Spatial Resampling (Neighbor Pixels)
            if (frame > 0) {
                for (int i = 0; i < 2; ++i) {
                    int2 offset = int2((next_float(rng) - 0.5) * 20.0, (next_float(rng) - 0.5) * 20.0);
                    int2 neighborCoords = clamp(int2(launchIndex.xy) + offset, int2(0,0), int2(launchDim.xy)-1);
                    
                    float4 neighbor_data;
                    if (flip) neighbor_data = g_reservoir0[neighborCoords];
                    else      neighbor_data = g_reservoir1[neighborCoords];
                    Reservoir neighbor_res = unpack_reservoir(neighbor_data);
                    neighbor_res.M = min(neighbor_res.M, 30); // Cap neighbor contribution
                    
                    // Re-evaluate neighbor light candidate at current shading point
                    float3 L_neigh;
                    float3 radiance_neigh;
                    if (neighbor_res.lightIndex == 0xFFFFFFFF) {
                        L_neigh = normalize(lightDir.xyz);
                        radiance_neigh = lightColor.rgb * lightColor.w;
                    } else if (neighbor_res.lightIndex < numLights) {
                        Light l = g_lights[neighbor_res.lightIndex];
                        L_neigh = l.position - P;
                        float dist = length(L_neigh);
                        L_neigh /= dist;
                        radiance_neigh = l.color * (l.intensity / (dist * dist + 1.0));
                    } else {
                        radiance_neigh = float3(0,0,0);
                    }

                    float NdotL_neigh = saturate(dot(N, L_neigh));
                    float3 F0 = lerp(float3(0.04, 0.04, 0.04), payload.albedo, metallic);
                    float3 H = normalize(L_neigh + V);
                    float3 spec = D_GGX(max(0.0, dot(N, H)), roughness) * V_SmithCorrelated(max(0.0, dot(N, V)), NdotL_neigh, roughness) * F_Schlick(max(0.0, dot(H, V)), F0);
                    float3 brdf_neigh = (payload.albedo / PI) * (1.0 - metallic) + spec;

                    float p_target_at_curr = calculate_p_target(radiance_neigh, payload.albedo, brdf_neigh, NdotL_neigh);
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
            float3 brdf_f = (payload.albedo / PI) * (1.0 - metallic) + spec_f;

            float p_target_final = calculate_p_target(radiance_final, payload.albedo, brdf_f, NdotL_final);
            
            // Finalize reservoir BEFORE storing (so W is valid in next frame)
            finalize_reservoir(res, p_target_final);

            // Store ReSTIR state for next frame
            float4 packed_res = pack_reservoir(res);
            if (flip) g_reservoir1[launchIndex.xy] = packed_res;
            else      g_reservoir0[launchIndex.xy] = packed_res;

            // D. Apply Visibility for current frame shading
            if (p_target_final > 0.0) {
                RayDesc shadowRay;
                shadowRay.Origin = P + N * 0.001;
                shadowRay.Direction = L_final;
                shadowRay.TMin = 0.001;
                shadowRay.TMax = dist_final - 0.002;
                RayPayload shadowPayload;
                shadowPayload.t = 1.0;
                TraceRay(g_accel, RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xFF, 0, 0, 0, shadowRay, shadowPayload);
                
                if (shadowPayload.t < 0.0) {
                    directLighting = radiance_final * brdf_f * NdotL_final * res.W;
                }
            }

            // --- ReSTIR GI (Indirect Illumination) ---
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
                    f_brdf_gi = (payload.albedo / PI) * (1.0 - metallic);
                }
                if (pdf_gi > 0.0) {
                    RayDesc giRay; giRay.Origin = P + N * 0.0005; giRay.Direction = nextDir_gi;
                    giRay.TMin = 0.0001; giRay.TMax = 1000.0;
                    RayPayload giPayload; giPayload.color = float3(0,0,0); giPayload.emissive = float3(0,0,0); giPayload.t = -1.0;
                    TraceRay(g_accel, RAY_FLAG_NONE, 0xFF, 0, 0, 0, giRay, giPayload);
                    if (giPayload.t > 0) {
                        float3 radiance = giPayload.color + giPayload.emissive;
                        float p_target = length(radiance * f_brdf_gi * saturate(dot(N, nextDir_gi)));
                        update_gi_reservoir(gi_res, giPayload.position, radiance, (p_target / pdf_gi), rng);
                    }
                }
            }
            // B. Temporal Resampling
            if (frame > 0) {
                float4 d0, d1, d2;
                if (flip) { d0 = g_gi_reservoir_b0[launchIndex.xy]; d1 = g_gi_reservoir_b1[launchIndex.xy]; d2 = g_gi_reservoir_b2[launchIndex.xy]; }
                else      { d0 = g_gi_reservoir_a0[launchIndex.xy]; d1 = g_gi_reservoir_a1[launchIndex.xy]; d2 = g_gi_reservoir_a2[launchIndex.xy]; }
                GI_Reservoir prev_gi = unpack_gi_reservoir(d0, d1, d2);
                prev_gi.M = min(prev_gi.M, 15);
                float3 L_gi = normalize(prev_gi.hitPos - P);
                float3 F0 = lerp(float3(0.04, 0.04, 0.04), payload.albedo, metallic);
                float3 H = normalize(L_gi + V);
                float3 spec = D_GGX(max(0.0, dot(N, H)), roughness) * G_Smith(max(0.0, dot(N, V)), saturate(dot(N, L_gi)), roughness) * F_Schlick(max(0.0, dot(H, V)), F0) / (4.0 * max(0.0, dot(N, V)) * saturate(dot(N, L_gi)) + 0.001);
                float3 brdf = (payload.albedo / PI) * (1.0 - metallic) + spec;
                float p_target_at_curr = length(prev_gi.radiance * brdf * saturate(dot(N, L_gi)));
                combine_gi_reservoirs(gi_res, prev_gi, p_target_at_curr, rng);
            }
            // C. Spatial Resampling
            if (frame > 0) {
                for (int i = 0; i < 2; ++i) {
                    int2 offset = int2((next_float(rng) - 0.5) * 30.0, (next_float(rng) - 0.5) * 30.0);
                    int2 neighborCoords = clamp(int2(launchIndex.xy) + offset, int2(0,0), int2(launchDim.xy)-1);
                    float4 d0, d1, d2;
                    if (flip) { d0 = g_gi_reservoir_b0[neighborCoords]; d1 = g_gi_reservoir_b1[neighborCoords]; d2 = g_gi_reservoir_b2[neighborCoords]; }
                    else      { d0 = g_gi_reservoir_a0[neighborCoords]; d1 = g_gi_reservoir_a1[neighborCoords]; d2 = g_gi_reservoir_a2[neighborCoords]; }
                    GI_Reservoir neigh_gi = unpack_gi_reservoir(d0, d1, d2);
                    neigh_gi.M = min(neigh_gi.M, 15);
                    float3 L_gi = normalize(neigh_gi.hitPos - P);
                    float3 F0 = lerp(float3(0.04, 0.04, 0.04), payload.albedo, metallic);
                    float3 H = normalize(L_gi + V);
                    float3 spec = D_GGX(max(0.0, dot(N, H)), roughness) * G_Smith(max(0.0, dot(N, V)), saturate(dot(N, L_gi)), roughness) * F_Schlick(max(0.0, dot(H, V)), F0) / (4.0 * max(0.0, dot(N, V)) * saturate(dot(N, L_gi)) + 0.001);
                    float3 brdf = (payload.albedo / PI) * (1.0 - metallic) + spec;
                    float p_target_at_curr = length(neigh_gi.radiance * brdf * saturate(dot(N, L_gi)));
                    combine_gi_reservoirs(gi_res, neigh_gi, p_target_at_curr, rng);
                }
            }
            // Finalize GI
            float3 L_gi_final = normalize(gi_res.hitPos - P);
            float3 F0_gi = lerp(float3(0.04, 0.04, 0.04), payload.albedo, metallic);
            float3 H_gi = normalize(L_gi_final + V);
            float3 spec_gi = D_GGX(max(0.0, dot(N, H_gi)), roughness) * G_Smith(max(0.0, dot(N, V)), saturate(dot(N, L_gi_final)), roughness) * F_Schlick(max(0.0, dot(H_gi, V)), F0_gi) / (4.0 * max(0.0, dot(N, V)) * saturate(dot(N, L_gi_final)) + 0.001);
            float3 brdf_gi_final = (payload.albedo / PI) * (1.0 - metallic) + spec_gi;
            float p_target_final_gi = length(gi_res.radiance * brdf_gi_final * saturate(dot(N, L_gi_final)));
            finalize_gi_reservoir(gi_res, p_target_final_gi);
            float4 out_d0, out_d1, out_d2; pack_gi_reservoir(gi_res, out_d0, out_d1, out_d2);
            if (flip) { g_gi_reservoir_a0[launchIndex.xy] = out_d0; g_gi_reservoir_a1[launchIndex.xy] = out_d1; g_gi_reservoir_a2[launchIndex.xy] = out_d2; }
            else      { g_gi_reservoir_b0[launchIndex.xy] = out_d0; g_gi_reservoir_b1[launchIndex.xy] = out_d1; g_gi_reservoir_b2[launchIndex.xy] = out_d2; }
            
            if (gi_res.W > 0.0) {
                // Visibility test for GI reconnection
                RayDesc giVisRay; giVisRay.Origin = P + N * 0.001; giVisRay.Direction = L_gi_final;
                giVisRay.TMin = 0.001; giVisRay.TMax = distance(gi_res.hitPos, P) - 0.002;
                RayPayload giVisPayload; giVisPayload.t = 1.0;
                TraceRay(g_accel, RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xFF, 0, 0, 0, giVisRay, giVisPayload);
                if (giVisPayload.t < 0.0) {
                    indirectLighting = gi_res.radiance * brdf_gi_final * saturate(dot(N, L_gi_final)) * gi_res.W;
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
                TraceRay(g_accel, RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xFF, 0, 0, 0, shadowRay, shadowPayload);
                if (shadowPayload.t < 0.0) {
                     float3 F0 = lerp(float3(0.04, 0.04, 0.04), payload.albedo, metallic);
                     float3 H = normalize(L_nee + V);
                     float3 spec = D_GGX(max(0.0, dot(N, H)), roughness) * V_SmithCorrelated(max(0.0, dot(N, V)), NdotL_nee, roughness) * F_Schlick(max(0.0, dot(H, V)), F0);
                     float3 brdf = (payload.albedo / PI) * (1.0 - metallic) + spec;
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
            if (SampleGlass(V, N, payload.ior, u, glassL)) {
                // Refracted
                if (refractiveBounces >= (int)maxRefractiveBounces) break;
                refractiveBounces++;
                nextDir = glassL;
                f_brdf = payload.refractionColor;
            } else {
                // Reflected
                if (specularBounces >= (int)maxSpecularBounces) break;
                specularBounces++;
                nextDir = glassL;
                f_brdf = float3(1,1,1);
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
            // Metallic / Diffuse PBR sampling
            float3 F0 = lerp(float3(0.04, 0.04, 0.04), payload.albedo, metallic);
            float3 F = F_Schlick(max(0.0, dot(N, V)), F0);
            
            float specProb = max(F.x, max(F.y, F.z));
            float diffProb = (1.0 - specProb) * (1.0 - metallic);
            float totalProb = specProb + diffProb;
            
            if (next_float(rng) * totalProb < specProb) {
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
            } else {
                // Diffuse Lambert
                if (giBounces >= (int)maxGIBounces) break;
                giBounces++;

                nextDir = SampleLambert(u, N);
                float NdotL = saturate(dot(N, nextDir));
                pdf = (PDF_Lambert(NdotL) * diffProb) / totalProb;
                f_brdf = (payload.albedo / PI) * (1.0 - metallic);
            }
            rayOrigin = P + N * 0.001;
        }

        if (pdf <= 0.0) break;

        throughput *= (f_brdf * saturate(dot(N, nextDir))) / pdf;
        rayDir = nextDir;

        // Russian Roulette
        if (bounce > 2) {
            float p = max(throughput.x, max(throughput.y, throughput.z));
            if (p <= 0.0 || next_float(rng) > p) break;
            throughput /= p;
        }
    }

    // Safety check on final result
    // Safety check on final result - check all components
    if (any(isnan(accumulatedColor)) || any(isinf(accumulatedColor))) accumulatedColor = float3(0,0,0);


    float3 finalColor = accumulatedColor * intensity;

    // Write DLSS inputs
    if (!primaryHit || primaryViewZ <= 0.0) {
        g_depth[launchIndex.xy] = 1.0;
        g_motionVectors[launchIndex.xy] = float2(0.0, 0.0);
        g_albedoOut[launchIndex.xy] = float4(0.0, 0.0, 0.0, 1.0);
        g_normalRoughnessOut[launchIndex.xy] = float4(0.0, 1.0, 0.0, 1.0);
    } else {
        float nearZc = nearZ;
        float farZc = farZ;
        float A = farZc / (farZc - nearZc);
        float B = (-nearZc * farZc) / (farZc - nearZc);
        float ndcZ = A + (B / primaryViewZ);
        g_depth[launchIndex.xy] = saturate(ndcZ);

        // Current NDC XY (no jitter)
        float f_inv_c = tan(radians(fov) * 0.5);
        float3 rel = primaryPos - camPos;
        float viewX = dot(rel, R);
        float viewY = dot(rel, U);
        float viewZ = dot(rel, forward);
        float ndcX = viewX / (viewZ * aspect * f_inv_c);
        float ndcY = -viewY / (viewZ * f_inv_c);
        float2 currScreen = (float2(ndcX, ndcY) * 0.5 + 0.5) * float2(launchDim.xy);

        float2 mvec = float2(0.0, 0.0);
        if (prevValid > 0.5) {
            float3 forwardP = normalize(prevForward);
            float3 Rp = normalize(cross(forwardP, prevUp));
            float3 Up = normalize(cross(Rp, forwardP));
            float f_inv_p = tan(radians(prevFov) * 0.5);
            float3 relP = primaryPos - prevPos;
            float vxP = dot(relP, Rp);
            float vyP = dot(relP, Up);
            float vzP = dot(relP, forwardP);
            float ndcXP = vxP / (vzP * prevAspect * f_inv_p);
            float ndcYP = -vyP / (vzP * f_inv_p);
            float2 prevScreen = (float2(ndcXP, ndcYP) * 0.5 + 0.5) * float2(launchDim.xy);
            mvec = prevScreen - currScreen;
        }
        g_motionVectors[launchIndex.xy] = mvec;
        g_albedoOut[launchIndex.xy] = float4(primaryAlbedo, 1.0);
        g_normalRoughnessOut[launchIndex.xy] = float4(normalize(primaryNormal), primaryRoughness);
    }

    if (frame == 0) {
        g_accumulation[launchIndex.xy] = float4(finalColor, 1.0);
        g_output[launchIndex.xy] = float4(LinearToSRGB(ToneMap(finalColor)), 1.0);
    } else {
        float4 prev_accum = g_accumulation[launchIndex.xy];
        float3 total_accum_color = prev_accum.rgb + finalColor;
        float total_samples = prev_accum.a + 1.0;
        
        g_accumulation[launchIndex.xy] = float4(total_accum_color, total_samples);
        g_output[launchIndex.xy] = float4(LinearToSRGB(ToneMap(total_accum_color / total_samples)), 1.0);
    }
}


