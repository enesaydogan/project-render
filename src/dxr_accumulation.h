#pragma once
#include <d3d12.h>
#include <wrl.h>

using Microsoft::WRL::ComPtr;

class DxrAccumulation {
public:
    DxrAccumulation();
    ~DxrAccumulation();

    void Initialize(ID3D12Device* device, UINT width, UINT height);
    void Resize(UINT width, UINT height);
    void Reset();
    void IncrementFrame();
    void Clear(ID3D12GraphicsCommandList4* commandList);

    UINT GetFrameCount() const { return m_frameCount; }
    ID3D12Resource* GetAccumulationBuffer() const { return m_accumulationBuffer.Get(); }
    D3D12_GPU_DESCRIPTOR_HANDLE GetAccumulationUAV() const { return m_accumulationUAVGpu; }
    ID3D12Resource* GetVarianceBuffer() const { return m_varianceBuffer.Get(); }
    D3D12_GPU_DESCRIPTOR_HANDLE GetVarianceUAV() const { return m_varianceUAVGpu; }

    // Check if accumulation is needed (e.g. if we haven't reached a limit)
    bool IsAccumulating() const { return m_isAccumulating; }
    void SetEnabled(bool enabled) { m_enabled = enabled; if(!enabled) Reset(); }
    bool IsEnabled() const { return m_enabled; }

    bool NeedsClear() const { return m_needsClear; }
    void SetNeedsClear(bool needs) { m_needsClear = needs; }

private:
    void CreateResources(UINT width, UINT height);

    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12Resource> m_accumulationBuffer;
    ComPtr<ID3D12Resource> m_varianceBuffer;
    ComPtr<ID3D12DescriptorHeap> m_uavHeap;
    D3D12_GPU_DESCRIPTOR_HANDLE m_accumulationUAVGpu;
    D3D12_GPU_DESCRIPTOR_HANDLE m_varianceUAVGpu;

    UINT m_frameCount = 0;
    UINT m_width = 0;
    UINT m_height = 0;
    bool m_isAccumulating = true;
    bool m_enabled = true;
    bool m_needsClear = false;
};
