#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>

namespace nrd {
    class Integration;
}
namespace nri {
    struct Device;
    struct CommandBuffer;
}

class NrdDenoiser {
public:
    static NrdDenoiser& Get();

    bool Initialize(ID3D12Device* device, ID3D12CommandQueue* cmdQueue);
    void Shutdown();

    // Recreate NRD integration and wrap resources
    void Recreate(uint32_t width, uint32_t height);

    // Call this each frame before denoising
    void RegisterResource(const char* name, ID3D12Resource* d3d12Resource);

    // Denoise pass
    void Denoise(ID3D12GraphicsCommandList* cmdList,
                 ID3D12Resource* inDiffuseRadianceHitDist,
                 ID3D12Resource* inSpecRadianceHitDist,
                 ID3D12Resource* inViewZ,
                 ID3D12Resource* inNormalRoughness,
                 ID3D12Resource* inMv,
                 ID3D12Resource* outDiffuse,
                 ID3D12Resource* outSpecular,
                 const struct CameraCB& cam,
                 float jitterX, float jitterY,
                 bool resetHistory);

private:
    NrdDenoiser();
    ~NrdDenoiser();

    bool m_initialized = false;
    uint32_t m_width = 0;
    uint32_t m_height = 0;

    ID3D12Device* m_d3dDevice = nullptr;
    ID3D12CommandQueue* m_d3dQueue = nullptr;

    nrd::Integration* m_nrdIntegration = nullptr;
    nri::Device* m_nriDevice = nullptr;
};
