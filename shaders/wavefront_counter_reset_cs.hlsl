#include "raytracing/common.hlsli"

cbuffer WavefrontCounterResetConstants : register(b0)
{
    uint counterIndex;
    uint resetValue;
    uint resetCount;
    uint reserved1;
};

[numthreads(1, 1, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    if (dispatchThreadID.x != 0u || dispatchThreadID.y != 0u || dispatchThreadID.z != 0u) {
        return;
    }

    const uint count = max(resetCount, 1u);
    [loop]
    for (uint i = 0u; i < count; ++i) {
        const uint index = counterIndex + i;
        if (index >= WAVEFRONT_QUEUE_COUNTER_COUNT) {
            break;
        }
        g_wavefrontQueueCounters[index] = resetValue;
    }
}
