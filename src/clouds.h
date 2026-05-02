#pragma once
#include "d3d12_helpers.h"
#include <DirectXMath.h>
#include <wrl.h>

struct CloudParams {
  // Primary art controls
  float density;      // Overall density multiplier
  float absorption;   // Extinction multiplier (higher = darker/thicker)
  float coverage;     // [0..1] coverage amount
  float scattering;   // HG g (forward scattering)
  int steps;          // View ray march steps
  float sunIntensity; // Direct light multiplier
  float cloudTop;     // World-space Y
  float cloudBottom;  // World-space Y
  float windSpeed;    // Scroll speed (world units/sec scaled in shader)

  // Shape controls (make it less "blobby")
  float baseScale;         // World->noise scale for base shape
  float detailScale;       // World->noise scale for erosion/detail
  float coverageScale;     // Large-scale coverage modulation (2D-ish)
  float coverageVariation; // How strongly coverage noise modulates threshold
  float erosion;           // [0..1] erosion strength
  float warpStrength;      // Domain-warp strength
  float shapePower;        // Density curve shaping (higher = puffier clumps)
  float powderStrength;    // Multiple scattering / powder approximation
  float cirrusAmount;      // High-altitude wispy cloud layer amount
  float cloudShadowStrength; // Scene-surface shadowing from clouds
  float _pad0;
  float _pad1;

  // Shadowing
  int shadowSteps;      // Light ray steps
  float shadowStepSize; // Step length along sun ray
  float shadowLod;      // LOD for shadow samples

  // Quality/perf controls
  int maxSteps;                 // Hard cap for view march steps
  float verticalStepMeters;     // Target vertical step size (meters)
  int shadowEvery;              // Recompute shadow every N view steps
  float shadowDensityThreshold; // Only shadow-march when density exceeds this

  // Animation
  float timeSeconds;

  // Baked-sky quality controls
  int previewBakeSamples; // Interactive bake samples per texel
  int finalBakeSamples;   // Final/idle bake samples per texel
  float bakeJitterStrength; // Angular supersampling radius in texels
  float multiScatterBoost;  // Extra baked-only multiple scattering fill

  // Baked lighting/art controls
  float silverLiningStrength; // Directional edge brightening around the sun
  float cloudType;            // 0=cumulus, 1=stratus, 2=overcast blend
  float groundBounceStrength; // Warm lower-cloud fill
  float shadowSoftness;       // Shadow/light march smoothing

  DirectX::XMFLOAT3 _pad; // ensure 16-byte boundaries
};

class CloudManager {
public:
  void Initialize(ID3D12Device *device, ID3D12GraphicsCommandList *cmdList);
  void Update(float dt, UINT frameIndex);

  void ResetToDefaults();

  ID3D12Resource *GetBaseTexture() const { return m_baseTexture.Get(); }
  ID3D12Resource *GetDetailTexture() const { return m_detailTexture.Get(); }
  D3D12_GPU_VIRTUAL_ADDRESS GetConstantBufferAddr() const {
    return m_constantBuffers[m_currentFrame]->GetGPUVirtualAddress();
  }
  CloudParams &GetParams() { return m_params; }

  // Request an on-GPU bake of the current clouds (runs a compute pass)
  // - cmdList: graphics/compute command list to record the bake into
  // - cameraCB: optional camera constant buffer (used for origin/light)
  void BakeSky(ID3D12GraphicsCommandList *cmdList, ID3D12Resource *cameraCB = nullptr);
  void RequestBake();
  bool NeedsBake() const { return m_bakeRequested; }
  ID3D12Resource *GetBakedSkyTexture() const;

  // GPU descriptor handle that points to the contiguous CBV+SRV descriptors for
  // clouds
  D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandle() const { return m_gpuHandle; }

private:
  enum class BakeQuality {
    Preview,
    Final,
  };

  enum class BakedSkyTexture {
    Preview,
    Final,
  };

  void CreateTextures(ID3D12Device *device, ID3D12GraphicsCommandList *cmdList);
  void CreateConstantBuffers(ID3D12Device *device);
  void UpdateConstantBuffer();
  void CreateDescriptors(ID3D12Device *device);
  void UpdateBakedSkySRV(ID3D12Device *device, ID3D12Resource *resource);

  Microsoft::WRL::ComPtr<ID3D12Resource> m_baseTexture;
  Microsoft::WRL::ComPtr<ID3D12Resource> m_detailTexture;
  Microsoft::WRL::ComPtr<ID3D12Resource> m_previewBakedSkyTexture; // low-res interactive lat-long bake
  Microsoft::WRL::ComPtr<ID3D12Resource> m_finalBakedSkyTexture;   // full-res idle lat-long bake
  std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> m_uploadBuffers;
  Microsoft::WRL::ComPtr<ID3D12Resource> m_constantBuffers[3];
  UINT m_currentFrame = 0;

  CloudParams m_params;
  UINT8 *m_cbMappedData[3] = {nullptr, nullptr, nullptr};
  bool m_initialized = false;

  // Descriptor handles for binding to raster shaders
  D3D12_CPU_DESCRIPTOR_HANDLE m_cpuHandle = {};
  D3D12_GPU_DESCRIPTOR_HANDLE m_gpuHandle = {};

  // UAV descriptors (for compute bake) - persistent allocation
  D3D12_CPU_DESCRIPTOR_HANDLE m_previewBakedSkyUAVCpuHandle = {};
  D3D12_GPU_DESCRIPTOR_HANDLE m_previewBakedSkyUAVGpuHandle = {};
  D3D12_CPU_DESCRIPTOR_HANDLE m_finalBakedSkyUAVCpuHandle = {};
  D3D12_GPU_DESCRIPTOR_HANDLE m_finalBakedSkyUAVGpuHandle = {};

  // Bake pipeline state / root signature cached here
  Microsoft::WRL::ComPtr<ID3D12RootSignature> m_bakeRootSig;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> m_bakePSO;

  // Bake control
  bool m_bakeRequested = false;
  bool m_bakeInProgress = false;
  UINT m_bakeNextRow = 0;
  bool m_pendingParamBake = false;
  bool m_pendingPreviewBake = false;
  bool m_pendingFinalBake = false;
  float m_secondsSinceParamEdit = 0.0f;
  float m_secondsSinceBake = 0.0f;
  BakeQuality m_requestedBakeQuality = BakeQuality::Final;
  BakedSkyTexture m_activeBakedSkyTexture = BakedSkyTexture::Final;
  CloudParams m_lastObservedParams = {};
  CloudParams m_lastBakedParams = {};
};

// Global instance declared in main.cpp, exposed here for other modules
extern CloudManager g_cloudManager;
