// simple compute shader to cull grass patches against the view frustum
// Input buffers:
//    StructuredBuffer<FGrassBlade> g_grassBlades : register(t0);
//    cbuffer CameraCB : register(b0) { float4x4 viewProj;
//                                       float3 frustumPlanes[6];
//                                       uint instanceCount; };

// structure must come first
struct FGrassBlade {
    float3 position;
    float scale;
    float3 normal;
    float yawRadians;
    uint colorVariation;
    uint sourceMeshId;
    uint pad0;
    uint pad1;
};

cbuffer CountCB : register(b0) {
    uint instanceCount;
};

// buffer definitions: instance list, an indexed visible list (counter in [0]),
// and a small uint array for indirect args
StructuredBuffer<FGrassBlade> g_grassBlades : register(t0);
RWStructuredBuffer<uint4> g_visibleList      : register(u0);
RWStructuredBuffer<uint>   g_indirectArgs    : register(u1);

[numthreads(64,1,1)]
void CSMain(uint3 tid : SV_DispatchThreadID) {
    uint idx = tid.x;
    if (idx >= instanceCount) return;
    FGrassBlade blade = g_grassBlades[idx];

    // TODO: perform actual frustum-box intersection using blade.position and
    // predefined patch radius. here we simply accept everything.
    bool visible = true;

    if (visible) {
        uint oldCount;
        InterlockedAdd(g_visibleList[0].x, 1, oldCount); // returns previous count
        uint outIndex = oldCount;
        // store data starting at slot 1
        g_visibleList[outIndex + 1] = uint4(idx, 0, 0, 0);
        // increment indirect arguments InstanceCount (1-based second element)
        InterlockedAdd(g_indirectArgs[1], 1);
    }
}
