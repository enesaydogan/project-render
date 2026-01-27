cbuffer CameraCB : register(b0)
{
    float3 pos;
    float _pad0;
    float3 forward;
    float _pad1;
    float3 up;
    float _pad2;
    float params[5]; // fov(deg), aspect, znear, zfar, intensity
};

cbuffer MaterialCB : register(b1)
{
    float4 baseColorFactor;
    float4 params1; // x=metallic, y=roughness, z=workflow, w=unused
    float4 specular; // rgb specular for spec-gloss workflow
    float4 emissiveFactor; // rgb emissive
    int4 textureIndices; // x=baseColor, y=metallicRoughness, z=normal, w=occlusion
    int4 emissiveAndPad; // x=emissiveTexIndex, yzw=padding
    float4 lightDir; // xyz = light direction (pointing towards light)
    float4 lightColor; // rgb + intensity in .w
};

// Texture array - we'll bind multiple textures in a descriptor table
Texture2D textures[16] : register(t0);
SamplerState linearSampler : register(s0);

struct VSInputMesh {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 uv : TEXCOORD0;
};

struct PSInputMesh {
    float4 position : SV_POSITION;
    float3 worldPos : POSITION0;
    float3 normal : NORMAL;
    float4 tangent : TANGENT0;
    float2 uv : TEXCOORD0;
};

PSInputMesh VSMainMesh(VSInputMesh input)
{
    PSInputMesh o;

    // World-space position (scale to match instances used for TLAS)
    float3 worldPos = input.position * 0.1f; // Scale down to match TLAS instance scale

    // Create proper view-projection matrix for raster rendering
    float aspect = params[1];
    float fov = params[0];
    float nearZ = params[2];
    float farZ = params[3];
    
    // Build camera basis and transform to view space (match simple.hlsl)
    float3 R = normalize(cross(forward, up));
    float3 U = normalize(cross(R, forward));
    float3 rel = worldPos - pos;
    float3 viewPos;
    viewPos.x = dot(rel, R);
    viewPos.y = dot(rel, U);
    viewPos.z = dot(rel, forward);
    
    // Build projection matrix
    // Convert FOV to radians and compute focal term
    float f = 1.0f / tan(radians(fov) * 0.5f);

    // Apply projection (D3D clip space: z in [0,1])
    float A = farZ / (farZ - nearZ);
    float B = -nearZ * farZ / (farZ - nearZ);
    o.position = float4(
        viewPos.x * f / aspect,
        -viewPos.y * f,
        viewPos.z * A + B,
        viewPos.z
    );

    o.worldPos = worldPos;
    o.normal = input.normal;
    o.tangent = input.tangent;
    o.uv = input.uv;
    return o;
}

// Improved microfacet BRDF helpers
static const float PI = 3.14159265359;

// GGX/Trowbridge-Reitz normal distribution with anisotropy support
float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    
    return a2 / max(denom, 0.0001);
}

// Smith's shadowing-masking function with height-correlated masking
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    
    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    
    return nom / max(denom, 0.0001);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    
    return ggx1 * ggx2;
}

// Fresnel-Schlick approximation with roughness for energy compensation
float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    return F0 + (max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0) - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// Extract normal from normal map and transform to world space
float3 GetNormalFromMap(float2 uv, float3 worldNormal, float4 worldTangent, int normalTexIndex)
{
    if (normalTexIndex < 0) return normalize(worldNormal);
    
    float3 tangentNormal = textures[normalTexIndex].Sample(linearSampler, uv).xyz * 2.0 - 1.0;
    
    float3 N = normalize(worldNormal);
    float3 T = normalize(worldTangent.xyz);
    float3 B = cross(N, T) * worldTangent.w;
    float3x3 TBN = float3x3(T, B, N);
    
    return normalize(mul(tangentNormal, TBN));
}

float4 PSMainMesh(PSInputMesh input) : SV_TARGET
{
#ifdef RASTER_DEBUG_DEPTH
    // Output clip-space depth as grayscale for debugging
    float clipW = input.position.w;
    float depth = 0.0f;
    if (abs(clipW) > 1e-6) {
        depth = saturate(input.position.z / clipW);
    }
    return float4(depth, depth, depth, 1.0);
#endif
#ifdef RASTER_DEBUG_UV
    // Debug mode: output UVs in RGB for quick comparison with RayGen UV output
    return float4(input.uv.xy, 0.0, 1.0);
#endif

    // Sample textures
    int baseColorIdx = textureIndices.x;
    int metallicRoughnessIdx = textureIndices.y;
    int normalIdx = textureIndices.z;
    int occlusionIdx = textureIndices.w;
    
    // Debug: return different colors based on material index
    if (baseColorIdx < 0) {
        // No texture - return bright red to debug plane
        return float4(1.0, 0.0, 0.0, 1.0);
    } else {
        // Has texture - return bright green to debug helmet
        return float4(0.0, 1.0, 0.0, 1.0);
    }
}
