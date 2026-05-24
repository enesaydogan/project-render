// shaders/regir_update_cs.hlsl
// ReGIR cell candidate generation — one thread per grid cell.
// Uses RIS (Resampled Importance Sampling) to select lights that
// contribute meaningfully to each cell, amortized across frames
// via temporal rotation of the starting light index.

#include "regir_lib.hlsl"
#include "random_lib.hlsl"

// SRV: pre-computed light bounds (CPU upload)
StructuredBuffer<ReGIRLightBound> g_regirLightBounds : register(t5001);

// UAV: cell reservoir output
RWStructuredBuffer<ReGIRCellReservoir> g_regirCells : register(u36);

cbuffer ReGIRUpdateConstants : register(b0)
{
    ReGIRConstants g_regirParams;
    uint g_numLightBounds;
    uint g_updatePad0;
    uint g_updatePad1;
    uint g_updatePad2;
};

// Simple cell reservoir (local to the update shader, unpacked from global buffer)
struct CellReservoir
{
    uint  lightIndex;
    float w_sum;
    uint  M;
};

CellReservoir init_cell_reservoir()
{
    CellReservoir r;
    r.lightIndex = 0xFFFFFFFFu;
    r.w_sum = 0.0;
    r.M = 0u;
    return r;
}

bool update_cell_reservoir(inout CellReservoir r, uint lightIndex,
                           float weight, inout RNG rng)
{
    if (!isfinite(weight) || weight <= 0.0)
        return false;

    float newWSum = r.w_sum + weight;
    r.M++;

    bool selected = (next_float(rng) * newWSum < weight);
    if (selected)
        r.lightIndex = lightIndex;
    r.w_sum = min(newWSum, 1e10);
    return selected;
}

void finalize_cell_reservoir(inout CellReservoir r,
                             out ReGIRCellReservoir outSlot)
{
    outSlot.lightIndex = r.lightIndex;
    outSlot.weight = r.w_sum;
    outSlot.M = min(r.M, 255u);
    if (r.M > 0u)
        outSlot.W = r.w_sum / (float)r.M;
    else
        outSlot.W = 0.0;
}

// Sphere vs AABB overlap test
bool SphereOverlapsCell(float3 sphereCenter, float sphereRadius,
                        float3 cellMin, float3 cellMax)
{
    float3 closest = clamp(sphereCenter, cellMin, cellMax);
    float3 diff = sphereCenter - closest;
    return dot(diff, diff) <= (sphereRadius * sphereRadius);
}

// Compute a target importance weight for a light relative to a cell.
// Uses power / distance² with a soft floor.
float ComputeLightCellWeight(float3 lightCenter, float lightRadius,
                             float lightPower, float3 cellCenter)
{
    float3 toLight = lightCenter - cellCenter;
    float dist2 = dot(toLight, toLight);
    float effectiveDist2 = max(dist2, lightRadius * lightRadius * 0.25);
    return lightPower / max(effectiveDist2, 1.0e-6);
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint cellIndex = dispatchThreadID.x;
    if (cellIndex >= g_regirParams.totalCells)
        return;
    if (g_numLightBounds == 0u)
        return;

    // Compute world-space cell AABB
    uint cx = cellIndex % g_regirParams.gridRes.x;
    uint cy = (cellIndex / g_regirParams.gridRes.x) % g_regirParams.gridRes.y;
    uint cz = cellIndex / (g_regirParams.gridRes.x * g_regirParams.gridRes.y);

    float3 cellMin = g_regirParams.gridMin +
                     float3(cx, cy, cz) * g_regirParams.cellSize;
    float3 cellMax = cellMin + g_regirParams.cellSize;
    float3 cellCenter = (cellMin + cellMax) * 0.5;
    float cellRadius = length(g_regirParams.cellSize) * 0.5;

    // Initialize per-slot reservoirs
    CellReservoir slots[REGIR_CANDIDATES_PER_CELL];
    for (uint s = 0u; s < REGIR_CANDIDATES_PER_CELL; ++s)
        slots[s] = init_cell_reservoir();

    // RNG seeded by cell index + frame index (temporal rotation)
    RNG rng;
    rng.state = (cellIndex * 0x9E3779B1u) ^
                (g_regirParams.frameIndex * 0x6C8E9CF5u) ^ 0xA24BAED5u;

    // Stagger: start at a different light each frame to amortize
    uint startLight = next_uint(rng) % max(g_numLightBounds, 1u);

    // Iterate all lights (wrapping around from startLight)
    for (uint li = 0u; li < g_numLightBounds; ++li) {
        uint lightIdx = (startLight + li) % g_numLightBounds;
        ReGIRLightBound lb = g_regirLightBounds[lightIdx];

        // Bounding sphere test
        if (!SphereOverlapsCell(lb.center, lb.radius, cellMin, cellMax))
            continue;

        float weight = ComputeLightCellWeight(
            lb.center, lb.radius, lb.power, cellCenter);

        if (weight <= 0.0)
            continue;

        // Reservoir sampling: insert into each slot
        for (uint s = 0u; s < REGIR_CANDIDATES_PER_CELL; ++s)
            update_cell_reservoir(slots[s], lb.lightIndex, weight, rng);
    }

    // Write back to global buffer
    uint base = ReGIR_CellBaseIndex(cellIndex, g_regirParams);
    for (uint s = 0u; s < REGIR_CANDIDATES_PER_CELL; ++s) {
        finalize_cell_reservoir(slots[s], g_regirCells[base + s]);
    }
}
