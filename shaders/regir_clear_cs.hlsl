// shaders/regir_clear_cs.hlsl
// Resets all ReGIR cell reservoirs to empty state.
// Dispatched when grid parameters change or lights are modified.

#include "regir_lib.hlsl"

// Bound as UAV (u36)
RWStructuredBuffer<ReGIRCellReservoir> g_regirCells : register(u36);

cbuffer ReGIRClearConstants : register(b0)
{
    uint totalCells;
    uint candidatesPerCell;
    uint pad0;
    uint pad1;
};

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint index = dispatchThreadID.x;
    if (index >= totalCells * candidatesPerCell)
        return;

    ReGIRCellReservoir empty;
    empty.lightIndex = 0xFFFFFFFFu;
    empty.weight = 0.0;
    empty.M = 0u;
    empty.W = 0.0;
    g_regirCells[index] = empty;
}
