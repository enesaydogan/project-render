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

Reservoir init_reservoir()
{
    Reservoir r;
    r.lightIndex = 0;
    r.w_sum = 0.0;
    r.M = 0;
    r.W = 0.0;
    return r;
}

// Update reservoir with a new candidate
// returns true if the candidate was selected
bool update_reservoir(inout Reservoir r, uint lightIndex, float weight, inout RNG rng)
{
    r.w_sum += weight;
    r.M++;
    if (next_float(rng) < (weight / r.w_sum)) {
        r.lightIndex = lightIndex;
        return true;
    }
    return false;
}

// Combine two reservoirs (Spatial or Temporal resampling)
void combine_reservoirs(inout Reservoir r, const Reservoir r_other, float p_target, inout RNG rng)
{
    uint M_orig = r.M;
    // We treat the incoming reservoir as a single sample with weight = p_target * W * M
    float weight = p_target * r_other.W * (float)r_other.M;
    update_reservoir(r, r_other.lightIndex, weight, rng);
    r.M = M_orig + r_other.M;
}

// Finalize the reservoir weight
void finalize_reservoir(inout Reservoir r, float p_target)
{
    if (p_target > 0.0) {
        r.W = r.w_sum / ((float)r.M * p_target);
    } else {
        r.W = 0.0;
    }
}

#endif // RESTIR_LIB_HLSL
