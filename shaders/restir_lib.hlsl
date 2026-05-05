// shaders/restir_lib.hlsl
// ReSTIR DI (Direct Illumination) Reservoir Management

#ifndef RESTIR_LIB_HLSL
#define RESTIR_LIB_HLSL

#include "random_lib.hlsl"

struct Reservoir
{
    uint lightIndex;  // Index of the selected light candidate
    float w_sum;      // Sum of weights (w_i)
    uint M;           // Number of samples processed
    float W;          // Resampling weight (w_sum / (M * p_target))
};

// target PDF for ReSTIR DI (luminance of lit surface)
inline float calculate_p_target(float3 radiance, float3 albedo, float3 f_brdf, float NdotL) {
    float p = length(max(0.0, radiance * f_brdf * NdotL));
    return min(p, 1e10); // Clamp to prevent infinity
}

Reservoir init_reservoir()
{
    Reservoir r;
    r.lightIndex = 0xFFFFFFFFu;
    r.w_sum = 0.0;
    r.M = 0;
    r.W = 0.0;
    return r;
}

inline Reservoir unpack_reservoir(float4 data)
{
    Reservoir r;
    r.lightIndex = isnan(data.x) ? 0xFFFFFFFFu : asuint(data.x);
    r.w_sum = data.y;
    r.M = asuint(data.z);
    r.W = data.w;
    if (isnan(r.w_sum) || isinf(r.w_sum)) r.w_sum = 0.0;
    if (isnan(r.W) || isinf(r.W)) r.W = 0.0;
    if (r.w_sum < 0.0 || r.W < 0.0) {
        r.w_sum = 0.0;
        r.W = 0.0;
        r.M = 0u;
        r.lightIndex = 0xFFFFFFFFu;
    }
    r.M = min(r.M, 1024u);
    return r;
}

inline float4 pack_reservoir(Reservoir r)
{
    return float4(asfloat(r.lightIndex), r.w_sum, asfloat(r.M), r.W);
}

// Update reservoir with a new candidate
// returns true if the candidate was selected
bool update_reservoir(inout Reservoir r, uint lightIndex, float weight, inout RNG rng)
{
    // Check for invalid weights
    if (!isfinite(weight) || weight < 0.0) return false;

    float new_w_sum = r.w_sum + weight;
    r.M++;
    
    bool selected = (next_float(rng) * new_w_sum < weight);
    if (selected) {
        r.lightIndex = lightIndex;
    }
    r.w_sum = new_w_sum;
    
    // Safety clamp on sum to prevent overflow
    if (r.w_sum > 1e20) r.w_sum = 1e20; 
    
    return selected;
}

// Combine two reservoirs (Spatial or Temporal resampling)
void combine_reservoirs(inout Reservoir r, const Reservoir r_other, float p_target, inout RNG rng)
{
    if (r_other.M == 0u || r_other.W <= 0.0 || !isfinite(p_target) ||
        p_target <= 0.0) {
        return;
    }
    uint M_orig = r.M;
    // We treat the incoming reservoir as a single sample with weight = p_target * W * M
    float weight = p_target * r_other.W * (float)r_other.M;
    weight = min(weight, 1e7); // Extra safety for combined weight
    update_reservoir(r, r_other.lightIndex, weight, rng);
    r.M = min(M_orig + r_other.M, 1024u);
}

// Finalize the reservoir weight
void finalize_reservoir(inout Reservoir r, float p_target)
{
    if (p_target > 0.0 && r.M > 0) {
        r.W = r.w_sum / ((float)r.M * p_target);
    } else {
        r.W = 0.0;
    }
    
    // Sanity check and hard cap to prevent over-exposure feedback loops
    if (isinf(r.W) || isnan(r.W) || r.W < 0.0) r.W = 0.0;
    r.W = min(r.W, 15.0); // Reduced to 15.0 for interior archviz stability
}

// ReSTIR GI Reservoir
struct GI_Reservoir
{
    float3 hitPos;
    float3 radiance;
    float w_sum;
    uint M;
    float W; // Final weight
};

GI_Reservoir init_gi_reservoir()
{
    GI_Reservoir r;
    r.hitPos = float3(0,0,0);
    r.radiance = float3(0,0,0);
    r.w_sum = 0.0;
    r.M = 0;
    r.W = 0.0;
    return r;
}

bool update_gi_reservoir(inout GI_Reservoir r, float3 hitPos, float3 radiance, float weight, inout RNG rng)
{
    // Clamp extreme weights to prevent fireflies from dominating local neighborhoods
    if (!isfinite(weight) || weight < 0.0) return false;
    weight = min(weight, 1e7);
    
    float new_w_sum = r.w_sum + weight;
    r.M++;
    
    bool selected = (next_float(rng) * new_w_sum < weight);
    if (selected) {
        r.hitPos = hitPos;
        r.radiance = radiance;
    }
    r.w_sum = new_w_sum;
    if (r.w_sum > 1e15) r.w_sum = 1e15;
    
    return selected;
}

void combine_gi_reservoirs(inout GI_Reservoir r, const GI_Reservoir r_other, float p_target, inout RNG rng)
{
    if (r_other.M == 0 || isnan(p_target) || isinf(p_target)) return;
    uint M_orig = r.M;
    float weight = p_target * r_other.W * (float)r_other.M;
    weight = min(weight, 1e7); // Extra safety for combined weight
    update_gi_reservoir(r, r_other.hitPos, r_other.radiance, weight, rng);
    r.M = min(M_orig + r_other.M, 60); // Reduced from 500 for better responsiveness to lighting changes
}

void finalize_gi_reservoir(inout GI_Reservoir r, float p_target)
{
    if (p_target > 0.0 && r.M > 0) {
        r.W = r.w_sum / ((float)r.M * p_target);
    } else {
        r.W = 0.0;
    }
    if (isinf(r.W) || isnan(r.W) || r.W < 0.0) r.W = 0.0;
    r.W = min(r.W, 50.0); // reduced from 100.0 for better stability in Archviz
}

#endif // RESTIR_LIB_HLSL
