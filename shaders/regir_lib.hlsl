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
    uint   lightsDirty;
    uint   lightBoundCount;
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

uint ReGIR_CellBaseIndex(uint cellIndex, ReGIRConstants params)
{
    return cellIndex * params.candidatesPerCell;
}

#endif // REGIR_LIB_HLSL
