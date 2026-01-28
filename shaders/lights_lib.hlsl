// shaders/lights_lib.hlsl
// Unified light evaluation for Path Tracing

#ifndef LIGHTS_LIB_HLSL
#define LIGHTS_LIB_HLSL

#define LIGHT_TYPE_DIRECTIONAL 0
#define LIGHT_TYPE_POINT       1
#define LIGHT_TYPE_SPOT        2
#define LIGHT_TYPE_AREA        3

struct Light
{
    uint type;
    float3 position;
    float3 direction;
    float intensity;
    float3 color;
    float range;
    float spotAngle; // cos(outer)
    float spotInnerAngle; // cos(inner)
    uint meshIndex; // For area lights
    uint padding;
};

// For now, we use the global directional light from the Camera CB
// In Phase 2, we will add a StructuredBuffer<Light> for local lights.

struct LightSample
{
    float3 L;         // Direction to light
    float3 radiance;  // Radiance from light (unshadowed)
    float dist;       // Distance to light (FLT_MAX for directional)
    float pdf;        // PDF of sampling this light
};

// Evaluate a directional light (like the Sun)
LightSample evaluate_directional_light(float3 lightDir, float3 lightColor, float intensity)
{
    LightSample ls;
    ls.L = normalize(lightDir);
    ls.radiance = lightColor * intensity;
    ls.dist = 1e10; // "Infinity"
    ls.pdf = 1.0;   // Dirac delta for directional light
    return ls;
}

// Evaluate Environment Map (IBL) as a light source
// (Placeholder for now, will be used in ReSTIR GI or DI)
LightSample sample_env_map(Texture2D env, SamplerState s, float3 dir)
{
    LightSample ls;
    ls.L = dir;
    ls.radiance = env.SampleLevel(s, DirectionToUV(dir), 0).rgb;
    ls.dist = 1e10;
    ls.pdf = 1.0; // Uniform for now
    return ls;
}

#endif // LIGHTS_LIB_HLSL
