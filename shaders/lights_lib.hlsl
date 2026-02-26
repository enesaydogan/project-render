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
float3 RotateY(float3 v, float angle)
{
    float c = cos(angle);
    float s = sin(angle);
    return float3(c * v.x + s * v.z, v.y, -s * v.x + c * v.z);
}

uint SampleCdf1D(Texture2D<float4> cdfTex, uint length, uint row, float xi)
{
    uint lo = 0;
    uint hi = max(0u, length - 1u);

    // Supports up to 65536 elements; enough for practical env map dimensions.
    [unroll]
    for (uint i = 0; i < 16; ++i)
    {
        if (lo >= hi) break;
        uint mid = (lo + hi) >> 1;
        float c = cdfTex.Load(int3(mid, row, 0)).x;
        if (xi <= c) hi = mid;
        else         lo = mid + 1;
    }
    return min(lo, max(0u, length - 1u));
}

// Importance-sample env map using a precomputed 2D CDF:
// - envMarginalCdf: CDF over rows (v / theta)
// - envConditionalCdf: per-row CDF over columns (u / phi)
// Returns PDF in solid-angle measure (sr^-1).
LightSample sample_env_map(Texture2D env,
                           Texture2D<float4> conditionalCdf,
                           Texture2D<float4> marginalCdf,
                           SamplerState s,
                           inout RNG rng)
{
    LightSample ls;
    ls.L = float3(0.0, 1.0, 0.0);
    ls.radiance = float3(0.0, 0.0, 0.0);
    ls.dist = 1e10;
    ls.pdf = 0.0;

    uint envW, envH;
    env.GetDimensions(envW, envH);
    if (envW == 0 || envH == 0) {
        return ls;
    }

    uint margW, margH;
    marginalCdf.GetDimensions(margW, margH);
    if (margW == 0 || margH == 0) {
        return ls;
    }

    float xiRow = next_float(rng);
    float xiCol = next_float(rng);
    float xiJitX = next_float(rng);
    float xiJitY = next_float(rng);

    uint row = SampleCdf1D(marginalCdf, margW, 0, xiRow);
    uint col = SampleCdf1D(conditionalCdf, envW, row, xiCol);

    float u = ((float)col + xiJitX) / (float)envW;
    float v = ((float)row + xiJitY) / (float)envH;

    float3 localDir = UVToDirection(float2(u, v));

    // CDFs are built from the unrotated env map; rotate sampled direction
    // into world-space so DirectionToUVRotated(worldDir) maps back to sampled texel.
    float rotRad = radians(iblRotationDegrees);
    float3 worldDir = normalize(RotateY(localDir, -rotRad));
    float2 uvRot = DirectionToUVRotated(worldDir);

    float texelPmf = max(0.0, conditionalCdf.Load(int3(col, row, 0)).y);
    float theta = ((float)row + 0.5) / (float)envH * PI;
    float sinTheta = max(1e-6, sin(theta));
    float dOmega = (2.0 * PI / (float)envW) * (PI / (float)envH) * sinTheta;

    // When building with solid-angle weighting we also report pdf in
    // steradians; otherwise we can return a per-texel pdf which is helpful
    // for comparing the wrong behaviour.  The camera constant buffer field
    // sampleEnvSolidAngle is set via the UI (see editor_ui.cpp).
    float pdf;
    if (sampleEnvSolidAngle > 0.5) {
        pdf = texelPmf / max(1e-12, dOmega);
    } else {
        // texel-based pdf (unitless, 1/texel).  MIS in the path tracer will
        // still use this value; results will be incorrect but the toggle
        // allows experimentation.
        pdf = texelPmf;
    }

    ls.L = worldDir;
    ls.radiance = env.SampleLevel(s, uvRot, 0).rgb;
    ls.pdf = max(0.0, pdf);
    return ls;
}

#endif // LIGHTS_LIB_HLSL
