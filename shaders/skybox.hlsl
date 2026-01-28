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
    float _pad3;
    float _pad4, _pad5;

    // Global Lighting
    float4 lightDir; // xyz = direction towards light
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

float4 PSMain(PSInput input) : SV_TARGET {
    float3 dir = normalize(input.viewDir);
    float2 uv = DirectionToUV(dir);
    float3 color = envMap.SampleLevel(linearSampler, uv, 0).rgb;
    
    // Use the intensity from CameraCB
    color *= intensity;
    
    // Reinhard tone mapping to match mesh
    color = color / (color + 1.0);
    // Gamma correction
    color = pow(color, 1.0/2.2);
    
    return float4(color, 1.0);
}
