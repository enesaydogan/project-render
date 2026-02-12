// shaders/random_lib.hlsl
// Simple PCG random number generator for path tracing

#ifndef RANDOM_LIB_H
#define RANDOM_LIB_H

struct RNG {
    uint state;
};

// PCG random number generator
// http://www.pcg-random.org/
uint pcg_hash(uint input) {
    uint state = input * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

RNG init_rng(uint2 pixel, uint frame) {
    RNG rng;
    // Stronger spatial/temporal seed mixing to avoid visible structured blocks.
    uint seed = pixel.x * 1973u;
    seed ^= pixel.y * 9277u;
    seed ^= frame * 26699u;
    seed ^= 0x68bc21ebu;
    seed ^= (seed >> 16);
    seed *= 2246822519u;
    seed ^= (seed >> 13);
    seed *= 3266489917u;
    seed ^= (seed >> 16);
    rng.state = pcg_hash(seed);
    return rng;
}

float next_float(inout RNG rng) {
    rng.state = pcg_hash(rng.state);
    return float(rng.state) / 4294967296.0;
}

uint next_uint(inout RNG rng) {
    rng.state = pcg_hash(rng.state);
    return rng.state;
}

float2 next_float2(inout RNG rng) {
    return float2(next_float(rng), next_float(rng));
}

float3 next_float3(inout RNG rng) {
    return float3(next_float(rng), next_float(rng), next_float(rng));
}

// Consine weighted hemisphere sampling
float3 sample_hemisphere_cosine(float2 u) {
    float phi = 2.0 * 3.14159265 * u.x;
    float cos_theta = sqrt(u.y);
    float sin_theta = sqrt(1.0 - u.y);
    return float3(sin_theta * cos(phi), cos_theta, sin_theta * sin(phi));
}

float3 align_to_normal(float3 sample_dir, float3 normal) {
    float3 up = abs(normal.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 tangent = normalize(cross(up, normal));
    float3 bitangent = cross(normal, tangent);
    return sample_dir.x * tangent + sample_dir.y * normal + sample_dir.z * bitangent;
}

#endif // RANDOM_LIB_H
