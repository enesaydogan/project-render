// shaders/restir_gi_spatial_cs.hlsl
// Dedicated ReSTIR GI temporal/spatial reuse pass.

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

RWTexture2D<float4> g_gi_reservoir_a0 : register(u4);
RWTexture2D<float4> g_gi_reservoir_a1 : register(u5);
RWTexture2D<float4> g_gi_reservoir_a2 : register(u6);
RWTexture2D<float4> g_gi_reservoir_b0 : register(u7);
RWTexture2D<float4> g_gi_reservoir_b1 : register(u8);
RWTexture2D<float4> g_gi_reservoir_b2 : register(u9);

static const uint kGroupSize = 8;
static const uint kTileSize = kGroupSize + 2;

groupshared float  s_depthTile[kTileSize * kTileSize];
groupshared float4 s_normalTile[kTileSize * kTileSize];
groupshared float4 s_prevGi0Tile[kTileSize * kTileSize];
groupshared float4 s_prevGi1Tile[kTileSize * kTileSize];
groupshared float4 s_prevGi2Tile[kTileSize * kTileSize];

uint tile_index(uint2 p) {
    return p.y * kTileSize + p.x;
}

GI_Reservoir unpack_gi_reservoir(float4 d0, float4 d1, float4 d2) {
    GI_Reservoir r;
    r.hitPos = d0.xyz;
    r.radiance = d1.xyz;
    r.w_sum = d2.x;
    r.M = asuint(d2.y);
    r.W = d2.z;
    if (!isfinite(r.w_sum) || r.w_sum < 0.0) r.w_sum = 0.0;
    if (!isfinite(r.W) || r.W < 0.0) r.W = 0.0;
    return r;
}

void pack_gi_reservoir(GI_Reservoir r, out float4 d0, out float4 d1, out float4 d2) {
    d0 = float4(r.hitPos, 0.0);
    d1 = float4(r.radiance, 0.0);
    d2 = float4(r.w_sum, asfloat(r.M), r.W, 0.0);
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
        s_prevGi0Tile[idx] = float4(0.0, 0.0, 0.0, 0.0);
        s_prevGi1Tile[idx] = float4(0.0, 0.0, 0.0, 0.0);
        s_prevGi2Tile[idx] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    uint2 p = uint2(pix);
    s_depthTile[idx] = g_depth[p];
    s_normalTile[idx] = g_normalRoughnessOut[p];
    if (flip) {
        s_prevGi0Tile[idx] = g_gi_reservoir_b0[p];
        s_prevGi1Tile[idx] = g_gi_reservoir_b1[p];
        s_prevGi2Tile[idx] = g_gi_reservoir_b2[p];
    } else {
        s_prevGi0Tile[idx] = g_gi_reservoir_a0[p];
        s_prevGi1Tile[idx] = g_gi_reservoir_a1[p];
        s_prevGi2Tile[idx] = g_gi_reservoir_a2[p];
    }
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
    if (dlssRayReconstruction > 0.5) return;

    int2 groupBase = int2(gid.xy * kGroupSize);
    int2 localPix = groupBase + int2(gtid.xy);
    uint2 sharedCenter = gtid.xy + uint2(1, 1);

    // Stage previous-frame GI reservoirs and compatibility data in LDS.
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
        LoadSharedSlot(uint2(kTileSize - 1, 0), localPix + int2(1, -1), dim,
                       flip);
    }
    if (gtid.x == 0 && gtid.y == kGroupSize - 1) {
        LoadSharedSlot(uint2(0, kTileSize - 1), localPix + int2(-1, 1), dim,
                       flip);
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
    RNG rng = init_rng(pix + uint2(0xA113u, 0x91C1u), frame ^ 0x6ABEu);

    float4 cur0, cur1, cur2;
    if (flip) {
        cur0 = g_gi_reservoir_a0[pix];
        cur1 = g_gi_reservoir_a1[pix];
        cur2 = g_gi_reservoir_a2[pix];
    } else {
        cur0 = g_gi_reservoir_b0[pix];
        cur1 = g_gi_reservoir_b1[pix];
        cur2 = g_gi_reservoir_b2[pix];
    }
    GI_Reservoir res = unpack_gi_reservoir(cur0, cur1, cur2);
    if (res.M == 0) return;

    // Temporal reuse from previous ping-pong side.
    if (frame > 0) {
        uint centerIdx = tile_index(sharedCenter);
        float4 prev0 = s_prevGi0Tile[centerIdx];
        float4 prev1 = s_prevGi1Tile[centerIdx];
        float4 prev2 = s_prevGi2Tile[centerIdx];
        GI_Reservoir prev = unpack_gi_reservoir(prev0, prev1, prev2);
        prev.M = min(prev.M, 15);
        combine_gi_reservoirs(res, prev, 1.0, rng);
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
                                       0.94, 0.010)) continue;

        uint idx = tile_index(sN);
        float4 d0 = s_prevGi0Tile[idx];
        float4 d1 = s_prevGi1Tile[idx];
        float4 d2 = s_prevGi2Tile[idx];
        GI_Reservoir neigh = unpack_gi_reservoir(d0, d1, d2);
        neigh.M = min(neigh.M, 8);
        combine_gi_reservoirs(res, neigh, 1.0, rng);
    }

    finalize_gi_reservoir(res, 1.0);
    float4 out0, out1, out2;
    pack_gi_reservoir(res, out0, out1, out2);
    if (flip) {
        g_gi_reservoir_a0[pix] = out0;
        g_gi_reservoir_a1[pix] = out1;
        g_gi_reservoir_a2[pix] = out2;
    } else {
        g_gi_reservoir_b0[pix] = out0;
        g_gi_reservoir_b1[pix] = out1;
        g_gi_reservoir_b2[pix] = out2;
    }
}
