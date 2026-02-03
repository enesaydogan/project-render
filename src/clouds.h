#pragma once
#include <d3d12.h>
#include <wrl.h>

// Cloud Manager for Enscape-style Volumetric Clouds
// References:
// - Schneider (2015) "The Real-time Volumetric Cloudscapes of Horizon: Zero Dawn"
// - Hillaire (2020) "Physically Based Sky, Atmosphere and Cloud Rendering in Frostbite"

class CloudManager {
public:
    struct CloudParams {
        float density = 1.0f;
        float absorption = 0.5f;
        float scale = 0.001f;
        float coverage = 0.5f;
        float windSpeed = 10.0f;
    };

    void Initialize(ID3D12Device* device);
    void Render(ID3D12GraphicsCommandList* cmdList);
    void Update(float dt);

    void SetParams(const CloudParams& params) { m_params = params; }

private:
    void GenerateNoiseTextures(); // 3D Perlin-Worley noise

    CloudParams m_params;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_cloudNoiseTexture;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_blueNoiseTexture;
    bool m_initialized = false;
};
