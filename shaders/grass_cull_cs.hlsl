// simple compute shader to cull grass patches against the view frustum
// Input buffers:
//    StructuredBuffer<FGrassPatch> g_grassPatches : register(t0);
//    cbuffer CameraCB : register(b0) { float4x4 viewProj;
//                                       float3 frustumPlanes[6];
//                                       uint instanceCount; };

// structure must come first
struct FGrassPatch {
    float3 position;
    float scale;
    float3 normal;
    float yawRadians;
    float2 emitterUv;
    uint colorVariation;
    uint packedData;
};

cbuffer CountCB : register(b0) {
    uint instanceCount;
    float nearDistanceSq;
    float midDistanceSq;
    float _padCount;
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
    float dxrProceduralSkyBoost;
    float4x4 shadowMatrix;
    float4x4 viewProj;
    float4x4 invViewProj;
};

// buffer definitions: instance list, an indexed visible list (counter in [0]),
// and a small uint array for indirect args
StructuredBuffer<FGrassPatch> g_grassPatches : register(t0);
RWStructuredBuffer<uint4> g_visibleNear      : register(u0);
RWStructuredBuffer<uint>  g_indirectNear     : register(u1);
RWStructuredBuffer<uint4> g_visibleMid       : register(u2);
RWStructuredBuffer<uint>  g_indirectMid      : register(u3);

[numthreads(64,1,1)]
void CSMain(uint3 tid : SV_DispatchThreadID) {
    uint idx = tid.x;
    if (idx >= instanceCount) return;
    FGrassPatch patch = g_grassPatches[idx];

    float radius = max(0.10, patch.scale * 0.90);
    float4 clip = mul(float4(patch.position, 1.0), viewProj);
    bool visible = (clip.w > 1e-4);
    if (visible) {
        float margin = clip.w * 0.15 + radius;
        visible = (clip.x >= -clip.w - margin) && (clip.x <= clip.w + margin) &&
                  (clip.y >= -clip.w - margin) && (clip.y <= clip.w + margin) &&
                  (clip.z >= -margin) && (clip.z <= clip.w + margin);
    }

    if (visible) {
        float3 toPatch = patch.position - pos;
        float distSq = dot(toPatch, toPatch);
        bool nearBand = distSq <= nearDistanceSq;
        bool midBand = (!nearBand) && (distSq <= midDistanceSq);
        if (!nearBand && !midBand) {
            return;
        }

        uint oldCount;
        if (nearBand) {
            InterlockedAdd(g_visibleNear[0].x, 1, oldCount);
            g_visibleNear[oldCount + 1] = uint4(idx, 0, 0, 0);
            InterlockedAdd(g_indirectNear[1], 1);
        } else {
            InterlockedAdd(g_visibleMid[0].x, 1, oldCount);
            g_visibleMid[oldCount + 1] = uint4(idx, 0, 0, 0);
            InterlockedAdd(g_indirectMid[1], 1);
        }
    }
}
