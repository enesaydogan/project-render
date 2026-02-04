#pragma once
#include "d3d12_helpers.h"
#include <DirectXMath.h>
#include <wrl.h>

struct CloudParams {
    // Primary art controls
    float density;        // Overall density multiplier
    float absorption;     // Extinction multiplier (higher = darker/thicker)
    float coverage;       // [0..1] coverage amount
    float scattering;     // HG g (forward scattering)
    int steps;            // View ray march steps
    float sunIntensity;   // Direct light multiplier
    float cloudTop;       // World-space Y
    float cloudBottom;    // World-space Y
    float windSpeed;      // Scroll speed (world units/sec scaled in shader)

    // Shape controls (make it less "blobby")
    float baseScale;          // World->noise scale for base shape
    float detailScale;        // World->noise scale for erosion/detail
    float coverageScale;      // Large-scale coverage modulation (2D-ish)
    float coverageVariation;  // How strongly coverage noise modulates threshold
    float erosion;            // [0..1] erosion strength
    float warpStrength;       // Domain-warp strength
    float shapePower;         // Density curve shaping (higher = puffier clumps)
    float powderStrength;     // Multiple scattering / powder approximation

    // Shadowing
    int shadowSteps;          // Light ray steps
    float shadowStepSize;     // Step length along sun ray
    float shadowLod;          // LOD for shadow samples

    // Quality/perf controls
    int maxSteps;                 // Hard cap for view march steps
    float verticalStepMeters;     // Target vertical step size (meters)
    int shadowEvery;              // Recompute shadow every N view steps
    float shadowDensityThreshold; // Only shadow-march when density exceeds this

    // Animation
    float timeSeconds;

    DirectX::XMFLOAT3 _pad; // ensure 16-byte boundaries
};

class CloudManager {
public:
    void Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);
    void Update(float dt);

    void ResetToDefaults();
    
    ID3D12Resource* GetNoiseTexture() const { return m_noiseTexture.Get(); }
    D3D12_GPU_VIRTUAL_ADDRESS GetConstantBufferAddr() const { return m_constantBuffer->GetGPUVirtualAddress(); }
    CloudParams& GetParams() { return m_params; }

private:
    void CreateNoiseTexture(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);
    void CreateConstantBuffer(ID3D12Device* device);
    void UpdateConstantBuffer();

    Microsoft::WRL::ComPtr<ID3D12Resource> m_noiseTexture;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_uploadBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_constantBuffer;
    
    CloudParams m_params;
    UINT8* m_cbMappedData = nullptr;
    bool m_initialized = false;
};
