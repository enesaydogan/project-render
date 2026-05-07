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
    const uint dispatchCount = activeCount;
    const uint groupCountX = (dispatchCount + 63u) / 64u;

    if (dispatchArgsIndex != kInvalidDispatchArgsIndex) {
        WavefrontDispatchArgs args;
        args.groupCountX = groupCountX;
        args.groupCountY = 1u;
        args.groupCountZ = 1u;
        args.activeCount = activeCount;
        g_wavefrontDispatchArgs[dispatchArgsIndex] = args;
    }

    if (reservedSlotBase != kInvalidReservedSlotBase) {
        // D3D12_DISPATCH_RAYS_DESC layout within WavefrontDispatchRaysRecordGpu (uint4 view):
        //   element[slotBase+5]: { CallableShaderTable.StrideInBytes.lo,
        //                          CallableShaderTable.StrideInBytes.hi,
        //                          Width, Height }
        //   element[slotBase+6]: { Depth, D3D12-struct-padding,
        //                          WF-padding[0], WF-padding[1]/queue-flags }
        // Write Width into element[slotBase+5].z and Height=1 into .w.
        // Write Depth=1 into element[slotBase+6].x and queue flags into .w.
        uint4 dims5 = g_wavefrontReserved[reservedSlotBase + 5u];
        dims5.z = dispatchCount; // Width
        dims5.w = 1u;            // Height = 1
        g_wavefrontReserved[reservedSlotBase + 5u] = dims5;

        uint4 dims6 = g_wavefrontReserved[reservedSlotBase + 6u];
        dims6.x = 1u;            // Depth = 1
        dims6.w = reservedFlags; // queue flags (read by WavefrontSecondaryRayGen)
        g_wavefrontReserved[reservedSlotBase + 6u] = dims6;
    }
}
