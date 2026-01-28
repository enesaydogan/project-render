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

inline float3 LinearToSRGB(float3 color) {
    return pow(max(color, 0.0), 1.0/2.2);
}

// target PDF for ReSTIR DI (luminance of lit surface)
float calculate_p_target(float3 radiance, float3 brdf, float NdotL) {
    return length(radiance * brdf * NdotL);
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

    RNG rng = init_rng(launchIndex.xy, frame);

    // Swap reservoirs per frame
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
    
    float3 dir = normalize(x_view * R + y_view * U + forward);

    RayPayload payload;
    payload.t = -1.0;
    payload.color = float3(0,0,0);
    payload.emissive = float3(0,0,0);
    payload.albedo = float3(0,0,0);

    float3 acc_color = float3(0, 0, 0);
    float3 origin = camPos;

    // Primary Ray
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = dir;
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    TraceRay(g_accel, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);

    if (payload.t < 0.0) {
        acc_color = payload.color; // Miss shader color (skybox)
        float4 res_data = pack_reservoir(init_reservoir());
        if (flip) g_reservoir1[launchIndex.xy] = res_data;
        else      g_reservoir0[launchIndex.xy] = res_data;
    } else {
        // We hit a surface. Perform ReSTIR DI.
        float3 P = payload.position;
        float3 N = payload.normal;
        
        Reservoir res = init_reservoir();
        
        // 1. Initial Candidate Sampling (RIS)
        // Sample Sun
        {
            LightSample ls = evaluate_directional_light(lightDir.xyz, lightColor.rgb, lightColor.w);
            float NdotL = saturate(dot(N, ls.L));
            float p_target = calculate_p_target(ls.radiance, payload.albedo, NdotL);
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
            float p_target = calculate_p_target(radiance, payload.albedo, NdotL) * (float)numLights;
            update_reservoir(res, lightIdx, p_target, rng);
        }

        // 2. Temporal Resampling
        if (frame > 0) {
            float4 prev_data;
            if (flip) prev_data = g_reservoir0[launchIndex.xy];
            else      prev_data = g_reservoir1[launchIndex.xy];
            Reservoir prev_res = unpack_reservoir(prev_data);
            // Clamp M to avoid infinite history
            prev_res.M = min(prev_res.M, 30);
            
            // Re-evaluate previous candidate at current point
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
            
            float p_target_at_curr = calculate_p_target(radiance_prev, payload.albedo, saturate(dot(N, L_prev)));
            combine_reservoirs(res, prev_res, p_target_at_curr, rng);
        }

        // Store current reservoir
        float4 packed_res = pack_reservoir(res);
        if (flip) g_reservoir1[launchIndex.xy] = packed_res;
        else      g_reservoir0[launchIndex.xy] = packed_res;

        // 3. Final Shading with Visibility
        float3 direct_lighting = float3(0, 0, 0);
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
        float p_target_final = calculate_p_target(radiance_final, payload.albedo, NdotL_final);

        if (p_target_final > 0.0) {
            RayDesc shadowRay;
            shadowRay.Origin = P + N * 0.001;
            shadowRay.Direction = L_final;
            shadowRay.TMin = 0.001;
            shadowRay.TMax = dist_final - 0.002;
            RayPayload shadowPayload;
            shadowPayload.t = 1.0; // Assume shadowed
            TraceRay(g_accel, RAY_FLAG_SKIP_CLOSEST_HIT_SHADER | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xFF, 0, 0, 0, shadowRay, shadowPayload);
            
            if (shadowPayload.t < 0.0) { // Miss shader ran, so it's unshadowed
                finalize_reservoir(res, p_target_final);
                direct_lighting = radiance_final * payload.albedo * NdotL_final * res.W;
            }
        }
        
        acc_color = (direct_lighting + payload.emissive) * intensity;
    }

    float3 current_color = acc_color;

    // Progressive Accumulation logic (disabled for dynamic ReSTIR interactive view, 
    // but useful for final convergence)
    if (frame == 0) {
        g_accumulation[launchIndex.xy] = float4(current_color, 1.0);
        g_output[launchIndex.xy] = float4(LinearToSRGB(ToneMap(current_color)), 1.0);
    } else {
        float4 prev_accum = g_accumulation[launchIndex.xy];
        float3 total_accum_color = prev_accum.rgb + current_color;
        float total_samples = prev_accum.a + 1.0;
        
        g_accumulation[launchIndex.xy] = float4(total_accum_color, total_samples);
        g_output[launchIndex.xy] = float4(LinearToSRGB(ToneMap(total_accum_color / total_samples)), 1.0);
    }
}

