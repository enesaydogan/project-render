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
};

// Simple cell reservoir (local to the update shader, unpacked from global buffer)
struct CellReservoir
{
    uint  lightIndex;
    float selectedWeight;
    float w_sum;
    uint  M;
};

CellReservoir init_cell_reservoir()
{
    CellReservoir r;
    r.lightIndex = 0xFFFFFFFFu;
    r.selectedWeight = 0.0;
    r.w_sum = 0.0;
    r.M = 0u;
    return r;
}

bool update_cell_reservoir(inout CellReservoir r, uint lightIndex,
                           float weight, inout RNG rng)
{
    // Count every proposal — M is "number of candidates considered", not
    // "number of accepted candidates". Skipping M++ on zero-weight inputs
    // breaks the RIS interpretation downstream (sample_weight = W / (M * p)).
    r.M++;

    if (!isfinite(weight) || weight <= 0.0)
        return false;

    float newWSum = r.w_sum + weight;
    bool selected = (next_float(rng) * newWSum < weight);
    if (selected) {
        r.lightIndex = lightIndex;
        r.selectedWeight = weight;
    }
    r.w_sum = min(newWSum, 1e10);
    return selected;
}

void finalize_cell_reservoir(inout CellReservoir r,
                             out ReGIRCellReservoir outSlot)
{
    outSlot.lightIndex = r.lightIndex;
    outSlot.weight = r.selectedWeight;
    outSlot.M = min(r.M, 255u);
    if (r.M > 0u)
        outSlot.W = r.w_sum;
    else
        outSlot.W = 0.0;
}

// Sphere vs AABB overlap. Cheap fast-path skip: if the light's reach
// sphere doesn't touch the cell at all, we can drop it before the more
// expensive weight/cone math. This is a real win once N_lights grows
// into the hundreds.
bool SphereOverlapsCell(float3 sphereCenter, float sphereRadius,
                        float3 cellMin, float3 cellMax)
{
    float3 closest = clamp(sphereCenter, cellMin, cellMax);
    float3 diff = sphereCenter - closest;
    return dot(diff, diff) <= (sphereRadius * sphereRadius);
}

// Angular falloff for spot/IES lights. Folds the cone direction into the
// proposal distribution so RIS doesn't pick a spotlight whose axis points
// away from the cell — those candidates contribute zero radiance and inflate
// variance once the per-cell light count gets large.
float ComputeSpotAngularFactor(ReGIRLightBound lb, float3 cellCenter)
{
    float3 toCell = cellCenter - lb.center;
    float distSq = dot(toCell, toCell);
    if (distSq < 1.0e-6)
        return 1.0;

    float3 axis = lb.direction;
    if (dot(axis, axis) < 0.25)
        return 1.0;

    float cosTheta = dot(axis, toCell) * rsqrt(distSq);
    float outerCos = clamp(lb.outerConeAngle, 0.01, 0.999);
    // Soft inner boundary so cells straddling the cone edge still get
    // nonzero proposal weight. Width is 25% of (1 - outer) — gentle ramp.
    float innerCos = lerp(outerCos, 1.0, 0.25);
    return smoothstep(outerCos, innerCos, cosTheta);
}

// Compute a target importance weight for a light relative to a cell.
// Uses power / distance² gated by the spotlight cone direction so the RIS
// proposal matches the actual radiance contribution. Reach is handled by
// the AABB cull in the caller.
float ComputeLightCellWeight(ReGIRLightBound lb, float3 cellCenter)
{
    float3 toLight = lb.center - cellCenter;
    float dist2 = dot(toLight, toLight);

    float effectiveDist2 = max(dist2, lb.radius * lb.radius * 0.25);
    float spatialWeight = lb.power / max(effectiveDist2, 1.0e-6);

    // Spot/IES cone term. Type IDs match LightType in src/light.h:
    //   2 = Spot, 5 = IES (IES profiles have a forward lobe — same axis).
    float angular = 1.0;
    if (lb.type == 2u || lb.type == 5u)
        angular = ComputeSpotAngularFactor(lb, cellCenter);

    return spatialWeight * angular;
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint cellIndex = dispatchThreadID.x;
    if (cellIndex >= g_regirParams.totalCells)
        return;
    const uint numLightBounds = g_regirParams.lightBoundCount;
    if (numLightBounds == 0u)
        return;

    // Compute world-space cell AABB + center
    uint cx = cellIndex % g_regirParams.gridRes.x;
    uint cy = (cellIndex / g_regirParams.gridRes.x) % g_regirParams.gridRes.y;
    uint cz = cellIndex / (g_regirParams.gridRes.x * g_regirParams.gridRes.y);

    float3 cellMin = g_regirParams.gridMin +
                     float3(cx, cy, cz) * g_regirParams.cellSize;
    float3 cellMax = cellMin + g_regirParams.cellSize;
    float3 cellCenter = (cellMin + cellMax) * 0.5;

    // Initialize per-slot reservoirs
    CellReservoir slots[REGIR_CANDIDATES_PER_CELL];
    for (uint s = 0u; s < REGIR_CANDIDATES_PER_CELL; ++s)
        slots[s] = init_cell_reservoir();

    // RNG seeded by cell index + frame index (temporal rotation)
    RNG rng;
    rng.state = (cellIndex * 0x9E3779B1u) ^
                (g_regirParams.frameIndex * 0x6C8E9CF5u) ^ 0xA24BAED5u;

    // Stagger: start at a different light each frame to amortize
    uint startLight = next_uint(rng) % max(numLightBounds, 1u);

    // Iterate all lights (wrapping around from startLight)
    for (uint li = 0u; li < numLightBounds; ++li) {
        uint lightIdx = (startLight + li) % numLightBounds;
        ReGIRLightBound lb = g_regirLightBounds[lightIdx];

        // Fast-path reach skip: light's bounding sphere doesn't touch
        // the cell AABB → no contribution. Saves the cone math for the
        // common case of 500+ lights where most are out of range.
        if (!SphereOverlapsCell(lb.center, lb.radius, cellMin, cellMax))
            continue;

        // Weight folds in cone direction, so out-of-cone spots fall
        // out of the proposal naturally with weight = 0.
        float weight = ComputeLightCellWeight(lb, cellCenter);

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
