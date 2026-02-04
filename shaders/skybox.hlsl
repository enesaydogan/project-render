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
};

Texture2D envMap : register(t0, space1);
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
         // Use sun color * intensity
         color = lightColor.rgb * lightColor.w;
    } else {
         // Only apply sky intensity scaling to the map, not the sun (which has its own intensity)
         color = baseSky * intensity;
    }

    // Raymarch clouds and composite in front of sky
    // Use a large tMax to ensure we cover the full atmosphere shell
    float4 cloudOut = RaymarchClouds(pos, dir, 0.0f, 100000.0f, normalize(lightDir.xyz), lightColor.rgb * lightColor.w);
    // cloudOut.rgb = in-scattered radiance, cloudOut.a = remaining transmittance
    float3 composed = cloudOut.rgb + color * cloudOut.a;

    // ACES Tone Mapping
    composed = ToneMap(composed);
    // Gamma correction
    composed = pow(composed, 1.0/2.2);
    
    return float4(composed, 1.0);
}
