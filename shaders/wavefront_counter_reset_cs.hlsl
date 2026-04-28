#include "raytracing/common.hlsli"

cbuffer WavefrontCounterResetConstants : register(b0)
{
    uint counterIndex;
    uint resetValue;
    uint reserved0;
    uint reserved1;
};

[numthreads(1, 1, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    if (dispatchThreadID.x != 0u || dispatchThreadID.y != 0u || dispatchThreadID.z != 0u) {
        return;
    }

    g_wavefrontQueueCounters[counterIndex] = resetValue;
}