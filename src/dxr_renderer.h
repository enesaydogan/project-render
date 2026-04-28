#pragma once
#include "assets/asset_loader.h"
#include "oidn_denoiser.h"
#include <d3d12.h>
#include <string>
#include <vector>
#include <wrl.h>

#include "light.h"

// Forward declare Scene types to avoid circular dependency if ever needed
namespace Scene {
struct Instance;
}

using Microsoft::WRL::ComPtr;

class StreamlineManager;

// Update light buffer for ReSTIR
namespace DxrRenderer {
// Initialize probe (device required). Call this early to detect support.
void Initialize(ID3D12Device *device);
// Attach command queue and synchronization primitives once created
void SetCommandQueue(ID3D12CommandQueue *commandQueue, ID3D12Fence *fence,
                     UINT64 *fenceValues, UINT *frameIndexPtr,
                     HANDLE fenceEvent);
// Build RayTracing pipeline (TLAS, BLAS, and PSO)
void CreateRayTracingPipeline(UINT width, UINT height);
// Build acceleration structures for given meshes and instances
void BuildAccelerationStructures(
    const std::vector<const Asset::GpuMesh *> &meshes,
    const std::vector<Scene::Instance> &instances);
// Request a full BLAS/TLAS rebuild on the next RenderFrame call.
void RequestAccelerationStructureRebuild();
// Request a TLAS refresh on the next RenderFrame call. If mesh identity is
// unchanged, DXR will refit/update TLAS instead of rebuilding BLAS.
void RequestAccelerationStructureUpdate();
// Mark a material as changed in a way that can affect DXR traversal flags
// (alpha/blend/transmission). The next render frame will rebuild BLAS/TLAS as
// needed.
void MarkMaterialDirty(int materialIndex);
// Update light buffer for ReSTIR. Callers that are already issuing a broader
// renderer invalidation can pass false to avoid duplicate accumulation resets.
void UpdateLights(const std::vector<Light> &lights,
                  bool resetAccumulation = true);
// Reset accumulation for path tracing
void ResetAccumulation();
// Attach Streamline manager (optional) for DLSS-SR / DLSS-RR evaluation.
void SetStreamlineManager(StreamlineManager *streamline);
// Resets Streamline/DLSS temporal history without touching DXR accumulation.
void ResetStreamlineHistory();
// Return true if state object and TLAS/output are ready for rendering
bool IsReady();
// Get current accumulation frame count
UINT GetAccumulationFrameCount();
// Get effective sample count shown to UI (RR uses its own still-frame counter).
UINT GetDisplayedSampleCount();

// Returns true when DXR has reached a stable end condition and the host can
// stop submitting frames until user input or another state change occurs.
bool CanIdleWithoutRendering();

// Profiling functions
void BeginFrameProfiling(ID3D12GraphicsCommandList *commandList);
void EndFrameProfiling(ID3D12GraphicsCommandList *commandList);
float GetFrameTimeMs();
float GetCPUWorkTimeMs();
float GetFPS();
float GetSPPPerSec();
void GetGPUTimes(float &restirTime, float &dispatchTime, float &denoiseTime,
                 float &noiseTime);
float GetGPUFrameTimeMs();

// Shader instrumentation counters (debug)
// outCounters will be filled with up to maxCount uint values (0 when not
// available)
void GetShaderCounters(UINT *outCounters, UINT maxCount);

// Path tracer backend selection. Wavefront modes are scaffolded first and will
// replace the legacy monolithic path as parity lands.
enum class PathTracingBackend {
  Legacy = 0,
  WavefrontParity = 1,
  WavefrontOptimized = 2,
};
enum class WavefrontMaterialBin {
  Diffuse = 0,
  GlossyDielectric = 1,
  Conductor = 2,
  DeltaReflection = 3,
  Refraction = 4,
  Emissive = 5,
  Translucent = 6,
};
void SetPathTracingBackend(PathTracingBackend backend);
PathTracingBackend GetPathTracingBackend();
const char *GetWavefrontStageName();
UINT GetWavefrontBootstrapPathCount();
UINT GetWavefrontBootstrapOverflowCount();
UINT GetWavefrontBootstrapDispatchGroups();
UINT GetWavefrontPrimaryRecordCount();
UINT GetWavefrontPrimaryHitCount();
UINT GetWavefrontPrimaryMissCount();
UINT GetWavefrontResolveRecordCount();
UINT GetWavefrontResolveSurfaceCount();
UINT GetWavefrontResolveDiffuseCount();
UINT GetWavefrontResolveSpecularCount();
UINT GetWavefrontResolveTransmissionCount();
UINT GetWavefrontResolveSkyCount();
UINT GetWavefrontSecondaryPathCount();
UINT GetWavefrontSecondaryDiffuseCount();
UINT GetWavefrontSecondarySpecularCount();
UINT GetWavefrontSecondaryTransmissionCount();
UINT GetWavefrontShadowTaskCount();
UINT GetWavefrontSecondaryVisibilityRecordCount();
UINT GetWavefrontSecondaryVisibilityDiffuseLaneCount();
UINT GetWavefrontSecondaryVisibilitySpecularLaneCount();
UINT GetWavefrontSecondaryVisibilityHitCount();
UINT GetWavefrontSecondaryVisibilityMissCount();
UINT GetWavefrontSecondaryResolveRecordCount();
UINT GetWavefrontSecondaryResolveSurfaceCount();
UINT GetWavefrontSecondaryResolveSkyCount();
UINT GetWavefrontShadowVisibilityTaskCount();
UINT GetWavefrontShadowVisibleCount();
UINT GetWavefrontShadowOccludedCount();
UINT GetWavefrontPrimaryMaterialBinCount(WavefrontMaterialBin materialBin);
UINT GetWavefrontSecondaryMaterialBinCount(WavefrontMaterialBin materialBin);

// Final denoiser mode control (Off, OIDN CPU, OIDN GPU, NVIDIA OptiX).
enum class DenoiserMode { Off = 0, OIDN_CPU = 1, OIDN_GPU = 2, OptiX = 3 };
void SetDenoiserMode(DenoiserMode m);
DenoiserMode GetDenoiserMode();

void SetOidnQuality(OidnDenoiser::Quality q);
OidnDenoiser::Quality GetOidnQuality();

enum class TonemapAmbientOcclusionMode {
  Inward = 0,
  Outward = 1,
  Both = 2,
};
void SetTonemapAmbientOcclusionMode(TonemapAmbientOcclusionMode mode);
TonemapAmbientOcclusionMode GetTonemapAmbientOcclusionMode();
void SetTonemapAmbientOcclusionIntensity(float intensity);
float GetTonemapAmbientOcclusionIntensity();
void SetTonemapAmbientOcclusionLengthMm(float lengthMm);
float GetTonemapAmbientOcclusionLengthMm();

// Get number of lights transferred to GPU
UINT GetLightCount();
// Camera jitter scale applied only when DLSS-RR is active.
// 1.0 = full jitter (best DLSS sampling), 0.0 = disable jitter.
void SetRrJitterScale(float scale);
float GetRrJitterScale();
// Perform DXR render (dispatch rays, copy to render target). Returns true if
// executed.
bool RenderFrame(ID3D12GraphicsCommandList *commandList,
                 ID3D12CommandAllocator *cmdAlloc, UINT frameIndex,
                 ID3D12Resource *renderTarget,
                 D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
                 ID3D12Resource *cameraCB, ID3D12Resource *materialCB,
                 D3D12_GPU_DESCRIPTOR_HANDLE texturesGpuStart,
                 UINT textureDescriptorCount,
                 const std::vector<const Asset::GpuMesh *> &meshes,
                 ID3D12Resource *meshDataSB = nullptr,
                 ID3D12Resource *materialExtraSB = nullptr,
                 UINT presentationX = 0, UINT presentationY = 0,
                 UINT presentationWidth = 0, UINT presentationHeight = 0);

// Submit pending ReSTIR DI/GI compute work on the async compute queue after
// the frame's direct queue work has been submitted.
void SubmitAsyncRestirWork();
// Wait for any in-flight async ReSTIR work to finish before destroying or
// recreating DXR resources.
void WaitForAsyncRestirIdle();

// Returns the last calculated average noise level (0.0 - 1.0+)
float GetCurrentNoiseLevel();
bool HasNoiseEstimate();

// Returns the last calculated average scene luminance in cd/m²
float GetCurrentAvgLuminance();
// Returns the last calculated EV100
float GetCurrentEV100();
// Returns true once the one-shot end-condition denoiser output is available.
bool HasDenoisedOutput();

void SetAutoExposure(bool enable);
bool GetAutoExposure();
void SetExposureCompensation(float comp);
float GetExposureCompensation();
void SetPhysicalCameraExposure(bool enable);
bool GetPhysicalCameraExposure();
void SetPhysicalCameraSettings(float iso, float shutterSeconds,
                               float apertureFNumber);
void GetPhysicalCameraSettings(float &iso, float &shutterSeconds,
                               float &apertureFNumber);
float GetPhysicalCameraEV100();

// Exports the latest tonemapped DXR frame to a PNG file.
// The PNG is lossless (maximum quality by format design).
bool ExportTonemappedFrameToPng(const std::wstring &filePath);
} // namespace DxrRenderer

extern bool g_rayTracingSupported;
