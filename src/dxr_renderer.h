#pragma once
#include "assets/asset_loader.h"
#include "oidn_denoiser.h"
#include <d3d12.h>
#include <vector>
#include <wrl.h>


// Forward declare Scene types to avoid circular dependency if ever needed
namespace Scene {
struct Instance;
}

using Microsoft::WRL::ComPtr;

class StreamlineManager;

struct GpuLight {
  uint32_t type;
  float position[3];
  float direction[3];
  float intensity;
  float color[3];
  float range;
  float spotAngle;
  float spotInnerAngle;
  uint32_t meshIndex;
  uint32_t padding; // Align to 64 bytes
};

// Declarations for DXR renderer helpers
namespace DxrRenderer {
// Initialize probe (device required). Call this early to detect support.
void Initialize(ID3D12Device *device);
// Attach command queue and synchronization primitives once created
void SetCommandQueue(ID3D12CommandQueue *commandQueue, ID3D12Fence *fence,
                     UINT64 *fenceValues, UINT *frameIndexPtr,
                     HANDLE fenceEvent);
// Build pipeline (compiles shaders and creates state object)
void CreateRayTracingPipeline(UINT width, UINT height);
// Build acceleration structures for given meshes and instances
void BuildAccelerationStructures(const std::vector<Asset::GpuMesh> &meshes,
                                 const std::vector<Scene::Instance> &instances);
// Update light buffer for ReSTIR
void UpdateLights(const std::vector<GpuLight> &lights);
// Reset accumulation for path tracing
void ResetAccumulation();
// Attach Streamline manager (optional) for DLSS-SR / DLSS-RR evaluation.
void SetStreamlineManager(StreamlineManager* streamline);
// Resets Streamline/DLSS temporal history without touching DXR accumulation.
void ResetStreamlineHistory();
// Return true if state object and TLAS/output are ready for rendering
bool IsReady();
// Get current accumulation frame count
UINT GetAccumulationFrameCount();

// Denoiser mode control (Off, OIDN CPU, OIDN GPU)
enum class DenoiserMode { Off = 0, OIDN_CPU = 1, OIDN_GPU = 2 };
void SetDenoiserMode(DenoiserMode m);
DenoiserMode GetDenoiserMode();

void SetOidnQuality(OidnDenoiser::Quality q);
OidnDenoiser::Quality GetOidnQuality();

// Get number of lights transferred to GPU
UINT GetLightCount();
// Camera jitter scale applied only when DLSS-RR is active.
// 1.0 = full jitter (best DLSS sampling), 0.0 = disable jitter.
void SetRrJitterScale(float scale);
float GetRrJitterScale();
// Perform DXR render (dispatch rays, copy to render target). Returns true if
// executed.
bool RenderFrame(ID3D12GraphicsCommandList *commandList, ID3D12CommandAllocator *cmdAlloc, UINT frameIndex,
                 ID3D12Resource *renderTarget,
                 D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
                 ID3D12Resource *cameraCB, ID3D12Resource *materialCB,
                 D3D12_GPU_DESCRIPTOR_HANDLE texturesGpuStart,
                 UINT textureDescriptorCount,
                 const std::vector<Asset::GpuMesh> &meshes,
                 ID3D12Resource *meshDataSB = nullptr);

// Returns the last calculated average noise level (0.0 - 1.0+)
float GetCurrentNoiseLevel();
} // namespace DxrRenderer

extern bool g_rayTracingSupported;