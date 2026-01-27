// shaders/raytracing/hit.hlsl
// Closest hit shader

#include "common.hlsli"

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr)
{
    // Get the material (assuming single mesh for now)
    int albedoTexIndex = textureIndices.x;
    
#ifdef HIT_DEBUG
    // Encode primitive index into color for debugging
    uint primIndex = PrimitiveIndex();
    float r = (float)(primIndex & 0xFF) / 255.0f;
    float g = (float)((primIndex >> 8) & 0xFF) / 255.0f;
    payload.color = float4(r, g, 0.0f, 1.0f);
#else
    if (albedoTexIndex >= 0) {
        // Interpolate UV coordinates
        float2 bary2 = attr.barycentrics;
        float3 bary = float3(bary2.x, bary2.y, 1.0 - bary2.x - bary2.y);
        uint primIndex = PrimitiveIndex();
        uint baseIndex = primIndex * 3;
        uint i0 = indices[baseIndex];
        uint i1 = indices[baseIndex + 1];
        uint i2 = indices[baseIndex + 2];
        
        float2 uv0 = vertices[i0].uv;
        float2 uv1 = vertices[i1].uv;
        float2 uv2 = vertices[i2].uv;
        
        float2 uv = uv0 * bary.x + uv1 * bary.y + uv2 * bary.z;
        
        // Interpolate normal/tangent
        float3 n0 = vertices[i0].normal; float3 n1 = vertices[i1].normal; float3 n2 = vertices[i2].normal;
        float3 N = normalize(n0 * bary.x + n1 * bary.y + n2 * bary.z);

        // Sample the albedo texture
        float3 baseCol = textures[albedoTexIndex].SampleLevel(linearSampler, uv, 0).rgb;

        // Simple lighting: directional light from material
        float3 L = normalize(lightDir.xyz);
        float NdotL = max(dot(N, L), 0.0);
        float3 diffuse = baseCol * NdotL * lightColor.rgb * lightColor.w;
        payload.color = float4(diffuse, 1.0f);
    } else {
        // Fallback to base color factor
        float3 N = float3(0,1,0);
        float3 L = normalize(lightDir.xyz);
        float NdotL = max(dot(N, L), 0.0);
        float3 diffuse = baseColorFactor.rgb * NdotL * lightColor.rgb * lightColor.w;
        payload.color = float4(diffuse, baseColorFactor.a);
    }
#endif
}