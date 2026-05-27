// shaders/regir_update_cs.hlsl
// ReGIR cell candidate generation — one thread per grid cell.
// Uses RIS (Resampled Importance Sampling) to select lights that
// contribute meaningfully to each cell.

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

// Cone vs AABB conservative overlap. For spotlights the sphere bound is
// inflated by 1/cos(outer), which keeps narrow spots oriented across a room
// hitting every cell within the sphere even when most of those cells are
// outside the cone. This test rejects cells whose AABB lies entirely outside
// the (cone half-angle + cell subtended angle) cone.
//
// Approximation: the cell's angular footprint from the apex is bounded by the
// angle subtended by the cell's bounding sphere (half-diagonal). We accept if
// cos(axis, toCellCenter) >= cos(coneHalfAngle + subtended), and also accept
// if the apex is inside the cell (cells near the emitter).
bool ConeOverlapsCell(float3 apex, float3 axis, float cosHalfAngle,
                      float3 cellMin, float3 cellMax)
{
    float3 cellCenter = (cellMin + cellMax) * 0.5;
    float3 halfExtent = (cellMax - cellMin) * 0.5;
    float3 toCenter = cellCenter - apex;
    float dist = length(toCenter);

    // Apex inside (or extremely close to) the cell — assume overlap.
    float cellRadius = length(halfExtent);
    if (dist <= cellRadius + 1.0e-4)
        return true;

    float cosAxis = dot(axis, toCenter) / dist;

    // Cell subtended half-angle from apex: sin = cellRadius / dist.
    float sinSub = saturate(cellRadius / dist);
    float cosSub = sqrt(max(1.0 - sinSub * sinSub, 0.0));

    // cos(coneHalf + sub) = cosCone * cosSub - sinCone * sinSub.
    float cosConeClamped = clamp(cosHalfAngle, -1.0, 1.0);
    float sinCone = sqrt(max(1.0 - cosConeClamped * cosConeClamped, 0.0));
    float cosCombined = cosConeClamped * cosSub - sinCone * sinSub;

    return cosAxis >= cosCombined;
}

// Angular falloff for spot lights. Folds the cone direction into the
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
    float outerCos = clamp(lb.outerConeAngle, -1.0, 1.0);
    float innerCos = clamp(lb.innerConeAngle, outerCos, 1.0);
    return smoothstep(outerCos, innerCos, cosTheta);
}

float ComputeAreaFacingFactor(ReGIRLightBound lb, float3 cellCenter)
{
    float3 toCell = cellCenter - lb.center;
    float distSq = dot(toCell, toCell);
    if (distSq < 1.0e-6)
        return 1.0;

    float3 normal = lb.direction;
    if (dot(normal, normal) < 0.25)
        return 1.0;

    // sample_area_light() uses cosLight = dot(lightNormal, light -> receiver).
    return saturate(dot(normal, toCell) * rsqrt(distSq));
}

// Compute a target importance weight for a light relative to a cell.
// Uses power / distance^2 with the same per-type shaping used by the light
// evaluators. Reach is handled by the AABB cull in the caller.
float ComputeLightCellWeight(ReGIRLightBound lb, float3 cellCenter)
{
    float3 toLight = lb.center - cellCenter;
    float dist2 = dot(toLight, toLight);

    float emitterRadius = max(lb.emitterRadius, 0.0);
    float attenuationDenom = max(dist2 + emitterRadius * emitterRadius, 1.0e-6);
    if ((lb.lightIndex & 0x80000000u) != 0u)
        attenuationDenom = max(max(dist2, emitterRadius * emitterRadius), 1.0e-6);
    float spatialWeight = lb.power / attenuationDenom;

    // Type IDs match LightType in src/light.h.
    float angular = 1.0;
    if (lb.type == 2u)
        angular = ComputeSpotAngularFactor(lb, cellCenter);
    else if (lb.type == 3u || lb.type == 4u)
        angular = ComputeAreaFacingFactor(lb, cellCenter);

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
    const uint activeCandidates =
        clamp(g_regirParams.candidatesPerCell, 1u, REGIR_CANDIDATES_PER_CELL);

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
    for (uint s = 0u; s < activeCandidates; ++s)
        slots[s] = init_cell_reservoir();

    // RNG seeded by cell index + frame index. The update loops all current
    // light bounds, so every frame builds a fresh target-proportional reservoir
    // instead of mixing stale selected lights into the current PDF.
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

        // Tighter cull for spots: spotlights have a directional cone that
        // the sphere bound doesn't capture, so we'd otherwise process every
        // cell within reach even when most of them are outside the cone.
        if (lb.type == 2u) {
            if (dot(lb.direction, lb.direction) >= 0.25 &&
                !ConeOverlapsCell(lb.center, lb.direction,
                                  clamp(lb.outerConeAngle, -1.0, 1.0),
                                  cellMin, cellMax))
                continue;
        }

        // Weight folds in cone direction, so out-of-cone spots fall
        // out of the proposal naturally with weight = 0.
        float weight = ComputeLightCellWeight(lb, cellCenter);

        if (weight <= 0.0)
            continue;

        // Reservoir sampling: insert into each slot
        for (uint s = 0u; s < activeCandidates; ++s)
            update_cell_reservoir(slots[s], lb.lightIndex, weight, rng);
    }

    // Write back to global buffer
    uint base = ReGIR_CellBaseIndex(cellIndex, g_regirParams);
    for (uint s = 0u; s < activeCandidates; ++s) {
        finalize_cell_reservoir(slots[s], g_regirCells[base + s]);
    }
}
