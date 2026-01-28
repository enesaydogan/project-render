// shaders/brdf_lib.hlsl
// Physically Based BRDF library (GGX / Smith / Schlick)

#ifndef BRDF_LIB_HLSL
#define BRDF_LIB_HLSL

// GGX Distribution
float D_GGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float d = (NdotH * a2 - NdotH) * NdotH + 1.0;
    return a2 / (PI * d * d);
}

// Smith Geometry Function
float G_SchlickGGX(float NdotV, float roughness) {
    float k = (roughness + 1.0);
    k = (k * k) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float G_Smith(float NdotV, float NdotL, float roughness) {
    return G_SchlickGGX(NdotV, roughness) * G_SchlickGGX(NdotL, roughness);
}

// Schlick Fresnel
float3 F_Schlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// GGX Importance Sampling
// Samples a half-vector H given roughness and random samples u
float3 SampleGGX(float2 u, float3 N, float roughness) {
    float a = roughness * roughness;
    float phi = 2.0 * PI * u.x;
    float cosTheta = sqrt(max(0.0, (1.0 - u.y) / (1.0 + (a * a - 1.0) * u.y)));
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    
    float3 H;
    H.x = sinTheta * cos(phi);
    H.y = sinTheta * sin(phi);
    H.z = cosTheta;
    
    float3 Up = abs(N.z) < 0.999 ? float3(0,0,1) : float3(1,0,0);
    float3 T = normalize(cross(Up, N));
    float3 B = cross(N, T);
    
    return normalize(T * H.x + B * H.y + N * H.z);
}

// PDF for GGX importance sampling
float PDF_GGX(float NdotH, float VdotH, float roughness) {
    return D_GGX(NdotH, roughness) * NdotH / (4.0 * VdotH);
}

// Simple Lambertian cosine sampling
float3 SampleLambert(float2 u, float3 N) {
    float phi = 2.0 * PI * u.x;
    float cosTheta = sqrt(u.y);
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    
    float3 L;
    L.x = sinTheta * cos(phi);
    L.y = sinTheta * sin(phi);
    L.z = cosTheta;
    
    float3 Up = abs(N.z) < 0.999 ? float3(0,0,1) : float3(1,0,0);
    float3 T = normalize(cross(Up, N));
    float3 B = cross(N, T);
    
    return normalize(T * L.x + B * L.y + N * L.z);
}

float PDF_Lambert(float NdotL) {
    return NdotL / PI;
}

// Fresnel for dielectrics
float FresnelDielectric(float cosTheta, float ior) {
    float r0 = (1.0 - ior) / (1.0 + ior);
    r0 = r0 * r0;
    return r0 + (1.0 - r0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// Stochastic refraction sample
// Returns true if refracted, false if reflected
bool SampleGlass(float3 V, float3 N, float ior, float2 u, out float3 L) {
    float cosTheta = dot(V, N);
    float eta = cosTheta > 0.0 ? (1.0 / ior) : ior;
    float3 outwardN = cosTheta > 0.0 ? N : -N;
    cosTheta = abs(cosTheta);

    float F = FresnelDielectric(cosTheta, ior);

    if (u.x < F) {
        // Reflection
        L = reflect(-V, outwardN);
        return false;
    } else {
        // Refraction (Snell's Law)
        float sin2ThetaI = max(0.0, 1.0 - cosTheta * cosTheta);
        float sin2ThetaT = eta * eta * sin2ThetaI;
        if (sin2ThetaT >= 1.0) {
            // Total Internal Reflection
            L = reflect(-V, outwardN);
            return false;
        } else {
            float cosThetaT = sqrt(1.0 - sin2ThetaT);
            L = normalize(eta * (-V) + (eta * cosTheta - cosThetaT) * outwardN);
            return true;
        }
    }
}

#endif // BRDF_LIB_HLSL
