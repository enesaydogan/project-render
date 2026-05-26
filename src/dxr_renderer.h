#pragma once
#include "assets/asset_loader.h"
#include "oidn_denoiser.h"
#include <cstdint>
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
void GetPipelinePresentSize(UINT &width, UINT &height);
// Queue a pipeline rebuild for the render loop. This keeps UI callbacks from
// waiting on GPU idle and PSO creation directly.
void RequestPipelineRecreate(const char *context = nullptr);
bool ConsumePipelineRecreateRequest(std::string *outContext = nullptr);
bool HasPipelineRecreateRequest();
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
// Update the IES profile atlas Texture2DArray. Pass nullptr / 0 to clear.
bool UpdateIESAtlas(const float *data, int sliceCount);
// Reset accumulation for path tracing
void ResetAccumulation();
// Wake the render loop out of final-frame idle without necessarily invalidating
// accumulated samples. Use ResetAccumulation() for camera/material changes that
// require a new sample history.
void RequestInteractiveWake(const char *reason = nullptr);
bool ConsumeInteractiveWake();
bool HasInteractiveWake();
// After a scene load, force a few real DXR frames through before viewport input
// resumes so lazy AS/queue/cloud/reset work is paid at the load boundary.
void RequestSceneLoadWarmup(const char *reason = nullptr);
bool HasSceneLoadWarmup();
bool ConsumeSceneLoadWarmupFrame();
// Scene texture resources/descriptors changed. The next DXR frame must rebuild
// its private shader-visible texture table even if the descriptor range/count
// did not change.
void MarkTextureDescriptorTableDirty();
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

// Path tracer backend selection. Scene value 0 is deprecated and migrates to
// WavefrontOptimized during load.
enum class PathTracingBackend {
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
UINT GetWavefrontContinuationOverflowCount();
UINT GetWavefrontShadowOverflowCount();
UINT GetWavefrontMaterialBinOverflowCount();
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

// Tile-based panorama export: set full panorama dimensions and current tile offset.
// Set fullWidth=0 to disable tiling (default).
void SetExportTileConstants(UINT fullWidth, UINT fullHeight,
                            UINT tileOffsetX, UINT tileOffsetY);

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
// Mark ReGIR grid as dirty so it gets rebuilt on the next frame (call when scene
// geometry or lights change).
void MarkReGIRDirty();
// Runtime toggle for ReGIR enable/disable (default: true).
void SetReGIREnabled(bool enabled);
bool GetReGIREnabled();
struct ReGIRSettings {
  bool enabled = true;
  uint32_t gridRes[3] = {16u, 8u, 16u};
  uint32_t candidatesPerCell = 8u;
  float cellJitterScale = 1.0f;
  float lightReachScale = 1.0f;
  bool debugReadbackEnabled = true;
  uint32_t debugReadbackInterval = 8u;
};
struct ReGIRStats {
  bool enabled = true;
  bool resourcesCreated = false;
  bool sceneBoundsValid = false;
  bool dirty = true;
  bool readbackEnabled = true;
  bool readbackValid = false;
  uint32_t gridRes[3] = {16u, 8u, 16u};
  uint32_t maxGridRes[3] = {32u, 16u, 32u};
  uint32_t candidatesPerCell = 8u;
  uint32_t maxCandidatesPerCell = 8u;
  uint32_t totalCells = 0;
  uint32_t maxTotalCells = 0;
  float gridMin[3] = {};
  float gridMax[3] = {};
  float cellSize[3] = {};
  uint32_t lightCount = 0;
  uint32_t lightBoundCount = 0;
  uint32_t emissiveProxyCount = 0;
  uint32_t skippedDirectionalLights = 0;
  uint32_t skippedZeroPowerLights = 0;
  uint32_t lastUpdateFrameIndex = 0;
  uint32_t frameCounter = 0;
  uint32_t updateCount = 0;
  uint32_t lastDispatchGroups = 0;
  uint32_t fallbackReason = 0;
  uint64_t cellBufferBytes = 0;
  uint64_t activeCellBufferBytes = 0;
  uint64_t lightBoundsBufferBytes = 0;
  uint32_t readbackFrameIndex = 0;
  uint32_t occupiedCells = 0;
  uint32_t emptyCells = 0;
  uint32_t validSlots = 0;
  uint32_t selectedLightSlotCount = 0;
  uint32_t selectedLightIndex = 0xFFFFFFFFu;
  float occupancyPercent = 0.0f;
  float slotFillPercent = 0.0f;
  float avgSlotsPerOccupiedCell = 0.0f;
  float minCandidateWeight = 0.0f;
  float maxCandidateWeight = 0.0f;
  float avgCandidateWeight = 0.0f;
  float minReservoirWeightSum = 0.0f;
  float maxReservoirWeightSum = 0.0f;
  float avgReservoirWeightSum = 0.0f;
  float minInversePdf = 0.0f;
  float maxInversePdf = 0.0f;
  float avgInversePdf = 0.0f;
  float avgCandidateCountM = 0.0f;
  uint32_t maxCandidateCountM = 0;
  // Sampling-side counters from g_shaderCounters (per-frame).
  // Tells you whether sampling actually goes through ReGIR vs falls back.
  uint32_t sampleHits = 0;          // ReGIR pick returned a valid lightIndex
  uint32_t sampleOutOfBounds = 0;   // shading point outside grid AABB
  uint32_t sampleNoCandidate = 0;   // cell had no valid slots
  uint32_t sampleClamped = 0;       // inversePdf hit the domainCap clamp
  uint32_t currentFeatureMask = 0;  // dxrFeatureFlags as bound this frame
};
ReGIRSettings GetReGIRSettings();
void SetReGIRSettings(const ReGIRSettings &settings);
ReGIRStats GetReGIRStats();
void SetReGIRDebugSelectedLight(UINT lightIndex);
const char *GetReGIRFallbackReasonName(uint32_t reason);
// Camera jitter scale applied only when DLSS-RR is active.
// 1.0 = full jitter (best DLSS sampling), 0.0 = disable jitter.
void SetRrJitterScale(float scale);
float GetRrJitterScale();

struct GpuMemoryBreakdown {
  uint64_t renderTargetBytes = 0;
  uint64_t accumulationBytes = 0;
  uint64_t reservoirBytes = 0;
  uint64_t wavefrontQueueBytes = 0;
  uint64_t accelerationStructureBytes = 0;
  uint64_t shaderTableBytes = 0;
  uint64_t lightBufferBytes = 0;
  uint64_t diagnosticBufferBytes = 0;
  uint64_t descriptorHeapBytes = 0;
  uint64_t totalBytes = 0;
  uint64_t wavefrontPathQueueCapacity = 0;
  uint64_t wavefrontHitQueueCapacity = 0;
  uint64_t wavefrontShadowQueueCapacity = 0;
  uint32_t blasCount = 0;
  uint32_t descriptorCount = 0;
  uint32_t wavefrontQueueProfile = 0;
  uint32_t wavefrontQueuePromotionCount = 0;
};
GpuMemoryBreakdown GetGpuMemoryBreakdown();

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

// Copies the latest tonemapped DXR frame into an external preview/export
// target. The caller owns targetState and keeps it in sync with other passes.
bool CopyTonemappedFrameToResource(ID3D12GraphicsCommandList *commandList,
                                   ID3D12Resource *target,
                                   D3D12_RESOURCE_STATES *targetState);

// Tile-based panorama export: read back the current HDR beauty output
// (R16G16B16A16_FLOAT) into a caller-owned CPU buffer.
// outData must be pre-sized to tileWidth * tileHeight * 8 bytes.
bool ReadbackBeautyTile(std::vector<uint8_t> &outData,
                        UINT tileWidth, UINT tileHeight);

// Read back albedo (R16G16B16A16) and normal/roughness (R16G16B16A16)
// guide buffers for the current tile.
bool ReadbackGuideTiles(std::vector<uint8_t> &outAlbedo,
                        std::vector<uint8_t> &outNormal,
                        UINT tileWidth, UINT tileHeight);

// Save a CPU-side RGBA8 full-frame buffer as PNG via WIC.
bool SaveRgba8BufferToPng(const std::wstring &filePath,
                          UINT width, UINT height,
                          const std::vector<uint8_t> &rgbaData);

// CPU OIDN helper for tiled exports. Input and output are tightly packed
// R16G16B16A16_FLOAT buffers with width * 8 bytes per row.
bool DenoiseHostBeautyHalf4(const std::vector<uint8_t> &input,
                            UINT width, UINT height,
                            std::vector<uint8_t> &output);
bool DenoiseHostBeautyGuidedHalf4(const std::vector<uint8_t> &input,
                                  const std::vector<uint8_t> &albedo,
                                  const std::vector<uint8_t> &normal,
                                  UINT width, UINT height,
                                  std::vector<uint8_t> &output);
} // namespace DxrRenderer

extern bool g_rayTracingSupported;
