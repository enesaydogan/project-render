// shaders/brdf_lib.hlsl
// Physically Based BRDF library (GGX / Smith / Schlick)

#ifndef BRDF_LIB_HLSL
#define BRDF_LIB_HLSL

// GGX Distribution
float D_GGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float d = (NdotH * a2 - NdotH) * NdotH + 1.0;
    return a2 / (PI * d * d + 1e-7); // Safety epsilon
}

// Smith Geometry Function (Height-Correlated)
float V_SmithCorrelated(float NdotV, float NdotL, float roughness) {
    float a2 = roughness * roughness;
    float GGXV = NdotL * sqrt(max(0.0, a2 + (1.0 - a2) * (NdotV * NdotV)));
    float GGXL = NdotV * sqrt(max(0.0, a2 + (1.0 - a2) * (NdotL * NdotL)));
    return 0.5 / (GGXV + GGXL + 1e-5);
}

float G_Smith(float NdotV, float NdotL, float roughness) {
    // Legacy separable Smith for reference or fallback
    float k = (roughness * roughness) / 2.0; // Archviz optimization
    float g1 = NdotV / (NdotV * (1.0 - k) + k + 1e-5);
    float g2 = NdotL / (NdotL * (1.0 - k) + k + 1e-5);
    return g1 * g2;
}

// Schlick Fresnel
float3 F_Schlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow5(saturate(1.0 - cosTheta));
}

// Fresnel-Schlick with roughness compensation (for IBL and rough surfaces)
float3 F_SchlickRoughness(float cosTheta, float3 F0, float roughness) {
    return F0 + (max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0) - F0) * pow5(saturate(1.0 - cosTheta));
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
    return r0 + (1.0 - r0) * pow5(saturate(1.0 - cosTheta));
}

// DLSS-RR Specular Albedo approximation
// Ref: ProgrammingGuideDLSS_RR.md
float3 EnvBRDFApprox2(float3 SpecularColor, float alpha, float NoV) 
{ 
  NoV = abs(NoV); 
  // [Ray Tracing Gems, Chapter 32]
  float4 X; 
  X.x = 1.f; 
  X.y = NoV; 
  X.z = NoV * NoV; 
  X.w = NoV * X.z; 
  float4 Y; 
  Y.x = 1.f; 
  Y.y = alpha; 
  Y.z = alpha * alpha; 
  Y.w = alpha * Y.z; 
  float2x2 M1 = float2x2(0.99044f, -1.28514f, 1.29678f, -0.755907f); 
  float3x3 M2 = float3x3(1.f, 2.92338f, 59.4188f, 20.3225f, -27.0302f, 222.592f, 121.563f, 626.13f, 316.627f); 
  float2x2 M3 = float2x2(0.0365463f, 3.32707, 9.0632f, -9.04756); 
  float3x3 M4 = float3x3(1.f, 3.59685f, -1.36772f, 9.04401f, -16.3174f, 9.22949f, 5.56589f, 19.7886f, -20.2123f); 
  float bias = dot(mul(M1, X.xy), Y.xy) * rcp(dot(mul(M2, X.xyw), Y.xyw)); 
  float scale = dot(mul(M3, X.xy), Y.xy) * rcp(dot(mul(M4, X.xzw), Y.xyw)); 
  // This is a hack for specular reflectance of 0
  bias *= saturate(SpecularColor.g * 50); 
  return SpecularColor * max(0, scale) + max(0, bias); 
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
