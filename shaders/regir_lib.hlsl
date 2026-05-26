// shaders/regir_lib.hlsl
// ReGIR (Reservoir-based Grid Importance Resampling) — shared data structures
// and pure lookup functions. Buffer-dependent sampling functions live in
// common.hlsli where the global resource declarations are.

#ifndef REGIR_LIB_HLSL
#define REGIR_LIB_HLSL

// ---- GPU structs (must match C++ side in dxr_renderer.cpp) -----------------

struct ReGIRCellReservoir
{
    uint  lightIndex;  // index into g_lights, 0xFFFFFFFF = empty slot
    float weight;      // selected candidate target weight
    uint  M;           // sample count
    float W;           // cell target weight sum
};

struct ReGIRLightBound
{
    float3 center;     // world-space bounding sphere center
    float  radius;     // bounding sphere radius
    float3 direction;  // normalized spotlight direction
    float  outerConeAngle; // cos(outer cone), spot only
    float  power;      // total emissive power for importance sampling
    uint   lightIndex; // back-reference into g_lights
    uint   type;
    uint   pad0;
};

// ---- grid constants --------------------------------------------------------

#define REGIR_GRID_RES_X 16
#define REGIR_GRID_RES_Y 8
#define REGIR_GRID_RES_Z 16
#define REGIR_CANDIDATES_PER_CELL 8

// Temporal reuse: how many frames of history each cell retains.
// Higher = more variance reduction but slower reaction to scene changes.
// 64 is a moderate default — static scenes converge ~sqrt(64)=8x faster
// than the no-reuse baseline, dynamic edits still propagate within a
// handful of frames.
#define REGIR_TEMPORAL_M_CAP 64u

struct ReGIRConstants
{
    float3 gridMin;
    float  pad0;
    float3 gridMax;
    float  pad1;
    float3 cellSize;
    float  pad2;
    uint3  gridRes;
    uint   candidatesPerCell;
    uint   totalCells;
    uint   frameIndex;
    uint   proxyCount;     // emissive proxy entries in g_emissiveProxyData
    uint   lightBoundCount;
    float  cellJitterScale;
    uint   debugFlags;
    uint2  pad3;
};

// ---- world-position to cell index ------------------------------------------

uint ReGIR_WorldPosToCell(float3 worldPos, ReGIRConstants params)
{
    if (any(worldPos < params.gridMin) || any(worldPos > params.gridMax))
        return 0xFFFFFFFFu;

    uint3 coord = uint3(
        (worldPos - params.gridMin) / params.cellSize
    );
    coord = min(coord, params.gridRes - 1u);

    return coord.x +
           coord.y * params.gridRes.x +
           coord.z * params.gridRes.x * params.gridRes.y;
}

// ---- cell-index to reservoir base offset -----------------------------------
// Cell buffer is ping-ponged: frame N writes & samples from half (N & 1),
// reads previous-frame state from half ((N+1) & 1). Stride is the total
// per-half size in reservoir entries.

uint ReGIR_CellBaseIndex(uint cellIndex, ReGIRConstants params)
{
    uint stride = params.totalCells * params.candidatesPerCell;
    uint half_ = params.frameIndex & 1u;
    return half_ * stride + cellIndex * params.candidatesPerCell;
}

uint ReGIR_CellBaseIndexPrev(uint cellIndex, ReGIRConstants params)
{
    uint stride = params.totalCells * params.candidatesPerCell;
    uint half_ = (params.frameIndex + 1u) & 1u;
    return half_ * stride + cellIndex * params.candidatesPerCell;
}

#endif // REGIR_LIB_HLSL
