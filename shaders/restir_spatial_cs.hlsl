#include "restir_lib.hlsl"
#include "raytracing/common.hlsli"
#include "brdf_lib.hlsl"
#include "lights_lib.hlsl"

static const uint kGroupSize = 8;
static const uint kTileSize = kGroupSize + 2;

groupshared float  s_depthTile[kTileSize * kTileSize];
groupshared float4 s_normalTile[kTileSize * kTileSize];
groupshared float4 s_prevReservoirTile[kTileSize * kTileSize];

uint tile_index(uint2 p) {
    return p.y * kTileSize + p.x;
}

bool IsValidSurfaceData(float4 normalRoughness, float depth)
{
    float normalLenSq = dot(normalRoughness.xyz, normalRoughness.xyz);
    return isfinite(depth) && depth > 0.0 &&
           isfinite(normalRoughness.x) &&
           isfinite(normalRoughness.y) &&
           isfinite(normalRoughness.z) &&
           isfinite(normalRoughness.w) &&
           normalLenSq > 0.25 &&
           normalLenSq < 4.0;
}

float3 SafeNormalize3(float3 v, float3 fallback)
{
    float lenSq = dot(v, v);
    return (isfinite(lenSq) && lenSq > 1.0e-8) ? v * rsqrt(lenSq)
                                                : fallback;
}

bool IsSpatiallyCompatibleData(float4 n0, float d0, float4 n1, float d1,
                               float ndotMin, float depthTolBase)
{
    if (!IsValidSurfaceData(n0, d0) || !IsValidSurfaceData(n1, d1)) {
        return false;
    }

    float3 nn0 = n0.xyz;
    float3 nn1 = n1.xyz;
    nn0 = SafeNormalize3(nn0, float3(0.0, 1.0, 0.0));
    nn1 = SafeNormalize3(nn1, float3(0.0, 1.0, 0.0));
    if (dot(nn0, nn1) < ndotMin) return false;
    if (abs(n0.w - n1.w) > 0.25) return false;

    float depthTol = depthTolBase * min(d0, d1);
    if (abs(d0 - d1) > max(0.05, depthTol)) return false;

    return true;
}

void LoadSharedSlot(uint2 slot, int2 pix, uint2 dim, bool flip)
{
    uint idx = tile_index(slot);
    bool inside =
        pix.x >= 0 && pix.y >= 0 && pix.x < (int)dim.x && pix.y < (int)dim.y;
    if (!inside) {
        s_depthTile[idx] = 0.0;
        s_normalTile[idx] = float4(0.0, 0.0, 0.0, 0.0);
        s_prevReservoirTile[idx] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    uint2 p = uint2(pix);
    s_depthTile[idx] = g_linearDepth[p];
    s_normalTile[idx] = g_normalRoughnessOut[p];
    s_prevReservoirTile[idx] = flip ? g_reservoir0[p] : g_reservoir1[p];
}

// Reconstruct world-space position from pixel UV + linear depth.
float3 ReconstructWorldPos(uint2 pix, uint2 dim, float depth)
{
    float2 uv = ((float2)pix + 0.5) / (float2)dim;
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y; // flip Y

    float halfH = tan(fov * 0.5);
    float halfW = halfH * aspect;

    float3 forward = SafeNormalize3(camForward, float3(0.0, 0.0, 1.0));
    float3 cameraUp = SafeNormalize3(camUp, float3(0.0, 1.0, 0.0));
    float3 right = SafeNormalize3(cross(forward, cameraUp),
                                  float3(1.0, 0.0, 0.0));
    float3 up = SafeNormalize3(cross(right, forward), cameraUp);

    float3 dir = SafeNormalize3(forward + right * (ndc.x * halfW) +
                                    up * (ndc.y * halfH),
                                forward);
    return camPos + dir * depth;
}

// Evaluate a simplified p_target for a reservoir candidate at the given surface.
float EvalCandidatePTarget(uint candidateLightIndex, float3 N, float3 P)
{
    if (!all(isfinite(N)) || !all(isfinite(P))) {
        return 0.0;
    }
    uint numLights = (uint)max(lightCount, 0.0);
    if (candidateLightIndex != 0xFFFFFFFFu) {
        if (WavefrontIsEmissiveProxyLightIndex(candidateLightIndex)) {
            WavefrontLightSample proxy =
                WavefrontSampleEmissiveProxyLight(P, candidateLightIndex, 1.0);
            return max(0.0, length(proxy.radiance *
                                   saturate(dot(N, proxy.direction))));
        }
        if (candidateLightIndex >= numLights) {
            return 0.0;
        }
        Light l = g_lights[candidateLightIndex];
        LightSample ls = evaluate_light(l, P);
        return max(0.0, length(ls.radiance * saturate(dot(N, ls.L))));
    }
    return 0.0;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID,
            uint3 gtid : SV_GroupThreadID,
            uint3 gid : SV_GroupID)
{
    uint2 dim;
    g_depth.GetDimensions(dim.x, dim.y);
    uint frame = (uint)globalFrameCount;
    bool flip = (frame & 1u) == 1u;
    if (dlssRayReconstruction > 0.5 || dim.x == 0u || dim.y == 0u) {
        return;
    }

    int2 groupBase = int2(gid.xy * kGroupSize);
    int2 localPix = groupBase + int2(gtid.xy);
    uint2 sharedCenter = gtid.xy + uint2(1, 1);

    // Stage previous-frame reservoirs and compatibility data in LDS.
    LoadSharedSlot(sharedCenter, localPix, dim, flip);
    if (gtid.x == 0) {
        LoadSharedSlot(uint2(0, gtid.y + 1), localPix + int2(-1, 0), dim, flip);
    }
    if (gtid.x == kGroupSize - 1) {
        LoadSharedSlot(uint2(kTileSize - 1, gtid.y + 1),
                       localPix + int2(1, 0), dim, flip);
    }
    if (gtid.y == 0) {
        LoadSharedSlot(uint2(gtid.x + 1, 0), localPix + int2(0, -1), dim, flip);
    }
    if (gtid.y == kGroupSize - 1) {
        LoadSharedSlot(uint2(gtid.x + 1, kTileSize - 1),
                       localPix + int2(0, 1), dim, flip);
    }
    if (gtid.x == 0 && gtid.y == 0) {
        LoadSharedSlot(uint2(0, 0), localPix + int2(-1, -1), dim, flip);
    }
    if (gtid.x == kGroupSize - 1 && gtid.y == 0) {
        LoadSharedSlot(uint2(kTileSize - 1, 0), localPix + int2(1, -1), dim, flip);
    }
    if (gtid.x == 0 && gtid.y == kGroupSize - 1) {
        LoadSharedSlot(uint2(0, kTileSize - 1), localPix + int2(-1, 1), dim, flip);
    }
    if (gtid.x == kGroupSize - 1 && gtid.y == kGroupSize - 1) {
        LoadSharedSlot(uint2(kTileSize - 1, kTileSize - 1),
                       localPix + int2(1, 1), dim, flip);
    }
    GroupMemoryBarrierWithGroupSync();

    if (localPix.x < 0 || localPix.y < 0 ||
        localPix.x >= (int)dim.x || localPix.y >= (int)dim.y) {
        return;
    }

    uint2 pix = uint2(localPix);
    RNG rng = init_rng(pix + uint2(0x51D2u, 0xC3A5u), frame ^ 0xBA5Eu);

    float4 centerNormalRoughness = s_normalTile[tile_index(sharedCenter)];
    float centerDepth = s_depthTile[tile_index(sharedCenter)];

    float4 curData = flip ? g_reservoir1[pix] : g_reservoir0[pix];
    Reservoir res = unpack_reservoir(curData);
    if (res.M == 0) {
        return;
    }
    if (!IsValidSurfaceData(centerNormalRoughness, centerDepth)) {
        if (flip) {
            g_reservoir1[pix] = pack_reservoir(init_reservoir());
        } else {
            g_reservoir0[pix] = pack_reservoir(init_reservoir());
        }
        return;
    }

    float3 N = SafeNormalize3(centerNormalRoughness.xyz,
                              float3(0.0, 1.0, 0.0));

    // Reconstruct world position for correct local light evaluation.
    float3 P = ReconstructWorldPos(pix, dim, centerDepth);

    // Temporal reuse
    if (accumulationCount > 0.0) {
        float4 prevData = s_prevReservoirTile[tile_index(sharedCenter)];
        Reservoir prev = unpack_reservoir(prevData);
        prev.M = min(prev.M, 30);
        float p_target = EvalCandidatePTarget(prev.lightIndex, N, P);
        combine_reservoirs(res, prev, p_target, rng);

        // Spatial reuse from 4 immediate neighbors
        // Only run if history is valid, otherwise we pull uninitialized neighbor reservoirs!
        static const int2 kOffsets[4] = {
            int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1)
        };
        [unroll]
        for (uint i = 0; i < 4; ++i) {
            uint2 sN = uint2(int2(sharedCenter) + kOffsets[i]);
            float4 neighNormal = s_normalTile[tile_index(sN)];
            float neighDepth = s_depthTile[tile_index(sN)];
            if (!IsSpatiallyCompatibleData(centerNormalRoughness, centerDepth,
                                           neighNormal, neighDepth,
                                           0.96, 0.006)) {
                continue;
            }

            float4 neighData = s_prevReservoirTile[tile_index(sN)];
            Reservoir neigh = unpack_reservoir(neighData);
            neigh.M = min(neigh.M, 8);
            float p_target = EvalCandidatePTarget(neigh.lightIndex, N, P);
            combine_reservoirs(res, neigh, p_target, rng);
        }
    }

    float final_p_target = EvalCandidatePTarget(res.lightIndex, N, P);
    finalize_reservoir(res, final_p_target);

    float4 outData = pack_reservoir(res);
    if (flip) {
        g_reservoir1[pix] = outData;
    } else {
        g_reservoir0[pix] = outData;
    }
}
