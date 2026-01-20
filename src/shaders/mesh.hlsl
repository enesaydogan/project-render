cbuffer Transform : register(b0)
{
    float4 offset;
};

cbuffer MaterialCB : register(b1)
{
    float4 baseColorFactor; // rgb + alpha
    float4 params1; // x=metallic, y=roughness, z=workflow, w=anisotropy
    float4 specular; // rgb + glossiness
    float4 extras; // x=hasNormal, y=hasMetallicRoughness, z=hasOcclusion, w=hasEmissive
};

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

Texture2D baseColorTex : register(t0);
Texture2D metallicRoughnessTex : register(t1);
Texture2D normalTex : register(t2);
Texture2D occlusionTex : register(t3);
Texture2D emissiveTex : register(t4);
SamplerState linearSampler : register(s0);

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

// Microfacet BRDF helpers with energy compensation
float DistributionGGX(float3 N, float3 H, float roughness, float anisotropy)
{
    float a = roughness * roughness;
    float a2 = a * a;
    
    // Anisotropic GGX
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    
    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    denom = 3.14159265 * denom * denom;
    
    return a2 / max(denom, 0.0001);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx1 = GeometrySchlickGGX(NdotV, roughness);
    float ggx2 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    return F0 + (max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0) - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// Energy compensation for multiple scattering
float3 EnergyCompensation(float3 F, float NdotV, float roughness)
{
    float Ess = GeometrySchlickGGX(NdotV, roughness);
    float3 Favg = F + (1.0 - F) * 0.047619; // approximation
    return 1.0 + F * (1.0 / Ess - 1.0);
}

// Normal mapping in tangent space
float3 GetNormalFromMap(float2 uv, float3 worldNormal, float4 tangent, bool hasNormalMap)
{
    if (!hasNormalMap)
        return normalize(worldNormal);
    
    float3 tangentNormal = normalTex.Sample(linearSampler, uv).xyz * 2.0 - 1.0;
    
    float3 N = normalize(worldNormal);
    float3 T = normalize(tangent.xyz);
    float3 B = cross(N, T) * tangent.w;
    float3x3 TBN = float3x3(T, B, N);
    
    return normalize(mul(tangentNormal, TBN));
}

float4 PSMainMesh(PSInputMesh input) : SV_TARGET
{
    // Sample textures
    float4 baseColorSample = baseColorTex.Sample(linearSampler, input.uv);
    float3 baseColor = baseColorSample.rgb * baseColorFactor.rgb;
    float alpha = baseColorSample.a * baseColorFactor.a;
    
    // Material parameters
    float metallic = params1.x;
    float roughness = params1.y;
    int workflow = (int)params1.z;
    float anisotropy = params1.w;
    
    bool hasNormal = extras.x > 0.5;
    bool hasMetallicRoughness = extras.y > 0.5;
    bool hasOcclusion = extras.z > 0.5;
    bool hasEmissive = extras.w > 0.5;
    
    // Sample metallic-roughness texture
    if (hasMetallicRoughness)
    {
        float2 mr = metallicRoughnessTex.Sample(linearSampler, input.uv).gb;
        metallic *= mr.y;
        roughness *= mr.x;
    }
    
    roughness = max(roughness, 0.04); // Clamp roughness
    
    // Normal mapping
    float3 N = GetNormalFromMap(input.uv, input.normal, input.tangent, hasNormal);
    
    // View and light vectors (simple setup for now)
    float3 V = normalize(float3(0.0, 0.0, 1.0));
    float3 L = normalize(float3(0.5, 0.5, -1.0));
    float3 H = normalize(V + L);
    
    // Ambient occlusion
    float ao = 1.0;
    if (hasOcclusion)
    {
        ao = occlusionTex.Sample(linearSampler, input.uv).r;
    }
    
    // Emissive
    float3 emissive = float3(0.0, 0.0, 0.0);
    if (hasEmissive)
    {
        emissive = emissiveTex.Sample(linearSampler, input.uv).rgb;
    }
    
    // Metallic-roughness workflow
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);
    
    // Specular-glossiness workflow
    if (workflow == 1)
    {
        F0 = specular.rgb;
        roughness = 1.0 - specular.w; // glossiness to roughness
    }
    
    // Cook-Torrance BRDF
    float NDF = DistributionGGX(N, H, roughness, anisotropy);
    float G = GeometrySmith(N, V, L, roughness);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
    
    float3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.001) * max(dot(N, L), 0.001);
    float3 specular = numerator / max(denominator, 0.0001);
    
    // Energy compensation
    float3 compensation = EnergyCompensation(F, max(dot(N, V), 0.0), roughness);
    specular *= compensation;
    
    // Diffuse
    float3 kD = (1.0 - F) * (1.0 - metallic);
    float3 diffuse = kD * baseColor / 3.14159265;
    
    // Combine
    float NdotL = max(dot(N, L), 0.0);
    float3 Lo = (diffuse + specular) * NdotL * 2.0; // Light intensity
    
    // Add ambient
    float3 ambient = float3(0.03, 0.03, 0.03) * baseColor * ao;
    float3 color = ambient + Lo + emissive;
    
    // Tone mapping (simple Reinhard)
    color = color / (color + float3(1.0, 1.0, 1.0));
    
    // Gamma correction
    color = pow(color, float3(1.0/2.2, 1.0/2.2, 1.0/2.2));
    
    return float4(color, alpha);
}
