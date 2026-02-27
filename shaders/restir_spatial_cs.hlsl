// shaders/restir_spatial_cs.hlsl
// Dedicated ReSTIR DI temporal/spatial reuse pass.

#include "restir_lib.hlsl"

// Keep Camera layout-compatible with raytracing/common.hlsli (b0) while only
// exposing fields this compute pass needs.
cbuffer Camera : register(b0)
{
    float globalFrameCount : packoffset(c4.y);
    float dlssRayReconstruction : packoffset(c11.w);
}

RWTexture2D<float> g_depth : register(u10);
RWTexture2D<float4> g_normalRoughnessOut : register(u13);

RWTexture2D<float4> g_reservoir0 : register(u2);
RWTexture2D<float4> g_reservoir1 : register(u3);

static const uint kGroupSize = 8;
static const uint kTileSize = kGroupSize + 2;

groupshared float  s_depthTile[kTileSize * kTileSize];
groupshared float4 s_normalTile[kTileSize * kTileSize];
groupshared float4 s_prevReservoirTile[kTileSize * kTileSize];

uint tile_index(uint2 p) {
    return p.y * kTileSize + p.x;
}

Reservoir unpack_reservoir(float4 data) {
    Reservoir r;
    r.lightIndex = asuint(data.x);
    if (r.lightIndex == 0x7FC00000) r.lightIndex = 0xFFFFFFFF;
    r.w_sum = data.y;
    r.M = asuint(data.z);
    r.W = data.w;
    if (isnan(r.w_sum) || isinf(r.w_sum)) r.w_sum = 0.0;
    if (isnan(r.W) || isinf(r.W)) r.W = 0.0;
    return r;
}

float4 pack_reservoir(Reservoir r) {
    return float4(asfloat(r.lightIndex), r.w_sum, asfloat(r.M), r.W);
}

bool IsSpatiallyCompatibleData(float4 n0, float d0, float4 n1, float d1,
                               float ndotMin, float depthTolBase)
{
    float3 nn0 = n0.xyz;
    float3 nn1 = n1.xyz;
    float l0 = dot(nn0, nn0);
    float l1 = dot(nn1, nn1);

    if (l0 <= 0.25 || l1 <= 0.25) return false;
    nn0 = normalize(nn0);
    nn1 = normalize(nn1);
    if (dot(nn0, nn1) < ndotMin) return false;
    if (abs(n0.w - n1.w) > 0.25) return false;

    if (!isfinite(d0) || !isfinite(d1) || d0 <= 0.0 || d1 <= 0.0) return false;

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
    s_depthTile[idx] = g_depth[p];
    s_normalTile[idx] = g_normalRoughnessOut[p];
    s_prevReservoirTile[idx] = flip ? g_reservoir0[p] : g_reservoir1[p];
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
    if (dlssRayReconstruction > 0.5) {
        // Keep RR path minimally correlated.
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

    float4 curData = flip ? g_reservoir1[pix] : g_reservoir0[pix];
    Reservoir res = unpack_reservoir(curData);
    if (res.M == 0) {
        // No candidate from raygen; keep as-is.
        return;
    }

    // Temporal reuse from previous ping-pong side.
    if (frame > 0) {
        float4 prevData = s_prevReservoirTile[tile_index(sharedCenter)];
        Reservoir prev = unpack_reservoir(prevData);
        prev.M = min(prev.M, 30);
        combine_reservoirs(res, prev, 1.0, rng);
    }

    float4 centerNormal = s_normalTile[tile_index(sharedCenter)];
    float centerDepth = s_depthTile[tile_index(sharedCenter)];

    // Spatial reuse from previous frame neighbors.
    static const int2 kOffsets[4] = {
        int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1)
    };
    [unroll]
    for (uint i = 0; i < 4; ++i) {
        uint2 sN = uint2(int2(sharedCenter) + kOffsets[i]);
        float4 neighNormal = s_normalTile[tile_index(sN)];
        float neighDepth = s_depthTile[tile_index(sN)];
        if (!IsSpatiallyCompatibleData(centerNormal, centerDepth,
                                       neighNormal, neighDepth,
                                       0.96, 0.006)) {
            continue;
        }

        float4 neighData = s_prevReservoirTile[tile_index(sN)];
        Reservoir neigh = unpack_reservoir(neighData);
        neigh.M = min(neigh.M, 8);
        combine_reservoirs(res, neigh, 1.0, rng);
    }

    finalize_reservoir(res, 1.0);
    float4 outData = pack_reservoir(res);
    if (flip) {
        g_reservoir1[pix] = outData;
    } else {
        g_reservoir0[pix] = outData;
    }
}
