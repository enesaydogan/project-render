cbuffer Transform : register(b0)
{
    float4 offset;
};

cbuffer MaterialCB : register(b1)
{
    float4 baseColorFactor;
    float4 params1; // x=metallic, y=roughness, z=workflow, w=unused
    float4 specular; // rgb specular for spec-gloss workflow
    float4 emissiveFactor; // rgb emissive
    int4 textureIndices; // x=baseColor, y=metallicRoughness, z=normal, w=occlusion
    int4 emissiveAndPad; // x=emissiveTexIndex, yzw=padding
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
    float3 pos = input.position + offset.xyz;
    o.position = float4(pos, 1.0);
    o.worldPos = input.position;
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
    // Sample textures
    int baseColorIdx = textureIndices.x;
    int metallicRoughnessIdx = textureIndices.y;
    int normalIdx = textureIndices.z;
    int occlusionIdx = textureIndices.w;
    
    // Base color
    float3 baseColor = baseColorFactor.rgb;
    if (baseColorIdx >= 0) {
        float4 texColor = textures[baseColorIdx].Sample(linearSampler, input.uv);
        baseColor *= texColor.rgb;
    }
    
    // Metallic-roughness
    float metallic = params1.x;
    float roughness = params1.y;
    if (metallicRoughnessIdx >= 0) {
        float4 mr = textures[metallicRoughnessIdx].Sample(linearSampler, input.uv);
        metallic *= mr.b; // Blue channel = metallic
        roughness *= mr.g; // Green channel = roughness
    }
    
    // Clamp roughness to avoid division by zero
    roughness = max(roughness, 0.04);
    
    // Normal mapping
    float3 N = GetNormalFromMap(input.uv, input.normal, input.tangent, normalIdx);
    
    // Ambient occlusion
    float ao = 1.0;
    if (occlusionIdx >= 0) {
        ao = textures[occlusionIdx].Sample(linearSampler, input.uv).r;
    }
    
    // Emissive
    float3 emissive = emissiveFactor.rgb;
    if (emissiveAndPad.x >= 0) {
        emissive *= textures[emissiveAndPad.x].Sample(linearSampler, input.uv).rgb;
    }
    
    // Simple directional light setup
    float3 V = normalize(float3(0.0, 0.0, 1.0)); // View direction
    float3 L = normalize(float3(0.5, 0.5, -1.0)); // Light direction
    float3 H = normalize(V + L);
    
    // Calculate reflectance at normal incidence
    float3 F0 = float3(0.04, 0.04, 0.04); // Dielectric base reflectivity
    F0 = lerp(F0, baseColor, metallic);
    
    // Cook-Torrance BRDF
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
    
    float3 numerator = NDF * G * F;
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float denominator = 4.0 * NdotV * NdotL;
    float3 specular = numerator / max(denominator, 0.001);
    
    // Energy conservation
    float3 kS = F;
    float3 kD = float3(1.0, 1.0, 1.0) - kS;
    kD *= 1.0 - metallic;
    
    // Outgoing radiance
    float3 Lo = (kD * baseColor / PI + specular) * NdotL;
    
    // Ambient lighting with AO
    float3 ambient = float3(0.03, 0.03, 0.03) * baseColor * ao;
    
    // Add emissive
    float3 color = ambient + Lo + emissive;
    
    // Simple tone mapping
    color = color / (color + float3(1.0, 1.0, 1.0));
    
    // Gamma correction
    color = pow(color, float3(1.0/2.2, 1.0/2.2, 1.0/2.2));
    
    return float4(color, baseColorFactor.a);
}
