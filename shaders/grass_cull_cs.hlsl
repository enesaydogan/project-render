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
    float2 emitterUv;
    int emitterDiffuseTexture;
    uint emitterPad;
    uint colorVariation;
    uint sourceMeshId;
    uint pad0;
    uint pad1;
};

cbuffer CountCB : register(b0) {
    uint instanceCount;
};

cbuffer CameraCB : register(b1)
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
    float globalFrameCount;
    float lightCount;
    float maxSpecularBounces;
    float maxRefractiveBounces;
    float maxGIBounces;
    float maxSPP;
    float accumulationCount;
    float4 lightDir;
    float4 lightColor;
    float3 prevPos;
    float prevValid;
    float3 prevForward;
    float dlssEnabled;
    float3 prevUp;
    float dlssRayReconstruction;
    float prevFov;
    float prevAspect;
    float prevNearZ;
    float prevFarZ;
    float noiseThreshold;
    float useAdaptiveSampling;
    float debugVisualizationMode;
    float cloudRenderingEnabled;
    float iblRotationDegrees;
    float sampleEnvSolidAngle;
    float nrdEnabled;
    float exportRendering;
    float4x4 shadowMatrix;
    float4x4 viewProj;
    float4x4 invViewProj;
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

    float radius = max(0.08, blade.scale * 0.75);
    float4 clip = mul(float4(blade.position, 1.0), viewProj);
    bool visible = (clip.w > 1e-4);
    if (visible) {
        float margin = clip.w * 0.15 + radius;
        visible = (clip.x >= -clip.w - margin) && (clip.x <= clip.w + margin) &&
                  (clip.y >= -clip.w - margin) && (clip.y <= clip.w + margin) &&
                  (clip.z >= -margin) && (clip.z <= clip.w + margin);
    }

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
