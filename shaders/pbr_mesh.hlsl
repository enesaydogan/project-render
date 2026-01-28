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

cbuffer WorldCB : register(b2)
{
    float4x4 world;
};

cbuffer MaterialCB : register(b1)
{
    float4 diffuseColor;        // rgb, a=opacity
    float4 reflectionColor;     // rgb, w=reflectionGlossiness
    float4 refractionColor;     // rgb, w=refractionGlossiness
    float4 emissiveColor;       // rgb, w=ior
    int4 textureIndices;        // x=diffuse, y=reflect, z=normal, w=refract
    int4 emissiveAndPad;        // x=emissive, y=occlusion, z=metalRough
};

// Texture array - bonded as an unbounded array in SM 6.x
Texture2D textures[] : register(t0);
Texture2D envMap : register(t0, space1);
SamplerState linearSampler : register(s0);

float2 DirectionToUV(float3 dir) {
    float2 uv;
    uv.x = atan2(dir.x, dir.z) / (2.0 * 3.14159265) + 0.5;
    uv.y = acos(clamp(dir.y, -1.0, 1.0)) / 3.14159265;
    return uv;
}

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

    // World-space position
    float4 worldPos = mul(world, float4(input.position, 1.0f));
    float3 outWorldPos = worldPos.xyz;

    // Use a standard right-handed view basis for the camera
    float3 R = normalize(cross(forward, up)); // Right (F x U in RH with F pointing away)
    float3 U = normalize(cross(R, forward));  // Up (orthonormal)
    
    float3 rel = outWorldPos - pos;
    float3 viewPos;
    viewPos.x = dot(rel, R);
    viewPos.y = dot(rel, U);
    viewPos.z = dot(rel, forward);
    
    // Build projection matrix
    // Convert FOV to radians and compute focal term
    float f = 1.0f / tan(radians(fov) * 0.5f);

    // Apply projection (D3D clip space: z in [0,1])
    // Standard perspective: W=Z_view, X_ndc = X_view * f / aspect, Y_ndc = Y_view * f
    float A = farZ / (farZ - nearZ);
    float B = -nearZ * farZ / (farZ - nearZ);
    o.position = float4(
        viewPos.x * f / aspect,
        viewPos.y * f,
        viewPos.z * A + B,
        viewPos.z
    );

    o.worldPos = outWorldPos;
    o.normal = mul((float3x3)world, input.normal);
    o.tangent = float4(mul((float3x3)world, input.tangent.xyz), input.tangent.w);
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
    if (normalTexIndex < 0 || length(worldTangent.xyz) < 0.001) return normalize(worldNormal);
    
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

    // --- Texture Lookups ---
    float3 BaseColor = diffuseColor.rgb;
    float alpha = diffuseColor.a;
    if (textureIndices.x >= 0) {
        float4 diffSample = textures[textureIndices.x].Sample(linearSampler, input.uv);
        BaseColor *= diffSample.rgb;
        alpha *= diffSample.a;
    }

    float metalness = 0.0;
    float roughness = saturate(1.0 - reflectionColor.w);
    
    // Metal/Roughness Logic: if MR texture exists, it overrides specific channels
    // G = Roughness, B = Metalness
    if (emissiveAndPad.z >= 0) {
        float4 mrSample = textures[emissiveAndPad.z].Sample(linearSampler, input.uv);
        roughness = mrSample.g; // Roughness is Green
        metalness = mrSample.b; // Metalness is Blue
    }
    
    // Standard PBR Model
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), BaseColor, metalness);
    float3 DiffuseAlbedo = BaseColor * (1.0 - metalness);
    
    // Normal
    float3 N = GetNormalFromMap(input.uv, input.normal, input.tangent, textureIndices.z);

    // Emissive
    float3 emiss = (emissiveAndPad.x >= 0) ? textures[emissiveAndPad.x].Sample(linearSampler, input.uv).rgb : float3(1,1,1);
    emiss *= emissiveColor.rgb; 

    // Occlusion
    float ao = (emissiveAndPad.y >= 0) ? textures[emissiveAndPad.y].Sample(linearSampler, input.uv).r : 1.0;

    // Lighting
    float3 V = normalize(pos - input.worldPos);
    float3 L = normalize(lightDir.xyz);
    if (length(lightDir.xyz) < 0.001) L = float3(0, 1, 0); 
    float3 H = normalize(V + L);

    roughness = max(roughness, 0.002);

    // Cook-Torrance BRDF
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
    
    float3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    float3 spec = numerator / denominator;
    
    // Energy Preservation
    float3 kS = F;
    float3 kD = 1.0 - kS;
    
    float3 diffuseTerm = kD * DiffuseAlbedo / PI;
    
    float NdotL = saturate(dot(N, L));
    float3 directLight = (diffuseTerm + spec) * lightColor.rgb * lightColor.w * NdotL;
    
    // IBL (Image Based Lighting)
    float3 R = reflect(-V, N);
    float3 F_ibl = FresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    float3 kS_ibl = F_ibl;
    float3 kD_ibl = 1.0 - kS_ibl;

    float2 envUV_diff = DirectionToUV(N);
    // Use mips now that they are available! Level 6-8 is good for diffuse
    float3 irradiance = envMap.SampleLevel(linearSampler, envUV_diff, 7.0).rgb; 
    float3 diffuse_ibl = kD_ibl * irradiance * DiffuseAlbedo;

    float2 envUV_spec = DirectionToUV(R);
    // Prefiltered reflection using mips based on roughness
    float3 prefilteredColor = envMap.SampleLevel(linearSampler, envUV_spec, roughness * 7.0).rgb;
    float3 specular_ibl = kS_ibl * prefilteredColor;

    // Use a consistent 5.0x boost for IBL to match raytracing and provide good environment lighting
    float3 ambient = (diffuse_ibl + specular_ibl) * ao * ambientColor.rgb * 5.0;
    
    float3 color = directLight + ambient + emiss;
    
    // Exposure & Tone Map
    color *= intensity;
    
    // DEBUG PASS
    int mode = (int)debugMode;
    // Mode 1: Albedo
    if (mode == 1) return float4(BaseColor, 1.0);
    if (mode == 2) return float4(N * 0.5 + 0.5, 1.0); // Normal
    if (mode == 3) return float4(emiss, 1.0); // Emissive
    if (mode == 4) return float4(1.0 - roughness, 1.0 - roughness, 1.0 - roughness, 1.0); // Glossiness
    if (mode == 5) return float4(F0, 1.0); // F0
    if (mode == 6) return float4(metalness, metalness, metalness, 1.0); // Metalness
    if (mode == 7) return float4(ao, ao, ao, 1.0); // AO

    color = color / (color + 1.0);
    color = pow(color, 1.0/2.2);

    return float4(color, alpha);
}
