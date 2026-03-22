// Compute shader builds D3D12_RAYTRACING_INSTANCE_DESC entries for grass
// patches using the same FGrassBlade buffer.  It writes into a large
// RWStructuredBuffer<D3D12_RAYTRACING_INSTANCE_DESC> starting at a
// caller-specified offset.

struct FGrassBlade {
    float3 position;
    float scale;
    float3 normal;
    float yawRadians;
    float2 emitterUv;
    int emitterDiffuseTexture;
    uint emitterPad;
    uint colorVariation;
    uint sourceMeshId;
    uint pad0;
    uint pad1;
};

struct TLASParams {
    uint startIndex;
    uint instanceCount;
    uint blasAddrLo;  // lower 32 bits of GPU VA
    uint blasAddrHi;  // upper 32 bits of GPU VA
    uint patchMeshIndex;
};

StructuredBuffer<FGrassBlade> g_grassBlades : register(t0);
RWStructuredBuffer<uint> g_tlasDescs : register(u0); // raw access, 4 uints per desc
cbuffer Params : register(b0) { TLASParams params; };

// helper to write a 3x4 row-major matrix into the uint buffer
void StoreTransform(uint baseIdx, float4x4 m) {
    // each row has 4 floats (packed into uints)
    g_tlasDescs[baseIdx + 0] = asuint(m[0][0]);
    g_tlasDescs[baseIdx + 1] = asuint(m[0][1]);
    g_tlasDescs[baseIdx + 2] = asuint(m[0][2]);
    g_tlasDescs[baseIdx + 3] = asuint(m[0][3]);
    g_tlasDescs[baseIdx + 4] = asuint(m[1][0]);
    g_tlasDescs[baseIdx + 5] = asuint(m[1][1]);
    g_tlasDescs[baseIdx + 6] = asuint(m[1][2]);
    g_tlasDescs[baseIdx + 7] = asuint(m[1][3]);
    g_tlasDescs[baseIdx + 8] = asuint(m[2][0]);
    g_tlasDescs[baseIdx + 9] = asuint(m[2][1]);
    g_tlasDescs[baseIdx +10] = asuint(m[2][2]);
    g_tlasDescs[baseIdx +11] = asuint(m[2][3]);
}

float3 SafeNormalize(float3 v, float3 fallback) {
    float lenSq = dot(v, v);
    return (lenSq > 1e-8) ? normalize(v) : fallback;
}

[numthreads(64,1,1)]
void CSMain(uint3 tid : SV_DispatchThreadID) {
    uint idx = tid.x;
    if (idx >= params.instanceCount) return;
    FGrassBlade blade = g_grassBlades[idx];

    // Build a stable basis aligned to the emitter normal, then apply yaw around
    // that local up axis so raster/DXR use the same orientation rules.
    float s;
    float c;
    sincos(blade.yawRadians, s, c);
    float scale = max(blade.scale, 1e-3);
    float3 up = SafeNormalize(blade.normal, float3(0.0, 1.0, 0.0));
    float3 helper = (abs(up.y) > 0.9) ? float3(1.0, 0.0, 0.0) : float3(0.0, 1.0, 0.0);
    float3 right = SafeNormalize(cross(helper, up), float3(1.0, 0.0, 0.0));
    float3 forward = SafeNormalize(cross(up, right), float3(0.0, 0.0, 1.0));
    float3 yawRight = right * c - forward * s;
    float3 yawForward = right * s + forward * c;

    float4x4 xform = float4x4(
         yawRight.x * scale, up.x * scale, yawForward.x * scale, blade.position.x,
         yawRight.y * scale, up.y * scale, yawForward.y * scale, blade.position.y,
         yawRight.z * scale, up.z * scale, yawForward.z * scale, blade.position.z,
         0, 0, 0, 1);

    // each instance descriptor is 64 bytes = 16 uints
    uint descBase = (params.startIndex + idx) * 16;
    StoreTransform(descBase, xform);

    // D3D12_RAYTRACING_INSTANCE_DESC layout after the 3x4 transform:
    //   uint[12] = InstanceID (bits 0-23) | InstanceMask (bits 24-31)
    //   uint[13] = InstanceContributionToHitGroupIndex (bits 0-23) | Flags (bits 24-31)
    //   uint[14..15] = AccelerationStructure GPU VA (64-bit, low then high)
    g_tlasDescs[descBase + 12] = (params.patchMeshIndex & 0x00FFFFFFu) | (0xFFu << 24);
    g_tlasDescs[descBase + 13] = (0u) | (0x01u << 24); // flags = FORCE_OPAQUE

    g_tlasDescs[descBase + 14] = params.blasAddrLo;
    g_tlasDescs[descBase + 15] = params.blasAddrHi;
}
