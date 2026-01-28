#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <string>
#include "asset_loader.h"

class IBLManager {
public:
    static IBLManager& Get() {
        static IBLManager instance;
        return instance;
    }

    bool Initialize(ID3D12Device* device, ID3D12CommandQueue* queue);
    bool LoadEnvironmentMap(const std::string& path);

    Asset::Texture& GetEnvMap() { return m_envMap; }
    bool IsLoaded() const { return m_envMap.resource != nullptr; }

    void SetGPUHandle(D3D12_GPU_DESCRIPTOR_HANDLE handle) { m_gpuHandle = handle; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandle() const { return m_gpuHandle; }
    
    void SetCPUHandle(D3D12_CPU_DESCRIPTOR_HANDLE handle) { m_cpuHandle = handle; }
    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUHandle() const { return m_cpuHandle; }

private:
    IBLManager() = default;
    IBLManager(const IBLManager&) = delete;
    IBLManager& operator=(const IBLManager&) = delete;

    Asset::Texture m_envMap;
    D3D12_GPU_DESCRIPTOR_HANDLE m_gpuHandle = {0};
    D3D12_CPU_DESCRIPTOR_HANDLE m_cpuHandle = {0};
    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_queue;
};
