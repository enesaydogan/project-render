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

#include "brdf_lib.hlsl"

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

    // Jittered sample for anti-aliasing
    float2 jitter = next_float2(rng) - 0.5;
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
                float3 spec = D_GGX(max(0.0, dot(N, H)), roughness) * G_Smith(max(0.0, dot(N, V)), NdotL, roughness) * F_Schlick(max(0.0, dot(H, V)), F0) / (4.0 * max(0.0, dot(N, V)) * NdotL + 0.001);
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
                float3 spec = D_GGX(max(0.0, dot(N, H)), roughness) * G_Smith(max(0.0, dot(N, V)), NdotL, roughness) * F_Schlick(max(0.0, dot(H, V)), F0) / (4.0 * max(0.0, dot(N, V)) * NdotL + 0.001);
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
                float3 spec = D_GGX(max(0.0, dot(N, H)), roughness) * G_Smith(max(0.0, dot(N, V)), NdotL_prev, roughness) * F_Schlick(max(0.0, dot(H, V)), F0) / (4.0 * max(0.0, dot(N, V)) * NdotL_prev + 0.001);
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
                    float3 spec = D_GGX(max(0.0, dot(N, H)), roughness) * G_Smith(max(0.0, dot(N, V)), NdotL_neigh, roughness) * F_Schlick(max(0.0, dot(H, V)), F0) / (4.0 * max(0.0, dot(N, V)) * NdotL_neigh + 0.001);
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
            float3 spec_f = D_GGX(max(0.0, dot(N, H_f)), roughness) * G_Smith(max(0.0, dot(N, V)), NdotL_final, roughness) * F_Schlick(max(0.0, dot(H_f, V)), F0) / (4.0 * max(0.0, dot(N, V)) * NdotL_final + 0.001);
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
                     float3 spec = D_GGX(max(0.0, dot(N, H)), roughness) * G_Smith(max(0.0, dot(N, V)), NdotL_nee, roughness) * F_Schlick(max(0.0, dot(H, V)), F0) / (4.0 * max(0.0, dot(N, V)) * NdotL_nee + 0.001);
                     float3 brdf = (payload.albedo / PI) * (1.0 - metallic) + spec;
                     directLighting = brdf * radiance_nee * NdotL_nee * 2.0; // *2 because of 50/50 sun/lights
                }
            }
        }

        accumulatedColor += throughput * (directLighting + payload.emissive);

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
                f_brdf = D_GGX(NdotH, roughness) * G_Smith(max(0.0, dot(N, V)), NdotL, roughness) * F_Schlick(VdotH, F0) / (4.0 * max(0.0, dot(N, V)) * NdotL + 0.001);
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


