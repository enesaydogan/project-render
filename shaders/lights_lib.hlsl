// shaders/lights_lib.hlsl
// Unified light evaluation for Path Tracing

#ifndef LIGHTS_LIB_HLSL
#define LIGHTS_LIB_HLSL

#define LIGHT_TYPE_DIRECTIONAL 0
#define LIGHT_TYPE_OMNI        1
#define LIGHT_TYPE_SPOT        2
#define LIGHT_TYPE_AREA_RECT   3
#define LIGHT_TYPE_AREA_DISK   4
#define LIGHT_TYPE_IES         5

struct Light
{
    uint type;
    float3 position;
    float3 emission;
    float3 direction;
    float radius;
    float innerConeAngle; // cos(inner)
    float outerConeAngle; // cos(outer)
    float2 areaExtents;
    int iesAtlasIndex;
};

struct LightSample
{
    float3 L;         // Direction to light
    float3 radiance;  // Radiance from light (unshadowed)
    float dist;       // Distance to light (FLT_MAX for directional)
    float pdf;        // PDF of sampling this light
};

// IES Atlas resources — uncomment when IES pipeline (Phase 2) is implemented:
// Texture2DArray<float4> g_iesAtlas : register(t100, space3);
// SamplerState g_iesSampler : register(s100, space3);

inline LightSample evaluate_directional_light(float3 lightDir, float3 lightColor, float intensity)
{
    LightSample ls;
    ls.L = normalize(lightDir);
    ls.radiance = lightColor * intensity;
    ls.dist = 1e10; // "Infinite" distance for directional light
    ls.pdf = 1.0;   // Treat as delta for NEE
    return ls;
}

LightSample evaluate_omni_light(Light light, float3 P)
{
    LightSample ls;
    float3 toLight = light.position - P;
    ls.dist = length(toLight);
    ls.L = toLight / max(1e-6, ls.dist);
    
    // Inverse square falloff with radius clamping for soft shadows/preventing singularities
    float distSq = ls.dist * ls.dist;
    float attenuation = 1.0;
    if (light.radius > 0.0) {
        // Soften the falloff near the light source
        attenuation = 1.0 / (distSq + light.radius * light.radius);
    } else {
        attenuation = 1.0 / max(1e-6, distSq);
    }
    
    ls.radiance = light.emission * attenuation;
    ls.pdf = 1.0; // For point lights, we treat as delta unless we sample area
    return ls;
}

LightSample evaluate_spot_light(Light light, float3 P)
{
    LightSample ls = evaluate_omni_light(light, P);
    
    // Spot light direction is the emission vector (pointing away from the light)
    // ls.L points from surface TO light, so -ls.L points from light TO surface
    float cosTheta = dot(-ls.L, normalize(light.direction));
    float spotScale = smoothstep(light.outerConeAngle, light.innerConeAngle, cosTheta);
    
    ls.radiance *= spotScale;
    return ls;
}

// IES light evaluation — falls back to omni until IES pipeline (Phase 2) is implemented.
// When IES textures are loaded, uncomment the atlas declarations above and restore
// the spherical-mapping + texture lookup here.
LightSample evaluate_ies_light(Light light, float3 P)
{
    // Placeholder: treat as omni light until IES atlas is available
    return evaluate_omni_light(light, P);
}

LightSample evaluate_light(Light light, float3 P)
{
    if (light.type == LIGHT_TYPE_DIRECTIONAL)
        // For directional lights, evaluate_directional expects a vector pointing TOWARDS light.
        // light.direction is the vector pointing AWAY from the light.
        return evaluate_directional_light(-light.direction, light.emission, 1.0);
    else if (light.type == LIGHT_TYPE_OMNI)
        return evaluate_omni_light(light, P);
    else if (light.type == LIGHT_TYPE_SPOT)
        return evaluate_spot_light(light, P);
    else if (light.type == LIGHT_TYPE_IES)
        return evaluate_ies_light(light, P);
    else if (light.type == LIGHT_TYPE_AREA_RECT || light.type == LIGHT_TYPE_AREA_DISK)
    {
        // For evaluate_light (no random numbers available), approximate area light
        // as a point light at the center. Use sample_area_light() when random samples are available.
        return evaluate_omni_light(light, P);
    }
    
    LightSample ls;
    ls.L = float3(0,1,0);
    ls.radiance = 0;
    ls.dist = 1e10;
    ls.pdf = 0;
    return ls;
}

LightSample sample_area_light(Light light, float3 P, float2 xi)
{
    LightSample ls;
    float3 samplePos = light.position;
    float3 lightNormal = normalize(light.direction);
    float area = 1.0;

    // Basis for area light orientation
    float3 forward = lightNormal;
    float3 right = normalize(cross(forward, abs(forward.y) > 0.9 ? float3(1, 0, 0) : float3(0, 1, 0)));
    float3 up = cross(right, forward);

    if (light.type == LIGHT_TYPE_AREA_RECT) {
        samplePos += (xi.x - 0.5) * light.areaExtents.x * right + (xi.y - 0.5) * light.areaExtents.y * up;
        area = light.areaExtents.x * light.areaExtents.y;
    } else if (light.type == LIGHT_TYPE_AREA_DISK) {
        float r = sqrt(xi.x) * light.areaExtents.x; // Uniform disk sampling
        float phi = 2.0 * PI * xi.y;
        samplePos += r * cos(phi) * right + r * sin(phi) * up;
        area = PI * light.areaExtents.x * light.areaExtents.x;
    }

    float3 toLight = samplePos - P;
    ls.dist = length(toLight);
    ls.L = toLight / max(1e-6, ls.dist);
    
    // Geometry term
    float cosLight = max(0.0, dot(lightNormal, -ls.L));
    ls.radiance = (light.emission / max(1e-6, area)) * cosLight / max(1e-6, ls.dist * ls.dist);
    ls.pdf = 1.0 / area; // PDF in area measure

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
                           inout RNG rng,
                           float sampleLod)
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
    ls.radiance = env.SampleLevel(s, uvRot, sampleLod).rgb;
    ls.pdf = max(0.0, pdf);
    return ls;
}

float evaluate_env_map_pdf(Texture2D<float4> conditionalCdf,
                           Texture2D<float4> marginalCdf,
                           float3 dir)
{
    uint envW, envH;
    conditionalCdf.GetDimensions(envW, envH);
    if (envW == 0 || envH == 0) return 0.0;

    float rotRad = radians(iblRotationDegrees);
    float3 localDir = RotateY(dir, rotRad);
    float2 uv = DirectionToUV(localDir);

    uint col = min((uint)(uv.x * envW), envW - 1);
    uint row = min((uint)(uv.y * envH), envH - 1);

    float texelPmf = max(0.0, conditionalCdf.Load(int3(col, row, 0)).y);
    float theta = ((float)row + 0.5) / (float)envH * PI;
    float sinTheta = max(1e-6, sin(theta));
    float dOmega = (2.0 * PI / (float)envW) * (PI / (float)envH) * sinTheta;

    if (sampleEnvSolidAngle > 0.5) {
        return texelPmf / max(1e-12, dOmega);
    } else {
        return texelPmf;
    }
}

#endif // LIGHTS_LIB_HLSL
