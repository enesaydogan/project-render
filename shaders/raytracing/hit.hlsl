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
        
        // Sample the albedo texture
        payload.color = textures[albedoTexIndex].SampleLevel(linearSampler, uv, 0);
    } else {
        // Fallback to base color factor
        payload.color = baseColorFactor;
    }
#endif
}