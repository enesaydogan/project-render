cbuffer Transform : register(b0)
{
    float4 offset;
};

cbuffer MaterialCB : register(b1)
{
    float4 baseColorFactor; // rgb + alpha
    float4 params1; // x=metallic, y=roughness, z=workflow, w=unused
    float4 specular; // rgb
    float4 extras; // glossiness in x
};

struct VSInput {
    float3 position : POSITION;
    float3 color : COLOR;
};

struct PSInput {
    float4 position : SV_POSITION;
    float3 color : COLOR;
};

PSInput VSMain(VSInput input)
{
    PSInput o;
    float3 pos = input.position + offset.xyz;
    o.position = float4(pos, 1.0);
    o.color = input.color;
    return o;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return float4(input.color, 1.0);
}

// Mesh-only shaders: vertex uses POSITION only, pixel outputs a fixed color
struct VSInputMesh {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 uv : TEXCOORD0;
};

struct PSInputMesh {
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float3 tangent : TANGENT0;
    float2 uv : TEXCOORD0;
};

Texture2D baseColorTex : register(t0);
SamplerState linearSampler : register(s0);

PSInputMesh VSMainMesh(VSInputMesh input)
{
    PSInputMesh o;
    float3 pos = input.position + offset.xyz;
    o.position = float4(pos, 1.0);
    o.normal = input.normal;
    o.tangent = input.tangent.xyz;
    o.uv = input.uv;
    return o;
}

// Microfacet BRDF helpers
float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness*roughness;
    float a2 = a*a;
    float NdotH = max(dot(N,H), 0.0);
    float NdotH2 = NdotH*NdotH;
    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (3.14159265 * denom * denom);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N,V), 0.0);
    float NdotL = max(dot(N,L), 0.0);
    float ggx1 = GeometrySchlickGGX(NdotV, roughness);
    float ggx2 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

float4 PSMainMesh(PSInputMesh input) : SV_TARGET
{
    float3 N = normalize(input.normal);
    float3 V = normalize(float3(0.0, 0.0, 1.0)); // view direction in this simple demo
    float3 L = normalize(float3(0.5, 0.5, -1.0));
    float3 H = normalize(V + L);

    float3 baseColor = baseColorTex.Sample(linearSampler, input.uv).rgb * baseColorFactor.rgb;
    float alpha = baseColorFactor.a;

    float metallic = params1.x;
    float roughness = params1.y;
    int workflow = (int)params1.z;

    float3 F0 = lerp(specular.rgb, baseColor, metallic);

    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    float3 numerator = NDF * G * F;
    float denom = 4.0 * max(dot(N,V), 0.001) * max(dot(N,L), 0.001);
    float3 specular = numerator / denom;

    float3 kD = (1.0 - F) * (1.0 - metallic);

    float NdotL = max(dot(N, L), 0.0);
    float3 Lo = (kD * baseColor / 3.14159265 + specular) * NdotL;

    return float4(Lo, alpha);
}
