cbuffer CameraCB : register(b0)
{
    float3 pos;
    float debugMode;
    float3 forward;
    float _pad1;
    float3 up;
    float _pad2;
    float fov;
    float aspect;
    float nearZ;
    float farZ;
    float intensity;
    float globalFrameCount; 
    float lightCount;
    float maxSpecularBounces;
    float maxRefractiveBounces;
    float maxGIBounces;
    float maxSPP;
    float accumulationCount; 

    // Global Lighting
    float4 lightDir; // xyz = direction towards light, w = sun radius (rad)
    float4 lightColor; // rgb + intensity in .w
    float4 ambientColor; // rgb + weight in .w

    // Keep layout aligned with src/camera.h so cloudRenderingEnabled reads the
    // correct value in raster mode too.
    float3 prevPos;
    float prevValid;
    float3 prevForward;
    float dlssEnabled;
    float3 prevUp;
    float dlssRayReconstruction;
    float prevFov;
    float prevAspect;
    float prevNearZ;
    float prevFarZ;
    float noiseThreshold;
    float useAdaptiveSampling;
    float debugVisualizationMode;
    float cloudRenderingEnabled;
};

Texture2D envMap : register(t0, space1);
Texture2D bakedClouds : register(t12, space2); // baked lat-long clouds (rgb + transmittance)
SamplerState linearSampler : register(s0);

struct VSInput {
    uint vertexId : SV_VertexID;
};

struct PSInput {
    float4 position : SV_POSITION;
    float3 viewDir : TEXCOORD0;
};

float2 DirectionToUV(float3 dir) {
    float2 uv;
    uv.x = atan2(dir.x, dir.z) / (2.0 * 3.14159265) + 0.5;
    uv.y = acos(dir.y) / 3.14159265;
    return uv;
}

float3 ToneMap(float3 x) {
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

PSInput VSMain(VSInput input) {
    PSInput output;
    
    // Create a full-screen triangle
    float2 uv = float2((input.vertexId << 1) & 2, input.vertexId & 2);
    // z = 0.99999f to ensure it passes LESS_EQUAL even at the far plane limit
    output.position = float4(uv * 2.0 - 1.0, 0.99999f, 1.0); 
    
    // Calculate view direction for this pixel in world space
    float3 right = normalize(cross(forward, up));
    float3 actualUp = normalize(cross(right, forward)); // Re-calculate up to ensure orthogonality
    
    float tanHalfFov = tan(radians(fov) * 0.5);
    
    float2 screenPos = uv * 2.0 - 1.0;
    
    output.viewDir = forward + (screenPos.x * tanHalfFov * aspect) * right + (screenPos.y * tanHalfFov) * actualUp;
    
    return output;
}

#include "clouds.hlsl"

float4 PSMain(PSInput input) : SV_TARGET {
    float3 dir = normalize(input.viewDir);
    float2 uv = DirectionToUV(dir);
    float3 baseSky = envMap.SampleLevel(linearSampler, uv, 0).rgb;
    
    // Add Analytic Sun Disc
    // lightDir.w holds the sun *radius* in radians (set in main.cpp)
    float3 L = normalize(lightDir.xyz);
    float cosTheta = dot(dir, L);
    float cosSunRadius = cos(lightDir.w);
    
    float3 color;
    if (cosTheta > cosSunRadius) {
         // Physically correct sun radiance = Illuminance (Lux) / Solid Angle (sr)
         // Omega = 2 * PI * (1 - cos(theta))
         const float PI = 3.14159265f;
         float sunSolidAngle = 2.0f * PI * (1.0f - cosSunRadius);
         float3 sunRadiance = (lightColor.rgb * lightColor.w) / max(sunSolidAngle, 1e-7f);
         color = sunRadiance * intensity;
    } else {
         // Apply sky intensity scaling and camera exposure
         color = baseSky * intensity;
    }

    // Use baked lat-long clouds when available (much cheaper than raymarching each pixel)
    float3 composed = color;
    if (cloudRenderingEnabled > 0.5f) {
        float4 baked = bakedClouds.SampleLevel(linearSampler, uv, 0);
        baked.a = saturate(baked.a);
        baked.rgb = max(baked.rgb, 0.0);
        // If debug view selected, show baked cloud color directly
        composed = baked.rgb + color * baked.a;
    }

    // ACES Tone Mapping
    composed = ToneMap(composed);
    // Gamma correction
    composed = pow(composed, 1.0/2.2);
    
    return float4(composed, 1.0);
}
