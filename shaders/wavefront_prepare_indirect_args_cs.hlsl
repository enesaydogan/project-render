#include "raytracing/common.hlsli"

cbuffer WavefrontPrepareIndirectArgsConstants : register(b0)
{
    uint queueCounterIndex;
    uint dispatchArgsIndex;
    uint reservedSlotBase;
    uint maxDispatchCount;
    uint reservedFlags;
};

static const uint kInvalidDispatchArgsIndex = 0xFFFFFFFFu;
static const uint kInvalidReservedSlotBase = 0xFFFFFFFFu;

[numthreads(1, 1, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    if (dispatchThreadID.x != 0u || dispatchThreadID.y != 0u || dispatchThreadID.z != 0u) {
        return;
    }

    const uint activeCount = min(maxDispatchCount, g_wavefrontQueueCounters[queueCounterIndex]);
    const uint dispatchCount = max(activeCount, 1u);
    const uint groupCountX = max((dispatchCount + 63u) / 64u, 1u);

    if (dispatchArgsIndex != kInvalidDispatchArgsIndex) {
        WavefrontDispatchArgs args;
        args.groupCountX = groupCountX;
        args.groupCountY = 1u;
        args.groupCountZ = 1u;
        args.activeCount = activeCount;
        g_wavefrontDispatchArgs[dispatchArgsIndex] = args;
    }

    if (reservedSlotBase != kInvalidReservedSlotBase) {
        uint4 dims = g_wavefrontReserved[reservedSlotBase + 6u];
        dims.x = dispatchCount;
        dims.y = 1u;
        dims.z = 1u;
        dims.w = reservedFlags;
        g_wavefrontReserved[reservedSlotBase + 6u] = dims;
    }
}
