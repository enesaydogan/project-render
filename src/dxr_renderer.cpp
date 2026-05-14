// DXR Renderer Implementation
#include "dxr_renderer.h"
#include "camera.h"
#include "clouds.h" // Access CloudManager
#include "d3d12_helpers.h"
#include "dxc_wrapper.h"
#include "dxr_accumulation.h"
#include "dxr_helpers.h"
#include "grass_manager.h"
#include "ibl_manager.h"
#include "oidn_denoiser.h"
#include "optix_denoiser.h"
#include "raster_renderer.h"
#include "scene.h"
#include "streamline_manager.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <wincodec.h>
#include <wrl.h>

// Expose global debug flag (set by WinMain parsing)
extern bool g_debugLog;

using Microsoft::WRL::ComPtr;

// Access global CBV/SRV descriptor heap so DXR can bind scene textures
extern DescriptorHeapAllocator g_cbvSrvAllocator;
// Globals from main/scene for descriptor bookkeeping
extern D3D12_GPU_DESCRIPTOR_HANDLE g_texturesGpuStart;
extern UINT g_textureDescriptorCount;
extern Microsoft::WRL::ComPtr<ID3D12Device> g_device;
extern CloudManager g_cloudManager; // Global from main.cpp
extern std::vector<Asset::Material> g_loadedMaterials;
extern std::vector<Asset::Texture> g_loadedTextures;

// Module-local state
static ID3D12Device *s_device = nullptr;
static ID3D12CommandQueue *s_commandQueue = nullptr;

static StreamlineManager *s_streamline = nullptr;
static bool s_streamlineResetHistory = true;
static DxrRenderer::PathTracingBackend s_pathTracingBackend =
  DxrRenderer::PathTracingBackend::WavefrontOptimized;
// Final / export denoiser mode & wrapper
static DxrRenderer::DenoiserMode s_denoiserMode =
    DxrRenderer::DenoiserMode::OIDN_GPU;
static OidnDenoiser s_oidnDenoiser;
static OptixDenoiserWrapper s_optixDenoiser;
static OidnDenoiser::Quality s_oidnQuality = OidnDenoiser::Quality::High;
static float s_rrJitterScale = 0.5f;
static bool s_pipelineRecreateRequested = false;
static std::string s_pipelineRecreateContext;

// default off so that users see the raw sky intensity without
// automatic normalization.  The UI checkbox will toggle this at runtime.
static bool s_autoExposure = false;
static float s_exposureCompensation = 1.0f;
static float s_smoothedExposure = 0.02f; // persistent smoothed exposure
static bool s_physicalCameraExposure = true;
static float s_cameraIso = 100.0f;
static float s_cameraShutterSeconds = 1.0f / 125.0f;
static float s_cameraApertureFNumber = 16.0f;
static DxrRenderer::TonemapAmbientOcclusionMode s_tonemapAoMode =
    DxrRenderer::TonemapAmbientOcclusionMode::Both;
static float s_tonemapAoIntensity = 0.0f;
static float s_tonemapAoLengthMm = 250.0f;

// When DLSS-RR is active we don't use the accumulation buffer; track a
// still-frame SPP count separately so maxSPP can still freeze rendering.
static UINT s_rrStillFrameSpp = 0;
static bool s_hasTonemappedFrame = false;
// Cache to detect manual exposure/intensity changes while rendering is frozen
static float s_lastCameraIntensity = -1e30f;
static float s_lastExposureCompensation = 1.0f;
// Exposed for UI/debug (WinMain). Keep external linkage.
unsigned int s_jitterFrameIndex = 0;
float s_lastJitterX = 0.0f;
float s_lastJitterY = 0.0f;

// Profiling state
static ComPtr<ID3D12QueryHeap> s_queryHeap;
static ComPtr<ID3D12Resource> s_queryReadbackBuffer;
static UINT64
    s_queryResults[10]; // For 10 timestamps: frame_start, restir_start,
                        // restir_end, dispatch_end, denoise_start, denoise_end,
                        // noise_start, noise_end, frame_end
static float
    s_gpuTimes[4]; // Times in ms: ReSTIR, DispatchRays, Denoising, Noise
static float s_gpuFrameTimeMs =
    0.0f; // Total GPU frame time (from timestamp 0..9)
static float s_frameTimeMs = 0.0f;
static float s_fps = 0.0f;
static float s_sppPerSec = 0.0f;
static UINT s_lastFrameCount = 0;
static std::chrono::high_resolution_clock::time_point s_lastFrameTime;

// Some debug toggles live in main.cpp; declare them here so we can react to UI
// changes.
extern bool g_dxrHitDebug;
extern bool g_dxrDumpD3D12Messages;
extern bool g_verboseRenderLogs;
static ID3D12Fence *s_fence = nullptr;
static UINT64 *s_fenceValues = nullptr;
static UINT *s_frameIndexPtr = nullptr;
static HANDLE s_fenceEvent = nullptr;

bool g_rayTracingSupported = false; // defined here
static UINT s_grassTlasStartIndex = 0xFFFFFFFFu;

// DXR-specific state kept internal to this module
static ComPtr<ID3D12Device5> s_dxrDevice;
static DxrAccumulation s_accumulation;
static DxrAccumulation s_transmissionAccumulation;

// Descriptor heaps for DXR
static ComPtr<ID3D12DescriptorHeap>
    s_srvHeap; // Holds [Textures(2048), VBs(1024), IBs(1024), OutputUAV(1)]
static D3D12_GPU_DESCRIPTOR_HANDLE s_texTableGpu;
static D3D12_GPU_DESCRIPTOR_HANDLE s_vbTableGpu;
static D3D12_GPU_DESCRIPTOR_HANDLE s_ibTableGpu;
static D3D12_GPU_DESCRIPTOR_HANDLE s_outputUAVGpu;
static D3D12_GPU_DESCRIPTOR_HANDLE s_accumUAVGpu;
static D3D12_GPU_DESCRIPTOR_HANDLE s_varianceUAVGpu;
static D3D12_GPU_DESCRIPTOR_HANDLE s_transmissionAccumUAVGpu;
static D3D12_GPU_DESCRIPTOR_HANDLE s_transmissionVarianceUAVGpu;
static D3D12_GPU_DESCRIPTOR_HANDLE s_reservoirGpuHandle[2];
static D3D12_GPU_DESCRIPTOR_HANDLE s_gi_reservoirGpuHandle[6];
static D3D12_GPU_DESCRIPTOR_HANDLE s_iblGpuHandle;
static D3D12_GPU_DESCRIPTOR_HANDLE s_shaderCountersGpuHandle;
static D3D12_GPU_DESCRIPTOR_HANDLE s_wavefrontShadowContributionGpuHandle;
static Microsoft::WRL::ComPtr<ID3D12Resource> s_shaderCountersBuffer;
static Microsoft::WRL::ComPtr<ID3D12Resource> s_shaderCountersReadbackBuffer;
static Microsoft::WRL::ComPtr<ID3D12Resource>
    s_wavefrontShadowContributionUAV;
static UINT s_lastShaderCounters[16] = {0};
#if defined(_DEBUG)
static constexpr bool kShaderCountersEnabled = true;
#else
static constexpr bool kShaderCountersEnabled = false;
#endif
static bool ShaderCountersEnabled() { return kShaderCountersEnabled; }

struct WavefrontPathStateGpu {
  float origin[3];
  uint32_t pixelIndex;
  float direction[3];
  uint32_t rngState;
  float throughput[3];
  uint32_t packedState;
};
static_assert(sizeof(WavefrontPathStateGpu) == 48,
              "WavefrontPathStateGpu must stay tightly packed.");

struct WavefrontHitRecordGpu {
  float hitT;
  uint32_t pixelIndex;
  uint32_t packedColor0;
  uint32_t packedColor1;
  uint32_t packedNormal;
  uint32_t packedAlbedo;
  uint32_t packedIorType;
  uint32_t packedTransmission;
  uint32_t packedSpecular;
  uint32_t packedState;
  uint32_t reserved;
  float surface[4];
  float guideOrigin[3];
  uint32_t guidePackedState;
  float guideDirection[3];
  float guideHitT;
  uint32_t guidePackedNormal;
  uint32_t guidePackedAlbedo;
  uint32_t guidePackedIorType;
  uint32_t guidePackedTransmission;
  uint32_t guidePackedSpecular;
  uint32_t guideReserved0;
  uint32_t guideReserved1;
  float guideSurface[4];
};
static_assert(sizeof(WavefrontHitRecordGpu) == 136,
              "WavefrontHitRecordGpu must stay tightly packed.");

struct WavefrontShadowTaskGpu {
  float origin[3];
  float maxDistance;
  float direction[3];
  uint32_t packedLightIndex;
  float throughput[3];
  uint32_t packedState;
};
static_assert(sizeof(WavefrontShadowTaskGpu) == 48,
              "WavefrontShadowTaskGpu must stay tightly packed.");

struct WavefrontDispatchArgsGpu {
  uint32_t groupCountX;
  uint32_t groupCountY;
  uint32_t groupCountZ;
  uint32_t activeCount;
};
static_assert(sizeof(WavefrontDispatchArgsGpu) == 16,
              "WavefrontDispatchArgsGpu must stay tightly packed.");

struct WavefrontDispatchRaysRecordGpu {
  D3D12_DISPATCH_RAYS_DESC desc;
  uint32_t padding[2];
};
static_assert(sizeof(WavefrontDispatchRaysRecordGpu) == 112,
              "WavefrontDispatchRaysRecordGpu must match indirect buffer stride.");

static constexpr UINT kWavefrontAbiVersion = 5;
static constexpr UINT kWavefrontPathStateDwords = 12;
static constexpr UINT kWavefrontHitRecordDwords = 34;
static constexpr UINT kWavefrontShadowTaskDwords = 12;
static constexpr UINT kWavefrontDispatchArgsDwords = 4;
static constexpr UINT kWavefrontQueueCounterCount = 16;
static constexpr UINT kWavefrontQueuePathA = 0u;
static constexpr UINT kWavefrontQueuePrimaryActive = 1u;
static constexpr UINT kWavefrontQueuePrimaryHit = 2u;
static constexpr UINT kWavefrontQueuePrimaryMiss = 3u;
static constexpr UINT kWavefrontQueuePathB = 4u;
static constexpr UINT kWavefrontQueueShadow = 5u;
static constexpr UINT kWavefrontStatsCount = 64;
static constexpr UINT kWavefrontDispatchArgCount = 16;
static constexpr UINT kWavefrontReservedUint4Count = 16;
static constexpr UINT64 kWavefrontMinQueueEntries = 65536ull;
static constexpr UINT64 kWavefrontMaxPathQueueEntries = 4194304ull; // 4M
static constexpr UINT64 kWavefrontMaxShadowQueueEntries = 8388608ull; // 8M
static constexpr UINT64 kWavefrontPathQueueMultiplier = 2ull;
static constexpr UINT64 kWavefrontShadowQueueMultiplier = 6ull;
static constexpr UINT kWavefrontSecondaryResolveDispatchArgsIndex = 2;
static constexpr UINT kWavefrontSecondaryDispatchRaysReservedSlot = 0;
static constexpr UINT kWavefrontShadowDispatchRaysReservedSlot = 7;
static constexpr UINT kWavefrontQueueFlagSourceIsA = 0x1u;
static constexpr UINT kWavefrontQueueFlagFilterDiffuse = 0x2u;
static constexpr UINT kWavefrontQueueFlagFilterSpecular = 0x4u;
static constexpr UINT kWavefrontQueueFlagUseMaterialBinList = 0x8u;
static constexpr UINT kWavefrontQueueFlagMissOnly = 0x10u;
static constexpr UINT kWavefrontMaterialBinCounterBase = 6u;
static constexpr UINT kWavefrontQueueFlagMaterialBinShift = 8u;
static constexpr UINT kWavefrontResolveFlagPrimarySurfaceOnly = 0x10000u;
static constexpr UINT kWavefrontResolveFlagDeferAccumulation = 0x80000u;
static constexpr UINT64 kWavefrontCounterStrideBytes = sizeof(UINT);
static constexpr UINT kWavefrontPrimaryMaterialBinStatsBase = 32u;
static constexpr UINT kWavefrontSecondaryMaterialBinStatsBase = 40u;
static constexpr UINT kWavefrontMaterialBinCount = 7u;
static_assert(kWavefrontAbiVersion == 5,
              "Bump shader WAVEFRONT_ABI_VERSION and docs with ABI changes.");
static_assert(sizeof(WavefrontPathStateGpu) / sizeof(uint32_t) ==
                  kWavefrontPathStateDwords,
              "CPU PathState dword count must match the shader ABI.");
static_assert(sizeof(WavefrontHitRecordGpu) / sizeof(uint32_t) ==
                  kWavefrontHitRecordDwords,
              "CPU HitRecord dword count must match the shader ABI.");
static_assert(sizeof(WavefrontShadowTaskGpu) / sizeof(uint32_t) ==
                  kWavefrontShadowTaskDwords,
              "CPU ShadowTask dword count must match the shader ABI.");
static_assert(sizeof(WavefrontDispatchArgsGpu) / sizeof(uint32_t) ==
                  kWavefrontDispatchArgsDwords,
              "CPU DispatchArgs dword count must match the shader ABI.");
static constexpr UINT kWavefrontDispatchRaysRecordStride =
    sizeof(WavefrontDispatchRaysRecordGpu);
static constexpr UINT kWavefrontSecondaryDispatchRaysRecordOffset = 0;
static constexpr UINT kWavefrontShadowDispatchRaysRecordOffset =
    kWavefrontDispatchRaysRecordStride;

// Descriptor counts (tweak to support large models)
static const UINT DXR_HEAP_TEX_COUNT =
    16384; // max textures (increased from 2048)
static const UINT DXR_HEAP_VB_COUNT =
    16384; // vertex buffer SRVs (increased from 4096)
static const UINT DXR_HEAP_IB_COUNT =
    16384; // index buffer SRVs (increased from 4096)
static const UINT DXR_HEAP_TEX_OFFSET = 0;
static const UINT DXR_HEAP_VB_OFFSET = DXR_HEAP_TEX_OFFSET + DXR_HEAP_TEX_COUNT;
static const UINT DXR_HEAP_IB_OFFSET = DXR_HEAP_VB_OFFSET + DXR_HEAP_VB_COUNT;
static const UINT DXR_HEAP_UAV_OFFSET = DXR_HEAP_IB_OFFSET + DXR_HEAP_IB_COUNT;
static const UINT DXR_HEAP_UAV_COUNT = 34; // u0..u33
static const UINT DXR_HEAP_ACCUM_UAV_OFFSET = DXR_HEAP_UAV_OFFSET + 1;
static const UINT DXR_HEAP_RESERVOIR_0_OFFSET = DXR_HEAP_UAV_OFFSET + 2;
static const UINT DXR_HEAP_RESERVOIR_1_OFFSET = DXR_HEAP_UAV_OFFSET + 3;
static const UINT DXR_HEAP_GI_RESERVOIR_0_OFFSET_A = DXR_HEAP_UAV_OFFSET + 4;
static const UINT DXR_HEAP_GI_RESERVOIR_0_OFFSET_B = DXR_HEAP_UAV_OFFSET + 5;
static const UINT DXR_HEAP_GI_RESERVOIR_0_OFFSET_C = DXR_HEAP_UAV_OFFSET + 6;
static const UINT DXR_HEAP_GI_RESERVOIR_1_OFFSET_A = DXR_HEAP_UAV_OFFSET + 7;
static const UINT DXR_HEAP_GI_RESERVOIR_1_OFFSET_B = DXR_HEAP_UAV_OFFSET + 8;
static const UINT DXR_HEAP_GI_RESERVOIR_1_OFFSET_C = DXR_HEAP_UAV_OFFSET + 9;
// Extra UAVs (u10+) reserved for Streamline/DLSS inputs/outputs
static const UINT DXR_HEAP_DEPTH_UAV_OFFSET = DXR_HEAP_UAV_OFFSET + 10;
static const UINT DXR_HEAP_MVEC_UAV_OFFSET = DXR_HEAP_UAV_OFFSET + 11;
static const UINT DXR_HEAP_ALBEDO_UAV_OFFSET = DXR_HEAP_UAV_OFFSET + 12;
static const UINT DXR_HEAP_NORMAL_ROUGHNESS_UAV_OFFSET =
    DXR_HEAP_UAV_OFFSET + 13;
static const UINT DXR_HEAP_DLSS_OUT_UAV_OFFSET = DXR_HEAP_UAV_OFFSET + 14;
static const UINT DXR_HEAP_LINEAR_DEPTH_UAV_OFFSET =
    DXR_HEAP_UAV_OFFSET + 15;
static const UINT DXR_HEAP_SPEC_ALBEDO_OFFSET = DXR_HEAP_UAV_OFFSET + 16;
static const UINT DXR_HEAP_SPEC_HITDIST_OFFSET = DXR_HEAP_UAV_OFFSET + 17;
static const UINT DXR_HEAP_SPEC_MVEC_OFFSET = DXR_HEAP_UAV_OFFSET + 18;
static const UINT DXR_HEAP_TRANSMISSION_ACCUM_OFFSET =
    DXR_HEAP_UAV_OFFSET + 19;
static const UINT DXR_HEAP_TRANSMISSION_VARIANCE_OFFSET =
    DXR_HEAP_UAV_OFFSET + 20;
static const UINT DXR_HEAP_OIDN_OUT_UAV_OFFSET = DXR_HEAP_UAV_OFFSET + 21;
static const UINT DXR_HEAP_VARIANCE_UAV_OFFSET = DXR_HEAP_UAV_OFFSET + 22;
static const UINT DXR_HEAP_WAVEFRONT_SHADOW_CONTRIB_OFFSET =
    DXR_HEAP_UAV_OFFSET + 23;
// Extra debug UAV: shader counters (readback) at u24
static const UINT DXR_HEAP_SHADER_COUNTERS_OFFSET = DXR_HEAP_UAV_OFFSET + 24;
static const UINT DXR_HEAP_WAVEFRONT_COUNTERS_OFFSET =
  DXR_HEAP_UAV_OFFSET + 25;
static const UINT DXR_HEAP_WAVEFRONT_PATH_A_OFFSET = DXR_HEAP_UAV_OFFSET + 26;
static const UINT DXR_HEAP_WAVEFRONT_PATH_B_OFFSET = DXR_HEAP_UAV_OFFSET + 27;
static const UINT DXR_HEAP_WAVEFRONT_HIT_OFFSET = DXR_HEAP_UAV_OFFSET + 28;
static const UINT DXR_HEAP_WAVEFRONT_SHADOW_OFFSET = DXR_HEAP_UAV_OFFSET + 29;
static const UINT DXR_HEAP_WAVEFRONT_DISPATCH_ARGS_OFFSET =
  DXR_HEAP_UAV_OFFSET + 30;
static const UINT DXR_HEAP_WAVEFRONT_STATS_OFFSET = DXR_HEAP_UAV_OFFSET + 31;
static const UINT DXR_HEAP_WAVEFRONT_RESERVED_OFFSET =
  DXR_HEAP_UAV_OFFSET + 32;
static const UINT DXR_HEAP_WAVEFRONT_BIN_INDICES_OFFSET =
  DXR_HEAP_UAV_OFFSET + 33;

// Dedicated SRV blocks after UAV range so UAV registers stay stable.
static const UINT DXR_HEAP_ENV_SRV_OFFSET =
    DXR_HEAP_UAV_OFFSET + DXR_HEAP_UAV_COUNT;
static const UINT DXR_HEAP_ENV_SRV_COUNT = 3; // t0..t2, space1
static const UINT DXR_HEAP_IBL_OFFSET = DXR_HEAP_ENV_SRV_OFFSET + 0;
static const UINT DXR_HEAP_IBL_CONDITIONAL_CDF_OFFSET =
    DXR_HEAP_ENV_SRV_OFFSET + 1;
static const UINT DXR_HEAP_IBL_MARGINAL_CDF_OFFSET =
    DXR_HEAP_ENV_SRV_OFFSET + 2;

// Cloud SRVs (t10..t12, space2) - must be contiguous for the cloud table.
static const UINT DXR_HEAP_CLOUD_SRV_OFFSET =
    DXR_HEAP_ENV_SRV_OFFSET + DXR_HEAP_ENV_SRV_COUNT;
static const UINT DXR_HEAP_CLOUD_SRV_COUNT = 3;
static const UINT DXR_HEAP_CLOUD_TEX_OFFSET = DXR_HEAP_CLOUD_SRV_OFFSET + 0;
static const UINT DXR_HEAP_CLOUD_DETAIL_TEX_OFFSET =
    DXR_HEAP_CLOUD_SRV_OFFSET + 1;
static const UINT DXR_HEAP_CLOUD_BAKED_TEX_OFFSET =
    DXR_HEAP_CLOUD_SRV_OFFSET + 2;

static const UINT DXR_HEAP_TOTAL_COUNT =
    DXR_HEAP_TEX_COUNT + DXR_HEAP_VB_COUNT + DXR_HEAP_IB_COUNT +
    DXR_HEAP_UAV_COUNT + DXR_HEAP_ENV_SRV_COUNT + DXR_HEAP_CLOUD_SRV_COUNT;

// Output texture dimensions used by DXR (kept local to module)
static UINT s_outputWidth = 1280;
static UINT s_outputHeight = 720;
// Output (swapchain) dimensions last requested by the host
static UINT s_presentWidth = 1280;
static UINT s_presentHeight = 720;
enum ResourceFeatureBits : uint32_t {
  ResourceFeature_Dlss = 1u << 0,
  ResourceFeature_DlssRayReconstruction = 1u << 1,
  ResourceFeature_FinalDenoiser = 1u << 2,
  ResourceFeature_TonemapAo = 1u << 3,
};

enum DxrFeatureBits : uint32_t {
  DxrFeature_AovOutput = 1u << 0,
  DxrFeature_PrimaryGuide = 1u << 1,
};

// Halton sequence helper for CPU-side jitter
static float Halton(uint32_t index, uint32_t base) {
  float f = 1.0f;
  float r = 0.0f;
  while (index > 0) {
    f /= (float)base;
    r += f * (float)(index % base);
    index /= base;
  }
  return r;
}

static bool IsDlssActive() {
  return s_streamline && s_streamline->IsInitialized() &&
         s_streamline->IsDeviceSet() && s_streamline->IsEnabled() &&
         s_streamline->GetMode() != StreamlineManager::Mode::Off;
}

static bool IsDlssRayReconstructionActive() {
  return IsDlssActive() &&
         s_streamline->GetMode() ==
             StreamlineManager::Mode::DLSS_RayReconstruction;
}

static uint32_t ComputeResourceFeatureMask() {
  uint32_t mask = 0;
  if (IsDlssActive()) {
    mask |= ResourceFeature_Dlss;
  }
  if (IsDlssRayReconstructionActive()) {
    mask |= ResourceFeature_DlssRayReconstruction;
  }
  if (s_denoiserMode != DxrRenderer::DenoiserMode::Off && !IsDlssActive()) {
    mask |= ResourceFeature_FinalDenoiser;
  }
  if (s_tonemapAoIntensity > 1.0e-4f) {
    mask |= ResourceFeature_TonemapAo;
  }
  return mask;
}

static uint32_t ComputeDxrFeatureMask(bool dlssActive, bool rrActive,
                                      bool debugViewActive,
                                      bool finalDenoiserActive) {
  uint32_t mask = 0;
  if (dlssActive || finalDenoiserActive || debugViewActive ||
      s_tonemapAoIntensity > 1.0e-4f) {
    mask |= DxrFeature_AovOutput;
  }
  if (dlssActive || rrActive || s_tonemapAoIntensity > 1.0e-4f) {
    mask |= DxrFeature_PrimaryGuide;
  }
  return mask;
}

static UINT64 ComputeWavefrontQueueCapacity(UINT width, UINT height,
                                            UINT64 maxEntries,
                                            UINT64 multiplier = 1ull) {
  const UINT64 pixelCount =
      std::max<UINT64>(1ull, static_cast<UINT64>(width) * height);
  const UINT64 requested = pixelCount * std::max<UINT64>(1ull, multiplier);
  return std::clamp(requested, kWavefrontMinQueueEntries, maxEntries);
}

static bool NeedsDepthAndMotionBuffers(uint32_t mask) {
  // The path tracer and ReSTIR passes write/read depth every frame, even when
  // DLSS is disabled. Keep these UAVs resident so wavefront/native-resolution
  // rendering does not fall back to null descriptors on the RR-off path.
  (void)mask;
  return true;
}

static bool NeedsSurfaceDataBuffers(uint32_t mask) {
  // Normal/roughness and albedo are also written unconditionally by the DXR
  // path and consumed by post-path passes beyond DLSS itself.
  (void)mask;
  return true;
}

static bool NeedsLinearDepthBuffer(uint32_t mask) {
  return (mask & ResourceFeature_TonemapAo) != 0;
}

static bool NeedsSpecularAuxBuffers(uint32_t mask) {
  return (mask & ResourceFeature_DlssRayReconstruction) != 0;
}

static bool NeedsDlssOutputBuffer(uint32_t mask) {
  return (mask & ResourceFeature_Dlss) != 0;
}

static bool NeedsOidnOutputBuffer(uint32_t mask) {
  return (mask & ResourceFeature_FinalDenoiser) != 0;
}

inline void TransitionResource(ID3D12GraphicsCommandList *cmdList,
                               ID3D12Resource *resource,
                               D3D12_RESOURCE_STATES before,
                               D3D12_RESOURCE_STATES after) {
  if (!cmdList || !resource)
    return;
  if (before == after)
    return;
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  barrier.Transition.pResource = resource;
  barrier.Transition.StateBefore = before;
  barrier.Transition.StateAfter = after;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  cmdList->ResourceBarrier(1, &barrier);
}

static void DumpD3D12InfoQueueMessages(const char *contextTag) {
  if (!g_dxrDumpD3D12Messages || !s_device) {
    return;
  }
  ComPtr<ID3D12InfoQueue> infoQueue;
  if (FAILED(s_device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
    return;
  }
  const UINT64 n = infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();
  for (UINT64 i = 0; i < n; ++i) {
    SIZE_T messageLength = 0;
    infoQueue->GetMessage(i, nullptr, &messageLength);
    std::vector<char> message(messageLength);
    D3D12_MESSAGE *pMsg = reinterpret_cast<D3D12_MESSAGE *>(message.data());
    if (SUCCEEDED(infoQueue->GetMessage(i, pMsg, &messageLength))) {
      fprintf(stderr, "D3D12 INFO (%s): Category=%d Severity=%d ID=%d: %s\n",
              contextTag, (int)pMsg->Category, (int)pMsg->Severity,
              (int)pMsg->ID, pMsg->pDescription);
    }
  }
}
static DxcHelper s_dxcHelper;
static ComPtr<ID3D12StateObject> s_rtStateObject;
static ComPtr<ID3D12Resource> s_sbtStorage;
static ComPtr<ID3D12RootSignature> s_rtGlobalRootSignature;
static ComPtr<ID3D12Resource> s_outputUAV;
static ComPtr<ID3D12Resource> s_depthUAV;
static ComPtr<ID3D12Resource> s_mvecUAV;
static ComPtr<ID3D12Resource> s_albedoUAV;
static ComPtr<ID3D12Resource> s_normalRoughnessUAV;
static ComPtr<ID3D12Resource> s_linearDepthUAV;
static ComPtr<ID3D12Resource> s_dlssOutputUAV;
static ComPtr<ID3D12Resource> s_oidnOutputUAV;
static ComPtr<ID3D12Resource> s_tonemapOutputUAV;
static ComPtr<ID3D12Resource> s_specularAlbedoUAV;
static ComPtr<ID3D12Resource> s_specHitDistanceUAV;
static ComPtr<ID3D12Resource> s_specularMotionVectorsUAV;

static bool PrepareSelectedFinalDenoiserResources() {
  if (!s_device || !s_outputUAV || !s_oidnOutputUAV ||
      s_denoiserMode == DxrRenderer::DenoiserMode::Off || IsDlssActive()) {
    return false;
  }

  if (s_denoiserMode == DxrRenderer::DenoiserMode::OIDN_CPU ||
      s_denoiserMode == DxrRenderer::DenoiserMode::OIDN_GPU) {
    s_oidnDenoiser.SetQuality(s_oidnQuality);
    if (!s_oidnDenoiser.Initialize(s_device)) {
      return false;
    }
    return s_oidnDenoiser.Prepare(s_outputUAV.Get(), s_albedoUAV.Get(),
                                  s_normalRoughnessUAV.Get(),
                                  s_oidnOutputUAV.Get());
  }

  if (s_denoiserMode == DxrRenderer::DenoiserMode::OptiX) {
    if (!s_optixDenoiser.Initialize(s_device)) {
      return false;
    }
    return s_optixDenoiser.Prepare(s_outputUAV.Get(), s_albedoUAV.Get(),
                                   s_normalRoughnessUAV.Get(),
                                   s_oidnOutputUAV.Get());
  }

  return false;
}

static UINT s_outputUAVDescriptorSize = 0;
static D3D12_GPU_DESCRIPTOR_HANDLE s_outputUAVGpuHandle = {0};
static ComPtr<ID3D12DescriptorHeap>
    s_uavHeap; // fallback heap when global heap not available
static ComPtr<ID3D12DescriptorHeap>
    s_mergedHeap; // merged heap that contains scene SRVs then output UAV
                  // (preferred)

// Tonemap compute pipeline resources (linear HDR -> swapchain format)
static ComPtr<ID3D12RootSignature> s_tonemapRootSig;
static ComPtr<ID3D12PipelineState> s_tonemapPSO;
static ComPtr<ID3D12Resource> s_tonemapCB;
static ComPtr<ID3D12DescriptorHeap> s_tonemapHeap;

struct ShaderTableEntry {
  void *id;
};
static UINT64 s_wavefrontRayGenShaderTableEntrySize = 0;
static UINT64 s_shaderTableEntrySize = 0;
static D3D12_GPU_VIRTUAL_ADDRESS s_wavefrontPrimaryRayGenShaderTable = 0;
static D3D12_GPU_VIRTUAL_ADDRESS s_wavefrontSecondaryRayGenShaderTable = 0;
static D3D12_GPU_VIRTUAL_ADDRESS s_wavefrontShadowRayGenShaderTable = 0;
static D3D12_GPU_VIRTUAL_ADDRESS s_missShaderTable = 0;
static D3D12_GPU_VIRTUAL_ADDRESS s_wavefrontHitGroupShaderTable = 0;
static D3D12_GPU_VIRTUAL_ADDRESS s_uploadedWavefrontSecondaryRayGen = 0;
static D3D12_GPU_VIRTUAL_ADDRESS s_uploadedWavefrontShadowRayGen = 0;

struct MeshBLAS {
  AccelerationStructureBuffers buffers;
  UINT64 meshId;
};
static std::vector<MeshBLAS> s_allBLAS;
static AccelerationStructureBuffers s_tlas;
static std::vector<ID3D12Resource *> s_cachedMeshBuffersForBlas;
static std::vector<uint8_t> s_cachedMeshOpaqueForBlas;
static std::vector<uint8_t> s_dirtyMaterialFlags;
static std::vector<const Asset::GpuMesh *> s_cachedTlasMeshOrder;
static bool s_tlasSupportsUpdate = false;
static bool s_forceAsRebuild = false;
static bool s_forceTlasUpdate = false;
static bool s_hasGrassTlasCameraPos = false;
static DirectX::XMFLOAT3 s_lastGrassTlasCameraPos = {};
static bool s_hasGrassCameraMotionSample = false;
static DirectX::XMFLOAT3 s_lastObservedGrassCameraPos = {};
static uint32_t s_resourceFeatureMask = 0;

// Async compute execution for decoupled ReSTIR DI/GI.
static ComPtr<ID3D12CommandQueue> s_asyncComputeQueue;
static ComPtr<ID3D12Fence> s_asyncDirectFence;
static ComPtr<ID3D12Fence> s_asyncComputeFence;
static ComPtr<ID3D12CommandAllocator> s_asyncComputeAllocator;
static ComPtr<ID3D12GraphicsCommandList4> s_asyncComputeList;
static HANDLE s_asyncComputeFenceEvent = nullptr;
static UINT64 s_asyncDirectFenceValue = 1;
static UINT64 s_asyncComputeFenceValue = 1;
static UINT64 s_asyncComputePendingFenceWait = 0;
static bool s_asyncRestirPending = false;
static bool s_asyncRestirAvailable = false;
static bool s_asyncRestirInitTried = false;
static ComPtr<ID3D12Resource> s_asyncRestirCameraCB;

enum class TextureStreamingPolicy { FullRes = 0, Balanced = 1, Aggressive = 2 };
static TextureStreamingPolicy s_textureStreamingPolicy =
    TextureStreamingPolicy::FullRes;
static TextureStreamingPolicy s_lastAppliedTextureStreamingPolicy =
    TextureStreamingPolicy::FullRes;
static bool s_textureStreamingAuto = false;
static bool s_textureTableDirty = true;

static ComPtr<ID3D12Resource> s_lightBuffer;
static UINT s_lightCount = 0;
static std::vector<Light> s_lastLightsCpu;
static ComPtr<ID3D12Resource> s_reservoirBuffers[2];
static ComPtr<ID3D12Resource> s_gi_reservoirBuffers[6];
static ComPtr<ID3D12RootSignature> s_restirSpatialRootSig;
static ComPtr<ID3D12PipelineState> s_restirSpatialPSO;
static ComPtr<ID3D12RootSignature> s_restirGiSpatialRootSig;
static ComPtr<ID3D12PipelineState> s_restirGiSpatialPSO;

// Noise Statistics Resources
static ComPtr<ID3D12RootSignature> s_noiseStatsRootSig;
static ComPtr<ID3D12PipelineState> s_noiseStatsPSO;
static ComPtr<ID3D12Resource> s_noiseStatsCB;
static ComPtr<ID3D12Resource> s_noiseStatsOutputBuffer;
static ComPtr<ID3D12Resource> s_noiseStatsReadbackBuffer;
static ComPtr<ID3D12DescriptorHeap> s_noiseStatsHeap;
static UINT s_noiseStatsCapacity =
    0; // number of floats currently allocated in output buffer
static float s_lastNoiseLevel = 0.0f;
static bool s_hasNoiseEstimate = false;
static UINT64 s_noiseStatsDispatchCount = 0;

// Average Luminance Resources
static ComPtr<ID3D12RootSignature> s_avgLumRootSig;
static ComPtr<ID3D12PipelineState> s_avgLumPSO;
static ComPtr<ID3D12Resource> s_avgLumCB;
static ComPtr<ID3D12Resource> s_avgLumBuffer;
static ComPtr<ID3D12Resource> s_avgLumReadbackBuffer;
static ComPtr<ID3D12DescriptorHeap> s_avgLumHeap;
static UINT s_avgLumCapacity = 0;
static float s_avgLuminanceCdM2 = 0.0f;
static float s_lastEV100 = 0.0f;

static ComPtr<ID3D12Resource> s_wavefrontQueueCountersBuffer;
static ComPtr<ID3D12Resource> s_wavefrontPathQueueABuffer;
static ComPtr<ID3D12Resource> s_wavefrontPathQueueBBuffer;
static ComPtr<ID3D12Resource> s_wavefrontHitQueueBuffer;
static ComPtr<ID3D12Resource> s_wavefrontShadowQueueBuffer;
static ComPtr<ID3D12Resource> s_wavefrontDispatchArgsBuffer;
static ComPtr<ID3D12Resource> s_wavefrontStatsBuffer;
static ComPtr<ID3D12Resource> s_wavefrontStatsReadbackBuffer;
static ComPtr<ID3D12Resource> s_wavefrontReservedBuffer;
static ComPtr<ID3D12Resource> s_wavefrontMaterialBinIndicesBuffer;
static UINT64 s_wavefrontPathQueueCapacity = 0;
static UINT64 s_wavefrontHitQueueCapacity = 0;
static UINT64 s_wavefrontShadowQueueCapacity = 0;
static UINT s_lastWavefrontBootstrapPathCount = 0;
static UINT s_lastWavefrontBootstrapOverflowCount = 0;
static UINT s_lastWavefrontContinuationOverflowCount = 0;
static UINT s_lastWavefrontShadowOverflowCount = 0;
static UINT s_lastWavefrontMaterialBinOverflowCount = 0;
static UINT s_lastWavefrontBootstrapDispatchGroups = 0;
static UINT s_lastWavefrontPrimaryHitCount = 0;
static UINT s_lastWavefrontPrimaryMissCount = 0;
static UINT s_lastWavefrontPrimaryRecordCount = 0;
static UINT s_lastWavefrontResolveRecordCount = 0;
static UINT s_lastWavefrontResolveSurfaceCount = 0;
static UINT s_lastWavefrontResolveDiffuseCount = 0;
static UINT s_lastWavefrontResolveSpecularCount = 0;
static UINT s_lastWavefrontResolveTransmissionCount = 0;
static UINT s_lastWavefrontResolveSkyCount = 0;
static UINT s_lastWavefrontSecondaryPathCount = 0;
static UINT s_lastWavefrontSecondaryDiffuseCount = 0;
static UINT s_lastWavefrontSecondarySpecularCount = 0;
static UINT s_lastWavefrontSecondaryTransmissionCount = 0;
static UINT s_lastWavefrontShadowTaskCount = 0;
static UINT s_lastWavefrontSecondaryVisibilityRecordCount = 0;
static UINT s_lastWavefrontSecondaryVisibilityHitCount = 0;
static UINT s_lastWavefrontSecondaryVisibilityMissCount = 0;
static UINT s_lastWavefrontSecondaryResolveRecordCount = 0;
static UINT s_lastWavefrontSecondaryResolveSurfaceCount = 0;
static UINT s_lastWavefrontSecondaryResolveSkyCount = 0;
static UINT s_lastWavefrontShadowVisibilityTaskCount = 0;
static UINT s_lastWavefrontShadowVisibleCount = 0;
static UINT s_lastWavefrontShadowOccludedCount = 0;
static UINT s_lastWavefrontStats[64] = {};
static const char *s_wavefrontStageName = "idle";

static void SetWavefrontStage(const char *stageName) {
  s_wavefrontStageName = stageName ? stageName : "unknown";
  if (g_verboseRenderLogs) {
    fprintf(stderr, "DxrRenderer: Wavefront stage: %s\n", s_wavefrontStageName);
  }
}

static bool s_noiseConvergedLatched = false;
static bool s_cloudDescriptorsDone = false;
static bool s_hasDenoised = false;
static int s_lastRenderFrameFailReason = -1;

enum class FinalDisplayState {
  Rendering,
  FinalDenoisePending,
  DisplayingFinal,
  WakePending,
};
static FinalDisplayState s_finalDisplayState = FinalDisplayState::Rendering;
static bool s_interactiveWakeRequested = false;
static UINT s_interactiveWakeFrameBudget = 0;
static UINT s_sceneLoadWarmupFramesRemaining = 0;
static std::string s_interactiveWakeReason;
static constexpr UINT kInteractiveWakeFrameBudget = 2;
static constexpr UINT kSceneLoadWarmupFrameBudget = 4;

static void QueueInteractiveWake(const char *reason) {
  s_interactiveWakeRequested = true;
  s_interactiveWakeFrameBudget =
      (std::max)(s_interactiveWakeFrameBudget, kInteractiveWakeFrameBudget);
  s_interactiveWakeReason = reason ? reason : "interactive change";
  s_finalDisplayState = FinalDisplayState::WakePending;
}

static ComPtr<ID3D12RootSignature> s_wavefrontBootstrapRootSig;
static ComPtr<ID3D12PipelineState> s_wavefrontBootstrapPSO;
static ComPtr<ID3D12RootSignature> s_wavefrontCounterResetRootSig;
static ComPtr<ID3D12PipelineState> s_wavefrontCounterResetPSO;
static ComPtr<ID3D12RootSignature> s_wavefrontPrepareIndirectArgsRootSig;
static ComPtr<ID3D12PipelineState> s_wavefrontPrepareIndirectArgsPSO;
static ComPtr<ID3D12RootSignature> s_wavefrontResolveRootSig;
static ComPtr<ID3D12PipelineState> s_wavefrontResolvePSO;
static ComPtr<ID3D12PipelineState> s_wavefrontRestirSeedPSO;
static ComPtr<ID3D12PipelineState> s_wavefrontSecondaryResolvePSO;
static ComPtr<ID3D12PipelineState> s_wavefrontShadowIntegratePSO;
static ComPtr<ID3D12PipelineState> s_wavefrontAccumulatePSO;
static ComPtr<ID3D12CommandSignature> s_wavefrontDispatchCommandSignature;
static ComPtr<ID3D12CommandSignature> s_wavefrontDispatchRaysCommandSignature;
static ComPtr<ID3D12Resource> s_wavefrontIndirectDispatchUploadBuffer;

static bool SaveRgba8ToPngWic(const std::wstring &filePath, UINT width,
                              UINT height, const uint8_t *pixels,
                              UINT rowStride) {
  if (filePath.empty() || width == 0 || height == 0 || !pixels ||
      rowStride == 0) {
    return false;
  }

  HRESULT hrInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool needsCoUninit = SUCCEEDED(hrInit);
  if (FAILED(hrInit) && hrInit != RPC_E_CHANGED_MODE) {
    return false;
  }

  bool success = false;
  do {
    auto LogFail = [&](const char *step, HRESULT hr) {
      fprintf(stderr, "DxrRenderer: PNG export failed at %s (hr=0x%08x)\n",
              step, (unsigned)hr);
    };

    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                  CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
      LogFail("CoCreateInstance(IWICImagingFactory)", hr);
      break;
    }

    ComPtr<IWICStream> stream;
    hr = factory->CreateStream(&stream);
    if (FAILED(hr)) {
      LogFail("CreateStream", hr);
      break;
    }

    hr = stream->InitializeFromFilename(filePath.c_str(), GENERIC_WRITE);
    if (FAILED(hr)) {
      LogFail("InitializeFromFilename", hr);
      break;
    }

    ComPtr<IWICBitmapEncoder> encoder;
    hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    if (FAILED(hr)) {
      LogFail("CreateEncoder(PNG)", hr);
      break;
    }

    hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) {
      LogFail("Encoder::Initialize", hr);
      break;
    }

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> props;
    hr = encoder->CreateNewFrame(&frame, &props);
    if (FAILED(hr)) {
      LogFail("CreateNewFrame", hr);
      break;
    }

    hr = frame->Initialize(props.Get());
    if (FAILED(hr)) {
      LogFail("Frame::Initialize", hr);
      break;
    }

    hr = frame->SetSize(width, height);
    if (FAILED(hr)) {
      LogFail("Frame::SetSize", hr);
      break;
    }

    // Use BGRA for widest encoder compatibility.
    WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
    hr = frame->SetPixelFormat(&pixelFormat);
    if (FAILED(hr)) {
      LogFail("Frame::SetPixelFormat", hr);
      break;
    }

    if (!IsEqualGUID(pixelFormat, GUID_WICPixelFormat32bppBGRA) &&
        !IsEqualGUID(pixelFormat, GUID_WICPixelFormat32bppRGBA)) {
      fprintf(stderr,
              "DxrRenderer: PNG export failed: unexpected pixel format after "
              "SetPixelFormat.\n");
      break;
    }

    std::vector<uint8_t> writeBuffer;
    const uint8_t *writePixels = pixels;
    UINT writeRowStride = rowStride;
    if (IsEqualGUID(pixelFormat, GUID_WICPixelFormat32bppBGRA)) {
      writeBuffer.resize((size_t)width * (size_t)height * 4u);
      for (UINT y = 0; y < height; ++y) {
        const uint8_t *src = pixels + (size_t)y * rowStride;
        uint8_t *dst = writeBuffer.data() + (size_t)y * (size_t)width * 4u;
        for (UINT x = 0; x < width; ++x) {
          dst[x * 4 + 0] = src[x * 4 + 2];
          dst[x * 4 + 1] = src[x * 4 + 1];
          dst[x * 4 + 2] = src[x * 4 + 0];
          dst[x * 4 + 3] = src[x * 4 + 3];
        }
      }
      writePixels = writeBuffer.data();
      writeRowStride = width * 4u;
    }

    const UINT imageBytes = writeRowStride * height;
    hr = frame->WritePixels(height, writeRowStride, imageBytes,
                            const_cast<BYTE *>(writePixels));
    if (FAILED(hr)) {
      LogFail("Frame::WritePixels", hr);
      break;
    }

    hr = frame->Commit();
    if (FAILED(hr)) {
      LogFail("Frame::Commit", hr);
      break;
    }

    hr = encoder->Commit();
    if (FAILED(hr)) {
      LogFail("Encoder::Commit", hr);
      break;
    }

    success = true;
  } while (false);

  if (needsCoUninit) {
    CoUninitialize();
  }
  return success;
}

namespace DxrRenderer {

struct NoiseStatsConstants {
  uint32_t width;
  uint32_t height;
  float padding[2];
};

float GetCurrentNoiseLevel() { return s_lastNoiseLevel; }
bool HasNoiseEstimate() { return s_hasNoiseEstimate; }
float GetCurrentAvgLuminance() { return s_avgLuminanceCdM2; }
float GetCurrentEV100() { return s_lastEV100; }
bool HasDenoisedOutput() { return s_hasDenoised; }

void SetAutoExposure(bool enable) { s_autoExposure = enable; }
bool GetAutoExposure() { return s_autoExposure; }
void SetTonemapAmbientOcclusionMode(TonemapAmbientOcclusionMode mode) {
  if (s_tonemapAoMode != mode) {
    s_tonemapAoMode = mode;
    g_cameraData.tonemapAoMode = static_cast<float>(static_cast<int>(mode));
    ResetAccumulation();
    s_hasTonemappedFrame = false;
  }
}
TonemapAmbientOcclusionMode GetTonemapAmbientOcclusionMode() {
  return s_tonemapAoMode;
}
void SetTonemapAmbientOcclusionIntensity(float intensity) {
  const float clamped = (std::clamp)(intensity, 0.0f, 4.0f);
  if (std::abs(s_tonemapAoIntensity - clamped) > 1.0e-6f) {
    s_tonemapAoIntensity = clamped;
    g_cameraData.tonemapAoIntensity = clamped;
    ResetAccumulation();
    s_hasTonemappedFrame = false;
  }
}
float GetTonemapAmbientOcclusionIntensity() { return s_tonemapAoIntensity; }
void SetTonemapAmbientOcclusionLengthMm(float lengthMm) {
  const float clamped = (std::clamp)(lengthMm, 0.0f, 5000.0f);
  if (std::abs(s_tonemapAoLengthMm - clamped) > 1.0e-6f) {
    s_tonemapAoLengthMm = clamped;
    // Use standard mm-to-m conversion for world radius: 1 mm = 0.001 m.
    // If this still feels too large, adjust slider range / effective ray scale.
    g_cameraData.tonemapAoRadiusMeters = clamped * 0.001f;
    ResetAccumulation();
    s_hasTonemappedFrame = false;
  }
}
float GetTonemapAmbientOcclusionLengthMm() { return s_tonemapAoLengthMm; }
void SetExposureCompensation(float comp) {
  if (s_exposureCompensation != comp) {
    s_exposureCompensation = comp;
    s_hasTonemappedFrame = false; // ensure tonemap re-applies new compensation
    s_lastExposureCompensation = comp;
  }
}
float GetExposureCompensation() { return s_exposureCompensation; }
void SetPhysicalCameraExposure(bool enable) {
  s_physicalCameraExposure = enable;
  s_hasTonemappedFrame = false;
}
bool GetPhysicalCameraExposure() { return s_physicalCameraExposure; }
void SetPhysicalCameraSettings(float iso, float shutterSeconds,
                               float apertureFNumber) {
  s_cameraIso = (std::max)(iso, 1.0f);
  s_cameraShutterSeconds = (std::max)(shutterSeconds, 1.0f / 8000.0f);
  s_cameraApertureFNumber = (std::max)(apertureFNumber, 0.7f);
  s_hasTonemappedFrame = false;
}
void GetPhysicalCameraSettings(float &iso, float &shutterSeconds,
                               float &apertureFNumber) {
  iso = s_cameraIso;
  shutterSeconds = s_cameraShutterSeconds;
  apertureFNumber = s_cameraApertureFNumber;
}
float GetPhysicalCameraEV100() {
  float safeIso = (std::max)(s_cameraIso, 1.0f);
  float safeShutter = (std::max)(s_cameraShutterSeconds, 1.0e-6f);
  float safeAperture = (std::max)(s_cameraApertureFNumber, 0.7f);
  return log2f((safeAperture * safeAperture / safeShutter) *
               (100.0f / safeIso));
}

static void EnsureNoiseStatsPipeline();
static void EnsureAvgLumPipeline();
static void EnsureRestirSpatialPipeline();
static void EnsureRestirGiSpatialPipeline();
static void EnsureWavefrontBootstrapPipeline();
static void EnsureWavefrontCounterResetPipeline();
static void EnsureWavefrontPrepareIndirectArgsPipeline();
static void EnsureWavefrontResolvePipeline();
static void EnsureWavefrontRestirSeedPipeline();
static void EnsureWavefrontSecondaryResolvePipeline();
static void EnsureWavefrontShadowIntegratePipeline();
static void EnsureWavefrontAccumulatePipeline();
static void EnsureWavefrontIndirectCommandSignatures();
static void PrepareWavefrontBackendPipelines();
static void DispatchWavefrontBootstrap(ID3D12GraphicsCommandList4 *list,
                                       ID3D12Resource *cameraCB);
static void DispatchWavefrontCounterReset(ID3D12GraphicsCommandList4 *list,
                                          UINT counterIndex,
                                          UINT resetValue,
                                          UINT resetCount = 1u);
static void DispatchWavefrontPrepareIndirectArgs(
  ID3D12GraphicsCommandList4 *list, UINT queueCounterIndex,
  UINT dispatchArgsIndex, UINT reservedSlotBase, UINT reservedFlags);
static void UploadWavefrontIndirectDispatchRecords(
  ID3D12GraphicsCommandList4 *list);
static void DispatchWavefrontPrimaryVisibility(
  ID3D12GraphicsCommandList4 *list);
static void DispatchWavefrontSecondaryVisibility(
  ID3D12GraphicsCommandList4 *list, UINT sourceQueueCounterIndex);
static void DispatchWavefrontShadowVisibility(
  ID3D12GraphicsCommandList4 *list);
static void DispatchWavefrontResolvePrimary(ID3D12GraphicsCommandList4 *list,
                                            ID3D12Resource *cameraCB,
                                            ID3D12Resource *materialCB,
                                            ID3D12Resource *meshDataSB,
                                            ID3D12Resource *materialExtraSB,
                                            UINT resolveFlags = 0u,
                                            bool useIndirectDispatch = false,
                                            UINT dispatchArgsIndex = 2,
                                            bool doBarriers = true);
static void DispatchWavefrontRestirSeed(ID3D12GraphicsCommandList4 *list,
                                        ID3D12Resource *cameraCB,
                                        UINT seedFlags = 0u,
                                        bool useIndirectDispatch = false,
                                        UINT dispatchArgsIndex = 2,
                                        bool doBarriers = true);
static void DispatchWavefrontResolveSecondary(ID3D12GraphicsCommandList4 *list,
                                              ID3D12Resource *cameraCB,
                                              UINT sourceQueueCounterIndex,
                                              UINT extraResolveFlags = 0u,
                                              bool useIndirectDispatch = false,
                                              UINT dispatchArgsIndex = 2,
                                              bool doBarriers = true);

struct TonemapConstants {
  uint32_t outWidth;
  uint32_t outHeight;
  float exposure;
  float vignette;
  float saturation;
  float contrast;
  float aoIntensity;
  float aoRadiusMeters;
  uint32_t aoMode;
  float _pad0;
};

static void EnsureTonemapPipeline() {
  if (s_tonemapPSO && s_tonemapRootSig && s_tonemapCB && s_tonemapHeap)
    return;
  if (!s_device)
    return;

  // Root signature: b0 constants, t0..t2 SRVs, u0 UAV
  D3D12_DESCRIPTOR_RANGE srvRange{};
  srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  srvRange.NumDescriptors = 3;
  srvRange.BaseShaderRegister = 0;
  srvRange.RegisterSpace = 0;

  D3D12_DESCRIPTOR_RANGE uavRange{};
  uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  uavRange.NumDescriptors = 1;
  uavRange.BaseShaderRegister = 0;
  uavRange.RegisterSpace = 0;

  D3D12_ROOT_PARAMETER params[3] = {};
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  params[0].Descriptor.ShaderRegister = 0;
  params[0].Descriptor.RegisterSpace = 0;
  params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[1].DescriptorTable.NumDescriptorRanges = 1;
  params[1].DescriptorTable.pDescriptorRanges = &srvRange;
  params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[2].DescriptorTable.NumDescriptorRanges = 1;
  params[2].DescriptorTable.pDescriptorRanges = &uavRange;
  params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  D3D12_STATIC_SAMPLER_DESC sampler{};
  sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler.MipLODBias = 0.0f;
  sampler.MaxAnisotropy = 1;
  sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
  sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
  sampler.MinLOD = 0.0f;
  sampler.MaxLOD = D3D12_FLOAT32_MAX;
  sampler.ShaderRegister = 0;
  sampler.RegisterSpace = 0;
  sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  D3D12_ROOT_SIGNATURE_DESC rsDesc{};
  rsDesc.NumParameters = _countof(params);
  rsDesc.pParameters = params;
  rsDesc.NumStaticSamplers = 1;
  rsDesc.pStaticSamplers = &sampler;
  rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

  ComPtr<ID3DBlob> sig;
  ComPtr<ID3DBlob> err;
  HRESULT hrSerialize = D3D12SerializeRootSignature(
      &rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
  if (FAILED(hrSerialize)) {
    if (err)
      fprintf(stderr, "DxrRenderer: Tonemap root signature error: %s\n",
              (char *)err->GetBufferPointer());
    return;
  }
  ThrowIfFailed(s_device->CreateRootSignature(0, sig->GetBufferPointer(),
                                              sig->GetBufferSize(),
                                              IID_PPV_ARGS(&s_tonemapRootSig)));

  // Compile compute shader
  ComPtr<IDxcBlob> cs;
  try {
    std::vector<std::wstring> defines;
    cs = s_dxcHelper.Compile(L"shaders/dxr_tonemap_cs.hlsl", L"CSMain",
                             L"cs_6_3", defines);
  } catch (const std::exception &e) {
    fprintf(stderr, "DxrRenderer: Tonemap CS compile failed: %s\n", e.what());
    return;
  }
  if (!cs) {
    fprintf(stderr, "DxrRenderer: Tonemap CS blob null\n");
    return;
  }

  D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
  psoDesc.pRootSignature = s_tonemapRootSig.Get();
  psoDesc.CS.pShaderBytecode = cs->GetBufferPointer();
  psoDesc.CS.BytecodeLength = cs->GetBufferSize();
  ThrowIfFailed(s_device->CreateComputePipelineState(
      &psoDesc, IID_PPV_ARGS(&s_tonemapPSO)));

  // Descriptor heap: HDR SRV, depth SRV, normal SRV, UAV
  D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
  heapDesc.NumDescriptors = 4;
  heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  ThrowIfFailed(
      s_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&s_tonemapHeap)));

  // Constant buffer
  D3D12_HEAP_PROPERTIES uploadProps{};
  uploadProps.Type = D3D12_HEAP_TYPE_UPLOAD;
  D3D12_RESOURCE_DESC cbDesc{};
  cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  cbDesc.Width = (sizeof(TonemapConstants) + 255) & ~255;
  cbDesc.Height = 1;
  cbDesc.DepthOrArraySize = 1;
  cbDesc.MipLevels = 1;
  cbDesc.SampleDesc.Count = 1;
  cbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ThrowIfFailed(s_device->CreateCommittedResource(
      &uploadProps, D3D12_HEAP_FLAG_NONE, &cbDesc,
      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&s_tonemapCB)));
  if (s_tonemapCB)
    s_tonemapCB->SetName(L"Tonemap Constants");
}

static void EnsureRestirSpatialPipeline() {
  if (s_restirSpatialPSO && s_restirSpatialRootSig) {
    return;
  }
  if (!s_device) {
    return;
  }

  D3D12_DESCRIPTOR_RANGE uavRange = {};
  uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  uavRange.NumDescriptors = 12; // u2..u13
  uavRange.BaseShaderRegister = 2;
  uavRange.RegisterSpace = 0;
  uavRange.OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  D3D12_DESCRIPTOR_RANGE texRange = {};
  texRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  texRange.NumDescriptors = 2048; // t1..t2049
  texRange.BaseShaderRegister = 1;
  texRange.RegisterSpace = 0;
  texRange.OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  D3D12_ROOT_PARAMETER params[4] = {};
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  params[0].Descriptor.ShaderRegister = 0;
  params[0].Descriptor.RegisterSpace = 0;
  params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[1].DescriptorTable.NumDescriptorRanges = 1;
  params[1].DescriptorTable.pDescriptorRanges = &uavRange;
  params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  // Lights StructuredBuffer (t5000)
  params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  params[2].Descriptor.ShaderRegister = 5000;
  params[2].Descriptor.RegisterSpace = 0;
  params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  // Texture Table (t1..t2048)
  params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[3].DescriptorTable.NumDescriptorRanges = 1;
  params[3].DescriptorTable.pDescriptorRanges = &texRange;
  params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
  rsDesc.NumParameters = _countof(params);
  rsDesc.pParameters = params;
  rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

  ComPtr<ID3DBlob> sig, err;
  HRESULT hrSerialize = D3D12SerializeRootSignature(
      &rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
  if (FAILED(hrSerialize)) {
    if (err) {
      fprintf(stderr, "DxrRenderer: ReSTIR spatial RS error: %s\n",
              (char *)err->GetBufferPointer());
    }
    return;
  }

  ThrowIfFailed(s_device->CreateRootSignature(
      0, sig->GetBufferPointer(), sig->GetBufferSize(),
      IID_PPV_ARGS(&s_restirSpatialRootSig)));

  ComPtr<IDxcBlob> cs;
  try {
    std::vector<std::wstring> defines;
    cs = s_dxcHelper.Compile(L"shaders/restir_spatial_cs.hlsl", L"CSMain",
                             L"cs_6_5", defines);
  } catch (const std::exception &e) {
    fprintf(stderr, "DxrRenderer: ReSTIR spatial CS compile failed: %s\n",
            e.what());
    return;
  }
  if (!cs) {
    fprintf(stderr, "DxrRenderer: ReSTIR spatial CS blob null\n");
    return;
  }

  D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature = s_restirSpatialRootSig.Get();
  psoDesc.CS.pShaderBytecode = cs->GetBufferPointer();
  psoDesc.CS.BytecodeLength = cs->GetBufferSize();
  HRESULT hrPso = s_device->CreateComputePipelineState(
      &psoDesc, IID_PPV_ARGS(&s_restirSpatialPSO));
  if (FAILED(hrPso)) {
    fprintf(stderr,
            "DxrRenderer: ReSTIR spatial CreateComputePipelineState failed: "
            "0x%08x\n",
            (unsigned)hrPso);
    DumpD3D12InfoQueueMessages("ReSTIR spatial PSO create");
    s_restirSpatialPSO.Reset();
    s_restirSpatialRootSig.Reset();
    return;
  }
}

static void EnsureRestirGiSpatialPipeline() {
  if (s_restirGiSpatialPSO && s_restirGiSpatialRootSig) {
    return;
  }
  if (!s_device) {
    return;
  }

  // Root signature:
  //  - b0: Camera constants
  //  - UAV table: u4..u13 (GI reservoirs + depth/normal compatibility data)
  D3D12_DESCRIPTOR_RANGE uavRange = {};
  uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  uavRange.NumDescriptors = 10; // u4..u13
  uavRange.BaseShaderRegister = 4;
  uavRange.RegisterSpace = 0;
  uavRange.OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  D3D12_ROOT_PARAMETER params[2] = {};
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  params[0].Descriptor.ShaderRegister = 0;
  params[0].Descriptor.RegisterSpace = 0;
  params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[1].DescriptorTable.NumDescriptorRanges = 1;
  params[1].DescriptorTable.pDescriptorRanges = &uavRange;
  params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
  rsDesc.NumParameters = _countof(params);
  rsDesc.pParameters = params;
  rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

  ComPtr<ID3DBlob> sig, err;
  HRESULT hrSerialize = D3D12SerializeRootSignature(
      &rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
  if (FAILED(hrSerialize)) {
    if (err) {
      fprintf(stderr, "DxrRenderer: ReSTIR GI spatial RS error: %s\n",
              (char *)err->GetBufferPointer());
    }
    return;
  }

  ThrowIfFailed(s_device->CreateRootSignature(
      0, sig->GetBufferPointer(), sig->GetBufferSize(),
      IID_PPV_ARGS(&s_restirGiSpatialRootSig)));

  ComPtr<IDxcBlob> cs;
  try {
    std::vector<std::wstring> defines;
    cs = s_dxcHelper.Compile(L"shaders/restir_gi_spatial_cs.hlsl", L"CSMain",
                             L"cs_6_3", defines);
  } catch (const std::exception &e) {
    fprintf(stderr, "DxrRenderer: ReSTIR GI spatial CS compile failed: %s\n",
            e.what());
    return;
  }
  if (!cs) {
    fprintf(stderr, "DxrRenderer: ReSTIR GI spatial CS blob null\n");
    return;
  }

  D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature = s_restirGiSpatialRootSig.Get();
  psoDesc.CS.pShaderBytecode = cs->GetBufferPointer();
  psoDesc.CS.BytecodeLength = cs->GetBufferSize();
  HRESULT hrPso = s_device->CreateComputePipelineState(
      &psoDesc, IID_PPV_ARGS(&s_restirGiSpatialPSO));
  if (FAILED(hrPso)) {
    fprintf(stderr,
            "DxrRenderer: ReSTIR GI spatial CreateComputePipelineState "
            "failed: 0x%08x\n",
            (unsigned)hrPso);
    DumpD3D12InfoQueueMessages("ReSTIR GI spatial PSO create");
    s_restirGiSpatialPSO.Reset();
    s_restirGiSpatialRootSig.Reset();
    return;
  }
}

static void EnsureAsyncComputeContext() {
  if (s_asyncRestirAvailable || s_asyncRestirInitTried || !s_device ||
      !s_commandQueue) {
    return;
  }
  s_asyncRestirInitTried = true;

  D3D12_COMMAND_QUEUE_DESC queueDesc = {};
  queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
  queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
  queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  queueDesc.NodeMask = 0;

  HRESULT hr = s_device->CreateCommandQueue(&queueDesc,
                                            IID_PPV_ARGS(&s_asyncComputeQueue));
  if (FAILED(hr)) {
    fprintf(stderr,
            "DxrRenderer: Async compute queue creation failed (0x%08x). "
            "Falling back to direct queue ReSTIR.\n",
            (unsigned)hr);
    return;
  }

  hr = s_device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                             IID_PPV_ARGS(&s_asyncDirectFence));
  if (FAILED(hr)) {
    fprintf(stderr,
            "DxrRenderer: Async direct fence creation failed (0x%08x)\n",
            (unsigned)hr);
    s_asyncComputeQueue.Reset();
    return;
  }

  hr = s_device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                             IID_PPV_ARGS(&s_asyncComputeFence));
  if (FAILED(hr)) {
    fprintf(stderr,
            "DxrRenderer: Async compute fence creation failed (0x%08x)\n",
            (unsigned)hr);
    s_asyncDirectFence.Reset();
    s_asyncComputeQueue.Reset();
    return;
  }

  hr = s_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE,
                                        IID_PPV_ARGS(&s_asyncComputeAllocator));
  if (FAILED(hr)) {
    fprintf(stderr,
            "DxrRenderer: Async compute allocator creation failed (0x%08x)\n",
            (unsigned)hr);
    s_asyncComputeFence.Reset();
    s_asyncDirectFence.Reset();
    s_asyncComputeQueue.Reset();
    return;
  }

  hr = s_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE,
                                   s_asyncComputeAllocator.Get(), nullptr,
                                   IID_PPV_ARGS(&s_asyncComputeList));
  if (FAILED(hr)) {
    fprintf(stderr,
            "DxrRenderer: Async compute list creation failed (0x%08x)\n",
            (unsigned)hr);
    s_asyncComputeAllocator.Reset();
    s_asyncComputeFence.Reset();
    s_asyncDirectFence.Reset();
    s_asyncComputeQueue.Reset();
    return;
  }
  s_asyncComputeList->Close();

  s_asyncComputeFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  if (!s_asyncComputeFenceEvent) {
    fprintf(stderr, "DxrRenderer: Async compute fence event creation failed\n");
    s_asyncComputeList.Reset();
    s_asyncComputeAllocator.Reset();
    s_asyncComputeFence.Reset();
    s_asyncDirectFence.Reset();
    s_asyncComputeQueue.Reset();
    return;
  }

  s_asyncDirectFenceValue = 1;
  s_asyncComputeFenceValue = 1;
  s_asyncComputePendingFenceWait = 0;
  s_asyncRestirAvailable = true;
}

static void DisableAsyncRestir(const char *reason) {
  if (reason && reason[0] != '\0') {
    fprintf(stderr, "DxrRenderer: %s\n", reason);
  }
  s_asyncRestirPending = false;
  s_asyncComputePendingFenceWait = 0;
  s_asyncRestirAvailable = false;
  s_asyncRestirInitTried = true;
  if (s_asyncComputeFenceEvent) {
    CloseHandle(s_asyncComputeFenceEvent);
    s_asyncComputeFenceEvent = nullptr;
  }
  s_asyncComputeList.Reset();
  s_asyncComputeAllocator.Reset();
  s_asyncComputeFence.Reset();
  s_asyncDirectFence.Reset();
  s_asyncComputeQueue.Reset();
}

static bool EnsureAsyncRestirCameraBuffer() {
  if (s_asyncRestirCameraCB) {
    return true;
  }
  if (!s_device) {
    return false;
  }

  D3D12_HEAP_PROPERTIES uploadProps = {};
  uploadProps.Type = D3D12_HEAP_TYPE_UPLOAD;

  D3D12_RESOURCE_DESC cbDesc = {};
  cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  cbDesc.Width = (sizeof(CameraCB) + 255u) & ~255u;
  cbDesc.Height = 1;
  cbDesc.DepthOrArraySize = 1;
  cbDesc.MipLevels = 1;
  cbDesc.SampleDesc.Count = 1;
  cbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

  HRESULT hr = s_device->CreateCommittedResource(
      &uploadProps, D3D12_HEAP_FLAG_NONE, &cbDesc,
      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
      IID_PPV_ARGS(&s_asyncRestirCameraCB));
  if (FAILED(hr)) {
    fprintf(stderr,
            "DxrRenderer: Failed to create async ReSTIR camera buffer "
            "(0x%08x)\n",
            (unsigned)hr);
    s_asyncRestirCameraCB.Reset();
    return false;
  }
  return true;
}

static bool UploadAsyncRestirCamera(const CameraCB &cameraSnapshot) {
  if (!EnsureAsyncRestirCameraBuffer()) {
    return false;
  }
  void *dst = nullptr;
  HRESULT hr = s_asyncRestirCameraCB->Map(0, nullptr, &dst);
  if (FAILED(hr) || !dst) {
    fprintf(stderr,
            "DxrRenderer: Failed to map async ReSTIR camera buffer "
            "(0x%08x)\n",
            (unsigned)hr);
    return false;
  }
  memcpy(dst, &cameraSnapshot, sizeof(CameraCB));
  s_asyncRestirCameraCB->Unmap(0, nullptr);
  return true;
}

static void WaitForAsyncRestirIdleForLightUpdates() {
  if (!s_asyncRestirAvailable || !s_asyncComputeFence) {
    return;
  }

  // Drop a not-yet-submitted pass if light data changed this frame.
  s_asyncRestirPending = false;

  UINT64 waitFence = s_asyncComputePendingFenceWait;
  if (waitFence == 0 && s_asyncComputeFenceValue > 1) {
    waitFence = s_asyncComputeFenceValue - 1;
  }
  if (waitFence == 0) {
    return;
  }

  if (s_asyncComputeFence->GetCompletedValue() >= waitFence) {
    s_asyncComputePendingFenceWait = 0;
    return;
  }

  if (!s_asyncComputeFenceEvent) {
    DisableAsyncRestir(
        "Async ReSTIR fence event missing while syncing light updates; "
        "falling back to direct-queue ReSTIR.");
    return;
  }

  HRESULT hr = s_asyncComputeFence->SetEventOnCompletion(waitFence,
                                                         s_asyncComputeFenceEvent);
  if (FAILED(hr)) {
    DisableAsyncRestir(
        "Failed to wait for async ReSTIR during light update; falling back "
        "to direct-queue ReSTIR.");
    return;
  }
  if (WaitForSingleObject(s_asyncComputeFenceEvent, 5000) == WAIT_TIMEOUT) {
    DisableAsyncRestir(
        "Timeout waiting for async ReSTIR during light update; falling back "
        "to direct-queue ReSTIR.");
    return;
  }
  s_asyncComputePendingFenceWait = 0;
}

void WaitForAsyncRestirIdle() {
  WaitForAsyncRestirIdleForLightUpdates();
}

static void DispatchRestirSpatialPasses(ID3D12GraphicsCommandList4 *list,
                                        ID3D12Resource *cameraCB) {
  if (!list || !cameraCB || !s_srvHeap || !s_device) {
    return;
  }

  EnsureRestirSpatialPipeline();
  if (s_restirSpatialPSO && s_restirSpatialRootSig) {
    D3D12_RESOURCE_BARRIER uavBarrier = {};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = nullptr;
    list->ResourceBarrier(1, &uavBarrier);

    ID3D12DescriptorHeap *rtHeaps[] = {s_srvHeap.Get()};
    list->SetDescriptorHeaps(1, rtHeaps);
    list->SetPipelineState(s_restirSpatialPSO.Get());
    list->SetComputeRootSignature(s_restirSpatialRootSig.Get());
    list->SetComputeRootConstantBufferView(0, cameraCB->GetGPUVirtualAddress());

    UINT inc = s_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_GPU_DESCRIPTOR_HANDLE restirUavTable = s_outputUAVGpu;
    restirUavTable.ptr += (UINT64)2 * (UINT64)inc; // u2..u13 table base
    list->SetComputeRootDescriptorTable(1, restirUavTable);

    list->SetComputeRootShaderResourceView(
        2, s_lightBuffer ? s_lightBuffer->GetGPUVirtualAddress() : 0);

    list->SetComputeRootDescriptorTable(3, s_texTableGpu);

    const UINT gx = (s_outputWidth + 7) / 8;
    const UINT gy = (s_outputHeight + 7) / 8;
    list->Dispatch(gx, gy, 1);

    uavBarrier.UAV.pResource = nullptr;
    list->ResourceBarrier(1, &uavBarrier);
  }

  EnsureRestirGiSpatialPipeline();
  if (s_restirGiSpatialPSO && s_restirGiSpatialRootSig) {
    D3D12_RESOURCE_BARRIER uavBarrier = {};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = nullptr;
    list->ResourceBarrier(1, &uavBarrier);

    ID3D12DescriptorHeap *rtHeaps[] = {s_srvHeap.Get()};
    list->SetDescriptorHeaps(1, rtHeaps);
    list->SetPipelineState(s_restirGiSpatialPSO.Get());
    list->SetComputeRootSignature(s_restirGiSpatialRootSig.Get());
    list->SetComputeRootConstantBufferView(0, cameraCB->GetGPUVirtualAddress());

    UINT inc = s_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_GPU_DESCRIPTOR_HANDLE restirGiUavTable = s_outputUAVGpu;
    restirGiUavTable.ptr += (UINT64)4 * (UINT64)inc; // u4..u13 table base
    list->SetComputeRootDescriptorTable(1, restirGiUavTable);

    const UINT gx = (s_outputWidth + 7) / 8;
    const UINT gy = (s_outputHeight + 7) / 8;
    list->Dispatch(gx, gy, 1);

    uavBarrier.UAV.pResource = nullptr;
    list->ResourceBarrier(1, &uavBarrier);
  }
}

static void EnsureWavefrontBootstrapPipeline() {
  if (s_wavefrontBootstrapPSO && s_wavefrontBootstrapRootSig) {
    return;
  }
  if (!s_device) {
    return;
  }

  D3D12_DESCRIPTOR_RANGE uavRange = {};
  uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  uavRange.NumDescriptors = DXR_HEAP_UAV_COUNT; // u0..u33
  uavRange.BaseShaderRegister = 0;
  uavRange.RegisterSpace = 0;
  uavRange.OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  D3D12_ROOT_PARAMETER params[4] = {};
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  params[0].Descriptor.ShaderRegister = 0;
  params[0].Descriptor.RegisterSpace = 0;
  params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  params[1].Constants.ShaderRegister = 1;
  params[1].Constants.RegisterSpace = 0;
  params[1].Constants.Num32BitValues = 4;
  params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[2].DescriptorTable.NumDescriptorRanges = 1;
  params[2].DescriptorTable.pDescriptorRanges = &uavRange;
  params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  params[3].Descriptor.ShaderRegister = 5000;
  params[3].Descriptor.RegisterSpace = 0;
  params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
  rsDesc.NumParameters = _countof(params);
  rsDesc.pParameters = params;
  rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

  ComPtr<ID3DBlob> sig, err;
  HRESULT hrSerialize = D3D12SerializeRootSignature(
      &rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
  if (FAILED(hrSerialize)) {
    if (err) {
      fprintf(stderr, "DxrRenderer: Wavefront bootstrap RS error: %s\n",
              (char *)err->GetBufferPointer());
    }
    return;
  }

  ThrowIfFailed(s_device->CreateRootSignature(
      0, sig->GetBufferPointer(), sig->GetBufferSize(),
      IID_PPV_ARGS(&s_wavefrontBootstrapRootSig)));

  ComPtr<IDxcBlob> cs;
  try {
    std::vector<std::wstring> defines;
    cs = s_dxcHelper.Compile(L"shaders/wavefront_bootstrap_cs.hlsl", L"CSMain",
                             L"cs_6_5", defines);
  } catch (const std::exception &e) {
    fprintf(stderr, "DxrRenderer: Wavefront bootstrap CS compile failed: %s\n",
            e.what());
    return;
  }
  if (!cs) {
    fprintf(stderr, "DxrRenderer: Wavefront bootstrap CS blob null\n");
    return;
  }

  D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature = s_wavefrontBootstrapRootSig.Get();
  psoDesc.CS.pShaderBytecode = cs->GetBufferPointer();
  psoDesc.CS.BytecodeLength = cs->GetBufferSize();
  HRESULT hrPso = s_device->CreateComputePipelineState(
      &psoDesc, IID_PPV_ARGS(&s_wavefrontBootstrapPSO));
  if (FAILED(hrPso)) {
    fprintf(stderr,
            "DxrRenderer: Wavefront bootstrap PSO creation failed: 0x%08x\n",
            (unsigned)hrPso);
    DumpD3D12InfoQueueMessages("Wavefront bootstrap PSO create");
    s_wavefrontBootstrapPSO.Reset();
    s_wavefrontBootstrapRootSig.Reset();
  }
}

static void DispatchWavefrontBootstrap(ID3D12GraphicsCommandList4 *list,
                                       ID3D12Resource *cameraCB) {
  if (!list || !cameraCB || !s_srvHeap || !s_device) {
    return;
  }

  EnsureWavefrontBootstrapPipeline();
  if (!s_wavefrontBootstrapPSO || !s_wavefrontBootstrapRootSig) {
    return;
  }

  ID3D12DescriptorHeap *rtHeaps[] = {s_srvHeap.Get()};
  list->SetDescriptorHeaps(1, rtHeaps);
  list->SetPipelineState(s_wavefrontBootstrapPSO.Get());
  list->SetComputeRootSignature(s_wavefrontBootstrapRootSig.Get());
  list->SetComputeRootConstantBufferView(0, cameraCB->GetGPUVirtualAddress());

  const UINT bootstrapConstants[4] = {
      s_outputWidth,
      s_outputHeight,
      static_cast<UINT>(s_pathTracingBackend),
      static_cast<UINT>(s_wavefrontPathQueueCapacity)};
  list->SetComputeRoot32BitConstants(1, _countof(bootstrapConstants),
                                     bootstrapConstants, 0);

  UINT inc = s_device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  list->SetComputeRootDescriptorTable(2, s_outputUAVGpu);
  list->SetComputeRootShaderResourceView(
      3, s_lightBuffer ? s_lightBuffer->GetGPUVirtualAddress() : 0);

  D3D12_RESOURCE_BARRIER uavBarrier = {};
  uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;

  auto ClearStructuredUav = [&](ID3D12Resource *resource, UINT heapOffset) {
    if (!resource) {
      return;
    }
    UINT zeros[4] = {0, 0, 0, 0};
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle =
        s_srvHeap->GetCPUDescriptorHandleForHeapStart();
    cpuHandle.ptr += static_cast<SIZE_T>(heapOffset) * inc;

    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = s_outputUAVGpu;
    gpuHandle.ptr += (UINT64)(heapOffset - DXR_HEAP_UAV_OFFSET) * (UINT64)inc;

    list->ClearUnorderedAccessViewUint(gpuHandle, cpuHandle, resource, zeros, 0,
                                       nullptr);
  };

  ClearStructuredUav(s_wavefrontQueueCountersBuffer.Get(),
                     DXR_HEAP_WAVEFRONT_COUNTERS_OFFSET);
  ClearStructuredUav(s_wavefrontDispatchArgsBuffer.Get(),
                     DXR_HEAP_WAVEFRONT_DISPATCH_ARGS_OFFSET);
  ClearStructuredUav(s_wavefrontStatsBuffer.Get(), DXR_HEAP_WAVEFRONT_STATS_OFFSET);

  list->ResourceBarrier(1, &uavBarrier);

  const UINT gx = (s_outputWidth + 7) / 8;
  const UINT gy = (s_outputHeight + 7) / 8;
  const UINT totalPaths = s_outputWidth * s_outputHeight;
  const UINT clampedPaths =
      (totalPaths > s_wavefrontPathQueueCapacity)
          ? static_cast<UINT>(s_wavefrontPathQueueCapacity)
          : totalPaths;
  s_lastWavefrontBootstrapPathCount = clampedPaths;
  s_lastWavefrontBootstrapOverflowCount = totalPaths - clampedPaths;
  s_lastWavefrontBootstrapDispatchGroups = (clampedPaths + 63u) / 64u;
  list->Dispatch(gx, gy, 1);

  list->ResourceBarrier(1, &uavBarrier);
}

static void EnsureWavefrontCounterResetPipeline() {
  if (s_wavefrontCounterResetPSO && s_wavefrontCounterResetRootSig) {
    return;
  }
  if (!s_device) {
    return;
  }

  D3D12_DESCRIPTOR_RANGE uavRange = {};
  uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  uavRange.NumDescriptors = DXR_HEAP_UAV_COUNT;
  uavRange.BaseShaderRegister = 0;
  uavRange.RegisterSpace = 0;
  uavRange.OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  D3D12_ROOT_PARAMETER params[3] = {};
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  params[0].Constants.ShaderRegister = 0;
  params[0].Constants.RegisterSpace = 0;
  params[0].Constants.Num32BitValues = 4;
  params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[1].DescriptorTable.NumDescriptorRanges = 1;
  params[1].DescriptorTable.pDescriptorRanges = &uavRange;
  params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  params[2].Descriptor.ShaderRegister = 5000;
  params[2].Descriptor.RegisterSpace = 0;
  params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
  rsDesc.NumParameters = _countof(params);
  rsDesc.pParameters = params;
  rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

  ComPtr<ID3DBlob> sig, err;
  HRESULT hrSerialize = D3D12SerializeRootSignature(
      &rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
  if (FAILED(hrSerialize)) {
    if (err) {
      fprintf(stderr, "DxrRenderer: Wavefront counter reset RS error: %s\n",
              (char *)err->GetBufferPointer());
    }
    return;
  }

  ThrowIfFailed(s_device->CreateRootSignature(
      0, sig->GetBufferPointer(), sig->GetBufferSize(),
      IID_PPV_ARGS(&s_wavefrontCounterResetRootSig)));

  ComPtr<IDxcBlob> cs;
  try {
    std::vector<std::wstring> defines;
    cs = s_dxcHelper.Compile(L"shaders/wavefront_counter_reset_cs.hlsl",
                             L"CSMain", L"cs_6_5", defines);
  } catch (const std::exception &e) {
    fprintf(stderr,
            "DxrRenderer: Wavefront counter reset CS compile failed: %s\n",
            e.what());
    return;
  }
  if (!cs) {
    fprintf(stderr, "DxrRenderer: Wavefront counter reset CS blob null\n");
    return;
  }

  D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature = s_wavefrontCounterResetRootSig.Get();
  psoDesc.CS.pShaderBytecode = cs->GetBufferPointer();
  psoDesc.CS.BytecodeLength = cs->GetBufferSize();
  HRESULT hrPso = s_device->CreateComputePipelineState(
      &psoDesc, IID_PPV_ARGS(&s_wavefrontCounterResetPSO));
  if (FAILED(hrPso)) {
    fprintf(stderr,
            "DxrRenderer: Wavefront counter reset PSO creation failed: 0x%08x\n",
            (unsigned)hrPso);
    DumpD3D12InfoQueueMessages("Wavefront counter reset PSO create");
    s_wavefrontCounterResetPSO.Reset();
    s_wavefrontCounterResetRootSig.Reset();
  }
}

static void DispatchWavefrontCounterReset(ID3D12GraphicsCommandList4 *list,
                                          UINT counterIndex,
                                          UINT resetValue,
                                          UINT resetCount) {
  if (!list || !s_srvHeap || !s_device) {
    return;
  }

  EnsureWavefrontCounterResetPipeline();
  if (!s_wavefrontCounterResetPSO || !s_wavefrontCounterResetRootSig) {
    return;
  }

  ID3D12DescriptorHeap *rtHeaps[] = {s_srvHeap.Get()};
  list->SetDescriptorHeaps(1, rtHeaps);
  list->SetPipelineState(s_wavefrontCounterResetPSO.Get());
  list->SetComputeRootSignature(s_wavefrontCounterResetRootSig.Get());

  const UINT resetConstants[4] = {counterIndex, resetValue, resetCount, 0u};
  list->SetComputeRoot32BitConstants(0, _countof(resetConstants),
                                     resetConstants, 0);
  list->SetComputeRootDescriptorTable(1, s_outputUAVGpu);
  list->SetComputeRootShaderResourceView(
      2, s_lightBuffer ? s_lightBuffer->GetGPUVirtualAddress() : 0);
  list->Dispatch(1, 1, 1);

  D3D12_RESOURCE_BARRIER uavBarrier = {};
  uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  uavBarrier.UAV.pResource = nullptr;
  list->ResourceBarrier(1, &uavBarrier);
}

static void EnsureWavefrontPrepareIndirectArgsPipeline() {
  if (s_wavefrontPrepareIndirectArgsPSO &&
      s_wavefrontPrepareIndirectArgsRootSig) {
    return;
  }
  if (!s_device) {
    return;
  }

  D3D12_DESCRIPTOR_RANGE uavRange = {};
  uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  uavRange.NumDescriptors = DXR_HEAP_UAV_COUNT;
  uavRange.BaseShaderRegister = 0;
  uavRange.RegisterSpace = 0;
  uavRange.OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  D3D12_ROOT_PARAMETER params[3] = {};
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  params[0].Constants.ShaderRegister = 0;
  params[0].Constants.RegisterSpace = 0;
  params[0].Constants.Num32BitValues = 5;
  params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[1].DescriptorTable.NumDescriptorRanges = 1;
  params[1].DescriptorTable.pDescriptorRanges = &uavRange;
  params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  params[2].Descriptor.ShaderRegister = 5000;
  params[2].Descriptor.RegisterSpace = 0;
  params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
  rsDesc.NumParameters = _countof(params);
  rsDesc.pParameters = params;
  rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

  ComPtr<ID3DBlob> sig, err;
  HRESULT hrSerialize = D3D12SerializeRootSignature(
      &rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
  if (FAILED(hrSerialize)) {
    if (err) {
      fprintf(stderr,
              "DxrRenderer: Wavefront prepare indirect args RS error: %s\n",
              (char *)err->GetBufferPointer());
    }
    return;
  }

  ThrowIfFailed(s_device->CreateRootSignature(
      0, sig->GetBufferPointer(), sig->GetBufferSize(),
      IID_PPV_ARGS(&s_wavefrontPrepareIndirectArgsRootSig)));

  ComPtr<IDxcBlob> cs;
  try {
    std::vector<std::wstring> defines;
    cs = s_dxcHelper.Compile(L"shaders/wavefront_prepare_indirect_args_cs.hlsl",
                             L"CSMain", L"cs_6_5", defines);
  } catch (const std::exception &e) {
    fprintf(stderr,
            "DxrRenderer: Wavefront prepare indirect args CS compile failed: %s\n",
            e.what());
    return;
  }
  if (!cs) {
    fprintf(stderr,
            "DxrRenderer: Wavefront prepare indirect args CS blob null\n");
    return;
  }

  D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature = s_wavefrontPrepareIndirectArgsRootSig.Get();
  psoDesc.CS.pShaderBytecode = cs->GetBufferPointer();
  psoDesc.CS.BytecodeLength = cs->GetBufferSize();
  HRESULT hrPso = s_device->CreateComputePipelineState(
      &psoDesc, IID_PPV_ARGS(&s_wavefrontPrepareIndirectArgsPSO));
  if (FAILED(hrPso)) {
    fprintf(stderr,
            "DxrRenderer: Wavefront prepare indirect args PSO creation failed: 0x%08x\n",
            (unsigned)hrPso);
    DumpD3D12InfoQueueMessages("Wavefront prepare indirect args PSO create");
    s_wavefrontPrepareIndirectArgsPSO.Reset();
    s_wavefrontPrepareIndirectArgsRootSig.Reset();
  }
}

static void EnsureWavefrontIndirectCommandSignatures() {
  if (!s_device) {
    return;
  }
  if (!s_wavefrontDispatchCommandSignature) {
    D3D12_INDIRECT_ARGUMENT_DESC argDesc = {};
    argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;

    D3D12_COMMAND_SIGNATURE_DESC sigDesc = {};
    sigDesc.ByteStride = sizeof(WavefrontDispatchArgsGpu);
    sigDesc.NumArgumentDescs = 1;
    sigDesc.pArgumentDescs = &argDesc;
    ThrowIfFailed(s_device->CreateCommandSignature(
        &sigDesc, nullptr, IID_PPV_ARGS(&s_wavefrontDispatchCommandSignature)));
  }
  if (!s_wavefrontDispatchRaysCommandSignature) {
    D3D12_INDIRECT_ARGUMENT_DESC argDesc = {};
    argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_RAYS;

    D3D12_COMMAND_SIGNATURE_DESC sigDesc = {};
    sigDesc.ByteStride = kWavefrontDispatchRaysRecordStride;
    sigDesc.NumArgumentDescs = 1;
    sigDesc.pArgumentDescs = &argDesc;
    ThrowIfFailed(s_device->CreateCommandSignature(
        &sigDesc, nullptr,
        IID_PPV_ARGS(&s_wavefrontDispatchRaysCommandSignature)));
  }
}

static void UploadWavefrontIndirectDispatchRecords(
    ID3D12GraphicsCommandList4 *list) {
  if (!list || !s_device || !s_wavefrontReservedBuffer ||
      s_wavefrontSecondaryRayGenShaderTable == 0 ||
      s_wavefrontShadowRayGenShaderTable == 0 || s_missShaderTable == 0 ||
      s_wavefrontHitGroupShaderTable == 0 ||
      s_wavefrontRayGenShaderTableEntrySize == 0 ||
      s_shaderTableEntrySize == 0) {
    return;
  }

  const UINT64 uploadSize = 2ull * kWavefrontDispatchRaysRecordStride;
  if (!s_wavefrontIndirectDispatchUploadBuffer) {
    D3D12_HEAP_PROPERTIES uploadProps = {};
    uploadProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC bufDesc = {};
    bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width = uploadSize;
    bufDesc.Height = 1;
    bufDesc.DepthOrArraySize = 1;
    bufDesc.MipLevels = 1;
    bufDesc.SampleDesc.Count = 1;
    bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ThrowIfFailed(s_device->CreateCommittedResource(
        &uploadProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&s_wavefrontIndirectDispatchUploadBuffer)));
    s_wavefrontIndirectDispatchUploadBuffer->SetName(
        L"Wavefront Indirect Dispatch Upload");
  }

  WavefrontDispatchRaysRecordGpu records[2] = {};
  auto initRecord = [&](WavefrontDispatchRaysRecordGpu &record,
                        D3D12_GPU_VIRTUAL_ADDRESS rayGenAddress) {
    record.desc.RayGenerationShaderRecord.StartAddress = rayGenAddress;
    record.desc.RayGenerationShaderRecord.SizeInBytes =
        s_wavefrontRayGenShaderTableEntrySize;
    record.desc.MissShaderTable.StartAddress = s_missShaderTable;
    record.desc.MissShaderTable.SizeInBytes = s_shaderTableEntrySize;
    record.desc.MissShaderTable.StrideInBytes = s_shaderTableEntrySize;
    record.desc.HitGroupTable.StartAddress = s_wavefrontHitGroupShaderTable;
    record.desc.HitGroupTable.SizeInBytes = s_shaderTableEntrySize;
    record.desc.HitGroupTable.StrideInBytes = s_shaderTableEntrySize;
    record.desc.CallableShaderTable.StartAddress = 0;
    record.desc.CallableShaderTable.SizeInBytes = 0;
    record.desc.CallableShaderTable.StrideInBytes = 0;
    record.desc.Width = 1;
    record.desc.Height = 1;
    record.desc.Depth = 1;
  };
  initRecord(records[0], s_wavefrontSecondaryRayGenShaderTable);
  initRecord(records[1], s_wavefrontShadowRayGenShaderTable);

  void *mapped = nullptr;
  D3D12_RANGE readRange = {0, 0};
  ThrowIfFailed(s_wavefrontIndirectDispatchUploadBuffer->Map(
      0, &readRange, &mapped));
  memcpy(mapped, records, sizeof(records));
  s_wavefrontIndirectDispatchUploadBuffer->Unmap(0, nullptr);

  D3D12_RESOURCE_BARRIER toCopy = {};
  toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  toCopy.Transition.pResource = s_wavefrontReservedBuffer.Get();
  toCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
  toCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  list->ResourceBarrier(1, &toCopy);
  list->CopyBufferRegion(s_wavefrontReservedBuffer.Get(), 0,
                         s_wavefrontIndirectDispatchUploadBuffer.Get(), 0,
                         sizeof(records));
  D3D12_RESOURCE_BARRIER toUav = toCopy;
  toUav.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  toUav.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  list->ResourceBarrier(1, &toUav);
}

static bool ExecuteWavefrontIndirectRayDispatch(
    ID3D12GraphicsCommandList4 *list, UINT recordOffsetBytes, bool doBarriers = true) {
  if (!list || !s_device || !s_wavefrontReservedBuffer) {
    return false;
  }

  EnsureWavefrontIndirectCommandSignatures();
  if (!s_wavefrontDispatchRaysCommandSignature) {
    return false;
  }

  D3D12_RESOURCE_BARRIER toIndirect = {};
  toIndirect.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  toIndirect.Transition.pResource = s_wavefrontReservedBuffer.Get();
  toIndirect.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  toIndirect.Transition.StateAfter = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
  toIndirect.Transition.Subresource =
      D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

  if (doBarriers) {
    list->ResourceBarrier(1, &toIndirect);
  }

  list->ExecuteIndirect(s_wavefrontDispatchRaysCommandSignature.Get(), 1,
                        s_wavefrontReservedBuffer.Get(), recordOffsetBytes,
                        nullptr, 0);

  if (doBarriers) {
    D3D12_RESOURCE_BARRIER toUav = toIndirect;
    toUav.Transition.StateBefore = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    toUav.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    list->ResourceBarrier(1, &toUav);
  }
  return true;
}

static bool ExecuteWavefrontIndirectComputeDispatch(
    ID3D12GraphicsCommandList4 *list, UINT dispatchArgsIndex, bool doBarriers = true) {
  if (!list || !s_device || !s_wavefrontDispatchArgsBuffer) {
    return false;
  }

  EnsureWavefrontIndirectCommandSignatures();
  if (!s_wavefrontDispatchCommandSignature) {
    return false;
  }

  D3D12_RESOURCE_BARRIER toIndirect = {};
  toIndirect.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  toIndirect.Transition.pResource = s_wavefrontDispatchArgsBuffer.Get();
  toIndirect.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  toIndirect.Transition.StateAfter = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
  toIndirect.Transition.Subresource =
      D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

  if (doBarriers) {
    list->ResourceBarrier(1, &toIndirect);
  }

  list->ExecuteIndirect(
      s_wavefrontDispatchCommandSignature.Get(), 1,
      s_wavefrontDispatchArgsBuffer.Get(),
      static_cast<UINT64>(dispatchArgsIndex) * sizeof(WavefrontDispatchArgsGpu),
      nullptr, 0);

  if (doBarriers) {
    D3D12_RESOURCE_BARRIER toUav = toIndirect;
    toUav.Transition.StateBefore = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    toUav.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    list->ResourceBarrier(1, &toUav);
  }
  return true;
}

static void DispatchWavefrontPrepareIndirectArgs(
    ID3D12GraphicsCommandList4 *list, UINT queueCounterIndex,
  UINT dispatchArgsIndex, UINT reservedSlotBase, UINT reservedFlags) {
  if (!list || !s_srvHeap || !s_device || s_lastWavefrontBootstrapPathCount == 0) {
    return;
  }

  EnsureWavefrontPrepareIndirectArgsPipeline();
  if (!s_wavefrontPrepareIndirectArgsPSO ||
      !s_wavefrontPrepareIndirectArgsRootSig) {
    return;
  }

  ID3D12DescriptorHeap *rtHeaps[] = {s_srvHeap.Get()};
  list->SetDescriptorHeaps(1, rtHeaps);
  list->SetPipelineState(s_wavefrontPrepareIndirectArgsPSO.Get());
  list->SetComputeRootSignature(s_wavefrontPrepareIndirectArgsRootSig.Get());

  const UINT maxDispatchCount =
      (queueCounterIndex == kWavefrontQueueShadow)
          ? static_cast<UINT>(
                std::min<UINT64>(s_wavefrontShadowQueueCapacity,
                                  (std::numeric_limits<UINT>::max)()))
          : static_cast<UINT>(
                std::min<UINT64>(s_wavefrontPathQueueCapacity,
                                  (std::numeric_limits<UINT>::max)()));
  const UINT prepConstants[5] = {queueCounterIndex, dispatchArgsIndex,
                                 reservedSlotBase, maxDispatchCount,
                                 reservedFlags};
  list->SetComputeRoot32BitConstants(0, _countof(prepConstants), prepConstants,
                                     0);
  list->SetComputeRootDescriptorTable(1, s_outputUAVGpu);
  list->SetComputeRootShaderResourceView(
      2, s_lightBuffer ? s_lightBuffer->GetGPUVirtualAddress() : 0);
  list->Dispatch(1, 1, 1);

  D3D12_RESOURCE_BARRIER uavBarrier = {};
  uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  uavBarrier.UAV.pResource = nullptr;
  list->ResourceBarrier(1, &uavBarrier);
}

static void DispatchWavefrontPrimaryVisibility(
    ID3D12GraphicsCommandList4 *list) {
  if (!list || s_wavefrontPrimaryRayGenShaderTable == 0 ||
      s_wavefrontRayGenShaderTableEntrySize == 0 || s_lastWavefrontBootstrapPathCount == 0) {
    return;
  }

  D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
  dispatchDesc.RayGenerationShaderRecord.StartAddress =
      s_wavefrontPrimaryRayGenShaderTable;
  dispatchDesc.RayGenerationShaderRecord.SizeInBytes =
      s_wavefrontRayGenShaderTableEntrySize;
  dispatchDesc.MissShaderTable.StartAddress = s_missShaderTable;
  dispatchDesc.MissShaderTable.SizeInBytes = s_shaderTableEntrySize;
  dispatchDesc.MissShaderTable.StrideInBytes = s_shaderTableEntrySize;
  dispatchDesc.HitGroupTable.StartAddress = s_wavefrontHitGroupShaderTable;
  dispatchDesc.HitGroupTable.SizeInBytes = s_shaderTableEntrySize;
  dispatchDesc.HitGroupTable.StrideInBytes = s_shaderTableEntrySize;
  dispatchDesc.Width = s_lastWavefrontBootstrapPathCount;
  dispatchDesc.Height = 1;
  dispatchDesc.Depth = 1;

  list->DispatchRays(&dispatchDesc);

  D3D12_RESOURCE_BARRIER uavBarrier = {};
  uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  uavBarrier.UAV.pResource = nullptr;
  list->ResourceBarrier(1, &uavBarrier);
}

static void DispatchWavefrontSecondaryVisibility(
  ID3D12GraphicsCommandList4 *list, UINT sourceQueueCounterIndex) {
  if (!list || s_wavefrontSecondaryRayGenShaderTable == 0 ||
      s_wavefrontRayGenShaderTableEntrySize == 0 || s_lastWavefrontBootstrapPathCount == 0) {
    return;
  }
  (void)sourceQueueCounterIndex;

  const bool dispatchedIndirect = ExecuteWavefrontIndirectRayDispatch(
      list, kWavefrontSecondaryDispatchRaysRecordOffset);

  if (!dispatchedIndirect) {
    // Fallback direct dispatch is intentionally queue-capacity sized.  The
    // raygen clamps against the live queue counter and buffer dimensions, so
    // this preserves correctness when glass split paths exceed primary count.
    const UINT fallbackWidth = static_cast<UINT>(
        std::min<UINT64>(s_wavefrontPathQueueCapacity,
                         (std::numeric_limits<UINT>::max)()));
    D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
    dispatchDesc.RayGenerationShaderRecord.StartAddress =
        s_wavefrontSecondaryRayGenShaderTable;
    dispatchDesc.RayGenerationShaderRecord.SizeInBytes =
        s_wavefrontRayGenShaderTableEntrySize;
    dispatchDesc.MissShaderTable.StartAddress = s_missShaderTable;
    dispatchDesc.MissShaderTable.SizeInBytes = s_shaderTableEntrySize;
    dispatchDesc.MissShaderTable.StrideInBytes = s_shaderTableEntrySize;
    dispatchDesc.HitGroupTable.StartAddress = s_wavefrontHitGroupShaderTable;
    dispatchDesc.HitGroupTable.SizeInBytes = s_shaderTableEntrySize;
    dispatchDesc.HitGroupTable.StrideInBytes = s_shaderTableEntrySize;
    dispatchDesc.Width = fallbackWidth;
    dispatchDesc.Height = 1;
    dispatchDesc.Depth = 1;

    list->DispatchRays(&dispatchDesc);
  }

  D3D12_RESOURCE_BARRIER uavBarrier = {};
  uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  uavBarrier.UAV.pResource = nullptr;
  list->ResourceBarrier(1, &uavBarrier);
}

static void DispatchWavefrontShadowVisibility(
    ID3D12GraphicsCommandList4 *list) {
  if (!list || s_wavefrontShadowRayGenShaderTable == 0 ||
      s_wavefrontRayGenShaderTableEntrySize == 0 || s_lastWavefrontBootstrapPathCount == 0) {
    return;
  }

  const bool dispatchedIndirect = ExecuteWavefrontIndirectRayDispatch(
      list, kWavefrontShadowDispatchRaysRecordOffset);

  if (!dispatchedIndirect) {
    D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
    dispatchDesc.RayGenerationShaderRecord.StartAddress =
        s_wavefrontShadowRayGenShaderTable;
    dispatchDesc.RayGenerationShaderRecord.SizeInBytes =
        s_wavefrontRayGenShaderTableEntrySize;
    dispatchDesc.MissShaderTable.StartAddress = s_missShaderTable;
    dispatchDesc.MissShaderTable.SizeInBytes = s_shaderTableEntrySize;
    dispatchDesc.MissShaderTable.StrideInBytes = s_shaderTableEntrySize;
    dispatchDesc.HitGroupTable.StartAddress = s_wavefrontHitGroupShaderTable;
    dispatchDesc.HitGroupTable.SizeInBytes = s_shaderTableEntrySize;
    dispatchDesc.HitGroupTable.StrideInBytes = s_shaderTableEntrySize;
    dispatchDesc.Width = static_cast<UINT>(
        std::min<UINT64>(s_wavefrontShadowQueueCapacity,
                         (std::numeric_limits<UINT>::max)()));
    dispatchDesc.Height = 1;
    dispatchDesc.Depth = 1;

    list->DispatchRays(&dispatchDesc);
  }

  D3D12_RESOURCE_BARRIER uavBarrier = {};
  uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  uavBarrier.UAV.pResource = nullptr;
  list->ResourceBarrier(1, &uavBarrier);
}

static void EnsureWavefrontResolvePipeline() {
  if (s_wavefrontResolvePSO && s_wavefrontResolveRootSig) {
    return;
  }
  if (!s_device) {
    return;
  }

  D3D12_DESCRIPTOR_RANGE uavRange = {};
  uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  uavRange.NumDescriptors = DXR_HEAP_UAV_COUNT; // u0..u33
  uavRange.BaseShaderRegister = 0;
  uavRange.RegisterSpace = 0;
  uavRange.OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE envSrvRange = {};
    envSrvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    envSrvRange.NumDescriptors = DXR_HEAP_ENV_SRV_COUNT; // t0..t2, space1
    envSrvRange.BaseShaderRegister = 0;
    envSrvRange.RegisterSpace = 1;
    envSrvRange.OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  D3D12_DESCRIPTOR_RANGE vbRange = {};
  vbRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  vbRange.NumDescriptors = 1024;
  vbRange.BaseShaderRegister = 2050;
  vbRange.RegisterSpace = 0;
  vbRange.OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  D3D12_DESCRIPTOR_RANGE ibRange = {};
  ibRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  ibRange.NumDescriptors = 1024;
  ibRange.BaseShaderRegister = 3074;
  ibRange.RegisterSpace = 0;
  ibRange.OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  D3D12_DESCRIPTOR_RANGE textureRange = {};
  textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  textureRange.NumDescriptors = 2048;
  textureRange.BaseShaderRegister = 1;
  textureRange.RegisterSpace = 0;
  textureRange.OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  D3D12_DESCRIPTOR_RANGE cloudSrvRange = {};
  cloudSrvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  cloudSrvRange.NumDescriptors = DXR_HEAP_CLOUD_SRV_COUNT; // t10..t12, space2
  cloudSrvRange.BaseShaderRegister = 10;
  cloudSrvRange.RegisterSpace = 2;
  cloudSrvRange.OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  D3D12_ROOT_PARAMETER params[14] = {};
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  params[0].Descriptor.ShaderRegister = 0;
  params[0].Descriptor.RegisterSpace = 0;
  params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  params[1].Constants.ShaderRegister = 1;
  params[1].Constants.RegisterSpace = 0;
  params[1].Constants.Num32BitValues = 4;
  params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[2].DescriptorTable.NumDescriptorRanges = 1;
  params[2].DescriptorTable.pDescriptorRanges = &uavRange;
  params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[3].DescriptorTable.NumDescriptorRanges = 1;
  params[3].DescriptorTable.pDescriptorRanges = &envSrvRange;
  params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  params[4].Descriptor.ShaderRegister = 5000;
  params[4].Descriptor.RegisterSpace = 0;
  params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  params[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  params[5].Descriptor.ShaderRegister = 0;
  params[5].Descriptor.RegisterSpace = 0;
  params[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  params[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  params[6].Descriptor.ShaderRegister = 2049;
  params[6].Descriptor.RegisterSpace = 0;
  params[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  params[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  params[7].Descriptor.ShaderRegister = 4098;
  params[7].Descriptor.RegisterSpace = 0;
  params[7].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  params[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  params[8].Descriptor.ShaderRegister = 4099;
  params[8].Descriptor.RegisterSpace = 0;
  params[8].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  params[9].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[9].DescriptorTable.NumDescriptorRanges = 1;
  params[9].DescriptorTable.pDescriptorRanges = &vbRange;
  params[9].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  params[10].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[10].DescriptorTable.NumDescriptorRanges = 1;
  params[10].DescriptorTable.pDescriptorRanges = &ibRange;
  params[10].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  params[11].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[11].DescriptorTable.NumDescriptorRanges = 1;
  params[11].DescriptorTable.pDescriptorRanges = &textureRange;
  params[11].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  params[12].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  params[12].Descriptor.ShaderRegister = 10;
  params[12].Descriptor.RegisterSpace = 2;
  params[12].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  params[13].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[13].DescriptorTable.NumDescriptorRanges = 1;
  params[13].DescriptorTable.pDescriptorRanges = &cloudSrvRange;
  params[13].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
  rsDesc.NumParameters = _countof(params);
  rsDesc.pParameters = params;
  rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

  D3D12_STATIC_SAMPLER_DESC linearSamplers[2] = {};
  linearSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  linearSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  linearSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  linearSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  linearSamplers[0].MipLODBias = 0;
  linearSamplers[0].MaxAnisotropy = 1;
  linearSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
  linearSamplers[0].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
  linearSamplers[0].MinLOD = 0.0f;
  linearSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
  linearSamplers[0].ShaderRegister = 0;
  linearSamplers[0].RegisterSpace = 0;
  linearSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  linearSamplers[1] = linearSamplers[0];
  linearSamplers[1].RegisterSpace = 2;

  rsDesc.NumStaticSamplers = _countof(linearSamplers);
  rsDesc.pStaticSamplers = linearSamplers;

  ComPtr<ID3DBlob> sig, err;
  HRESULT hrSerialize = D3D12SerializeRootSignature(
      &rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
  if (FAILED(hrSerialize)) {
    if (err) {
      fprintf(stderr, "DxrRenderer: Wavefront resolve RS error: %s\n",
              (char *)err->GetBufferPointer());
    }
    return;
  }

  ThrowIfFailed(s_device->CreateRootSignature(
      0, sig->GetBufferPointer(), sig->GetBufferSize(),
      IID_PPV_ARGS(&s_wavefrontResolveRootSig)));

  ComPtr<IDxcBlob> cs;
  try {
    std::vector<std::wstring> defines;
    cs = s_dxcHelper.Compile(L"shaders/wavefront_resolve_primary_cs.hlsl",
                             L"CSMain", L"cs_6_5", defines);
  } catch (const std::exception &e) {
    fprintf(stderr, "DxrRenderer: Wavefront resolve CS compile failed: %s\n",
            e.what());
    return;
  }
  if (!cs) {
    fprintf(stderr, "DxrRenderer: Wavefront resolve CS blob null\n");
    return;
  }

  D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature = s_wavefrontResolveRootSig.Get();
  psoDesc.CS.pShaderBytecode = cs->GetBufferPointer();
  psoDesc.CS.BytecodeLength = cs->GetBufferSize();
  HRESULT hrPso = s_device->CreateComputePipelineState(
      &psoDesc, IID_PPV_ARGS(&s_wavefrontResolvePSO));
  if (FAILED(hrPso)) {
    fprintf(stderr,
            "DxrRenderer: Wavefront resolve PSO creation failed: 0x%08x\n",
            (unsigned)hrPso);
    DumpD3D12InfoQueueMessages("Wavefront resolve PSO create");
    s_wavefrontResolvePSO.Reset();
    s_wavefrontResolveRootSig.Reset();
  }
}

static void EnsureWavefrontSecondaryResolvePipeline() {
  EnsureWavefrontResolvePipeline();
  if (!s_wavefrontResolveRootSig || s_wavefrontSecondaryResolvePSO) {
    return;
  }

  ComPtr<IDxcBlob> cs;
  try {
    std::vector<std::wstring> defines;
    cs = s_dxcHelper.Compile(L"shaders/wavefront_resolve_secondary_cs.hlsl",
                             L"CSMain", L"cs_6_5", defines);
  } catch (const std::exception &e) {
    fprintf(stderr,
            "DxrRenderer: Wavefront secondary resolve CS compile failed: %s\n",
            e.what());
    return;
  }
  if (!cs) {
    fprintf(stderr, "DxrRenderer: Wavefront secondary resolve CS blob null\n");
    return;
  }

  D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature = s_wavefrontResolveRootSig.Get();
  psoDesc.CS.pShaderBytecode = cs->GetBufferPointer();
  psoDesc.CS.BytecodeLength = cs->GetBufferSize();
  HRESULT hrPso = s_device->CreateComputePipelineState(
      &psoDesc, IID_PPV_ARGS(&s_wavefrontSecondaryResolvePSO));
  if (FAILED(hrPso)) {
    fprintf(stderr,
            "DxrRenderer: Wavefront secondary resolve PSO creation failed: 0x%08x\n",
            (unsigned)hrPso);
    DumpD3D12InfoQueueMessages("Wavefront secondary resolve PSO create");
    s_wavefrontSecondaryResolvePSO.Reset();
  }
}

static void EnsureWavefrontRestirSeedPipeline() {
  EnsureWavefrontResolvePipeline();
  if (!s_wavefrontResolveRootSig || s_wavefrontRestirSeedPSO) {
    return;
  }

  ComPtr<IDxcBlob> cs;
  try {
    std::vector<std::wstring> defines;
    cs = s_dxcHelper.Compile(L"shaders/wavefront_restir_seed_cs.hlsl",
                             L"CSMain", L"cs_6_5", defines);
  } catch (const std::exception &e) {
    fprintf(stderr,
            "DxrRenderer: Wavefront ReSTIR seed CS compile failed: %s\n",
            e.what());
    return;
  }
  if (!cs) {
    fprintf(stderr, "DxrRenderer: Wavefront ReSTIR seed CS blob null\n");
    return;
  }

  D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature = s_wavefrontResolveRootSig.Get();
  psoDesc.CS.pShaderBytecode = cs->GetBufferPointer();
  psoDesc.CS.BytecodeLength = cs->GetBufferSize();
  HRESULT hrPso = s_device->CreateComputePipelineState(
      &psoDesc, IID_PPV_ARGS(&s_wavefrontRestirSeedPSO));
  if (FAILED(hrPso)) {
    fprintf(stderr,
            "DxrRenderer: Wavefront ReSTIR seed PSO creation failed: "
            "0x%08x\n",
            (unsigned)hrPso);
    DumpD3D12InfoQueueMessages("Wavefront ReSTIR seed PSO create");
    s_wavefrontRestirSeedPSO.Reset();
  }
}

static void EnsureWavefrontShadowIntegratePipeline() {
  EnsureWavefrontResolvePipeline();
  if (!s_wavefrontResolveRootSig || s_wavefrontShadowIntegratePSO) {
    return;
  }

  ComPtr<IDxcBlob> cs;
  try {
    std::vector<std::wstring> defines;
    cs = s_dxcHelper.Compile(L"shaders/wavefront_integrate_shadow_cs.hlsl",
                             L"CSMain", L"cs_6_5", defines);
  } catch (const std::exception &e) {
    fprintf(stderr,
            "DxrRenderer: Wavefront shadow integrate CS compile failed: %s\n",
            e.what());
    return;
  }
  if (!cs) {
    fprintf(stderr, "DxrRenderer: Wavefront shadow integrate CS blob null\n");
    return;
  }

  D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature = s_wavefrontResolveRootSig.Get();
  psoDesc.CS.pShaderBytecode = cs->GetBufferPointer();
  psoDesc.CS.BytecodeLength = cs->GetBufferSize();
  HRESULT hrPso = s_device->CreateComputePipelineState(
      &psoDesc, IID_PPV_ARGS(&s_wavefrontShadowIntegratePSO));
  if (FAILED(hrPso)) {
    fprintf(stderr,
            "DxrRenderer: Wavefront shadow integrate PSO creation failed: "
            "0x%08x\n",
            (unsigned)hrPso);
    DumpD3D12InfoQueueMessages("Wavefront shadow integrate PSO create");
    s_wavefrontShadowIntegratePSO.Reset();
  }
}

static void EnsureWavefrontAccumulatePipeline() {
  EnsureWavefrontResolvePipeline();
  if (!s_wavefrontResolveRootSig || s_wavefrontAccumulatePSO) {
    return;
  }

  ComPtr<IDxcBlob> cs;
  try {
    std::vector<std::wstring> defines;
    cs = s_dxcHelper.Compile(L"shaders/wavefront_accumulate_cs.hlsl",
                             L"CSMain", L"cs_6_5", defines);
  } catch (const std::exception &e) {
    fprintf(stderr,
            "DxrRenderer: Wavefront accumulate CS compile failed: %s\n",
            e.what());
    return;
  }
  if (!cs) {
    fprintf(stderr, "DxrRenderer: Wavefront accumulate CS blob null\n");
    return;
  }

  D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature = s_wavefrontResolveRootSig.Get();
  psoDesc.CS.pShaderBytecode = cs->GetBufferPointer();
  psoDesc.CS.BytecodeLength = cs->GetBufferSize();
  HRESULT hrPso = s_device->CreateComputePipelineState(
      &psoDesc, IID_PPV_ARGS(&s_wavefrontAccumulatePSO));
  if (FAILED(hrPso)) {
    fprintf(stderr,
            "DxrRenderer: Wavefront accumulate PSO creation failed: "
            "0x%08x\n",
            (unsigned)hrPso);
    DumpD3D12InfoQueueMessages("Wavefront accumulate PSO create");
    s_wavefrontAccumulatePSO.Reset();
  }
}

static void DispatchWavefrontResolvePrimary(ID3D12GraphicsCommandList4 *list,
                                            ID3D12Resource *cameraCB,
                                            ID3D12Resource *materialCB,
                                            ID3D12Resource *meshDataSB,
                                            ID3D12Resource *materialExtraSB,
                                            UINT resolveFlags,
                                            bool useIndirectDispatch,
                                            UINT dispatchArgsIndex,
                                            bool doBarriers) {
  if (!list || !cameraCB || !s_srvHeap || !s_device ||
      s_lastWavefrontBootstrapPathCount == 0) {
    return;
  }

  EnsureWavefrontResolvePipeline();
  if (!s_wavefrontResolvePSO || !s_wavefrontResolveRootSig) {
    return;
  }

  ID3D12DescriptorHeap *rtHeaps[] = {s_srvHeap.Get()};
  list->SetDescriptorHeaps(1, rtHeaps);
  list->SetPipelineState(s_wavefrontResolvePSO.Get());
  list->SetComputeRootSignature(s_wavefrontResolveRootSig.Get());
  list->SetComputeRootConstantBufferView(0, cameraCB->GetGPUVirtualAddress());

  const UINT resolveConstants[4] = {s_outputWidth, s_outputHeight,
                                    s_lastWavefrontBootstrapPathCount,
                                    resolveFlags};
  list->SetComputeRoot32BitConstants(1, _countof(resolveConstants),
                                     resolveConstants, 0);
  list->SetComputeRootDescriptorTable(2, s_outputUAVGpu);
    list->SetComputeRootDescriptorTable(3, s_iblGpuHandle);
    list->SetComputeRootShaderResourceView(
      4, s_lightBuffer ? s_lightBuffer->GetGPUVirtualAddress() : 0);
  list->SetComputeRootShaderResourceView(
      5, s_tlas.result ? s_tlas.result->GetGPUVirtualAddress() : 0);
  list->SetComputeRootShaderResourceView(
      6, materialCB ? materialCB->GetGPUVirtualAddress() : 0);
  list->SetComputeRootShaderResourceView(
      7, meshDataSB ? meshDataSB->GetGPUVirtualAddress() : 0);
  list->SetComputeRootShaderResourceView(
      8, materialExtraSB ? materialExtraSB->GetGPUVirtualAddress() : 0);
  list->SetComputeRootDescriptorTable(9, s_vbTableGpu);
  list->SetComputeRootDescriptorTable(10, s_ibTableGpu);
  list->SetComputeRootDescriptorTable(11, s_texTableGpu);
  list->SetComputeRootConstantBufferView(12,
                                         g_cloudManager.GetConstantBufferAddr());
  D3D12_GPU_DESCRIPTOR_HANDLE cloudSRV =
      s_srvHeap->GetGPUDescriptorHandleForHeapStart();
  cloudSRV.ptr += (UINT64)DXR_HEAP_CLOUD_TEX_OFFSET *
                  s_device->GetDescriptorHandleIncrementSize(
                      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  list->SetComputeRootDescriptorTable(13, cloudSRV);

  const bool dispatchedIndirect =
      useIndirectDispatch &&
      ExecuteWavefrontIndirectComputeDispatch(
          list, dispatchArgsIndex, doBarriers);

  if (!dispatchedIndirect) {
    const UINT groupCountX = (s_lastWavefrontBootstrapPathCount + 63u) / 64u;
    list->Dispatch(groupCountX, 1, 1);
  }

  if (doBarriers) {
    D3D12_RESOURCE_BARRIER uavBarrier = {};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = nullptr;
    list->ResourceBarrier(1, &uavBarrier);
  }
}

static void DispatchWavefrontRestirSeed(ID3D12GraphicsCommandList4 *list,
                                        ID3D12Resource *cameraCB,
                                        UINT seedFlags,
                                        bool useIndirectDispatch,
                                        UINT dispatchArgsIndex,
                                        bool doBarriers) {
  if (!list || !cameraCB || !s_srvHeap || !s_device ||
      s_lastWavefrontBootstrapPathCount == 0) {
    return;
  }

  EnsureWavefrontRestirSeedPipeline();
  if (!s_wavefrontRestirSeedPSO || !s_wavefrontResolveRootSig) {
    return;
  }

  ID3D12DescriptorHeap *rtHeaps[] = {s_srvHeap.Get()};
  list->SetDescriptorHeaps(1, rtHeaps);
  list->SetPipelineState(s_wavefrontRestirSeedPSO.Get());
  list->SetComputeRootSignature(s_wavefrontResolveRootSig.Get());
  list->SetComputeRootConstantBufferView(0, cameraCB->GetGPUVirtualAddress());

  const UINT seedConstants[4] = {s_outputWidth, s_outputHeight,
                                 s_lastWavefrontBootstrapPathCount,
                                 seedFlags};
  list->SetComputeRoot32BitConstants(1, _countof(seedConstants),
                                     seedConstants, 0);
  list->SetComputeRootDescriptorTable(2, s_outputUAVGpu);
  list->SetComputeRootDescriptorTable(3, s_iblGpuHandle);
  list->SetComputeRootShaderResourceView(
      4, s_lightBuffer ? s_lightBuffer->GetGPUVirtualAddress() : 0);
  list->SetComputeRootShaderResourceView(
      5, s_tlas.result ? s_tlas.result->GetGPUVirtualAddress() : 0);
  list->SetComputeRootShaderResourceView(6, 0);
  list->SetComputeRootShaderResourceView(7, 0);
  list->SetComputeRootShaderResourceView(8, 0);
  list->SetComputeRootDescriptorTable(9, s_vbTableGpu);
  list->SetComputeRootDescriptorTable(10, s_ibTableGpu);
  list->SetComputeRootDescriptorTable(11, s_texTableGpu);
  list->SetComputeRootConstantBufferView(12,
                                         g_cloudManager.GetConstantBufferAddr());
  D3D12_GPU_DESCRIPTOR_HANDLE cloudSRV =
      s_srvHeap->GetGPUDescriptorHandleForHeapStart();
  cloudSRV.ptr += (UINT64)DXR_HEAP_CLOUD_TEX_OFFSET *
                  s_device->GetDescriptorHandleIncrementSize(
                      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  list->SetComputeRootDescriptorTable(13, cloudSRV);

  const bool dispatchedIndirect =
      useIndirectDispatch &&
      ExecuteWavefrontIndirectComputeDispatch(
          list, dispatchArgsIndex, doBarriers);

  if (!dispatchedIndirect) {
    const UINT groupCountX = (s_lastWavefrontBootstrapPathCount + 63u) / 64u;
    list->Dispatch(groupCountX, 1, 1);
  }

  if (doBarriers) {
    D3D12_RESOURCE_BARRIER uavBarrier = {};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = nullptr;
    list->ResourceBarrier(1, &uavBarrier);
  }
}

static void DispatchWavefrontResolveSecondary(ID3D12GraphicsCommandList4 *list,
                                              ID3D12Resource *cameraCB,
                                              UINT sourceQueueCounterIndex,
                                              UINT extraResolveFlags,
                                              bool useIndirectDispatch,
                                              UINT dispatchArgsIndex,
                                              bool doBarriers) {
  if (!list || !cameraCB || !s_srvHeap || !s_device ||
      s_lastWavefrontBootstrapPathCount == 0) {
    return;
  }

  EnsureWavefrontSecondaryResolvePipeline();
  if (!s_wavefrontSecondaryResolvePSO || !s_wavefrontResolveRootSig) {
    return;
  }

  ID3D12DescriptorHeap *rtHeaps[] = {s_srvHeap.Get()};
  list->SetDescriptorHeaps(1, rtHeaps);
  list->SetPipelineState(s_wavefrontSecondaryResolvePSO.Get());
  list->SetComputeRootSignature(s_wavefrontResolveRootSig.Get());
  list->SetComputeRootConstantBufferView(0, cameraCB->GetGPUVirtualAddress());

  const UINT sourceQueueFlags =
      (sourceQueueCounterIndex == 0u) ? kWavefrontQueueFlagSourceIsA : 0u;
  const UINT secondaryMaxDispatchCount = static_cast<UINT>(
      std::min<UINT64>(s_wavefrontPathQueueCapacity,
                       (std::numeric_limits<UINT>::max)()));
  const UINT resolveConstants[4] = {s_outputWidth, s_outputHeight,
                                    secondaryMaxDispatchCount,
                                    sourceQueueFlags | extraResolveFlags};
  list->SetComputeRoot32BitConstants(1, _countof(resolveConstants),
                                     resolveConstants, 0);
  list->SetComputeRootDescriptorTable(2, s_outputUAVGpu);
    list->SetComputeRootDescriptorTable(3, s_iblGpuHandle);
    list->SetComputeRootShaderResourceView(
      4, s_lightBuffer ? s_lightBuffer->GetGPUVirtualAddress() : 0);
  list->SetComputeRootShaderResourceView(
      5, s_tlas.result ? s_tlas.result->GetGPUVirtualAddress() : 0);
  list->SetComputeRootShaderResourceView(6, 0);
  list->SetComputeRootShaderResourceView(7, 0);
  list->SetComputeRootShaderResourceView(8, 0);
  list->SetComputeRootDescriptorTable(9, s_vbTableGpu);
  list->SetComputeRootDescriptorTable(10, s_ibTableGpu);
  list->SetComputeRootDescriptorTable(11, s_texTableGpu);
  list->SetComputeRootConstantBufferView(12,
                                         g_cloudManager.GetConstantBufferAddr());
  D3D12_GPU_DESCRIPTOR_HANDLE cloudSRV =
      s_srvHeap->GetGPUDescriptorHandleForHeapStart();
  cloudSRV.ptr += (UINT64)DXR_HEAP_CLOUD_TEX_OFFSET *
                  s_device->GetDescriptorHandleIncrementSize(
                      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  list->SetComputeRootDescriptorTable(13, cloudSRV);

  const bool dispatchedIndirect =
      useIndirectDispatch &&
      ExecuteWavefrontIndirectComputeDispatch(
          list, dispatchArgsIndex, doBarriers);

  if (!dispatchedIndirect) {
    const UINT groupCountX = (s_lastWavefrontBootstrapPathCount + 63u) / 64u;
    list->Dispatch(groupCountX, 1, 1);
  }

  if (doBarriers) {
    D3D12_RESOURCE_BARRIER uavBarrier = {};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = nullptr;
    list->ResourceBarrier(1, &uavBarrier);
  }
}

static void ClearWavefrontShadowContribution(
    ID3D12GraphicsCommandList4 *list) {
  if (!list || !s_srvHeap || !s_device || !s_wavefrontShadowContributionUAV) {
    return;
  }

  const UINT zeros[4] = {0u, 0u, 0u, 0u};
  UINT inc = s_device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle =
      s_srvHeap->GetCPUDescriptorHandleForHeapStart();
  cpuHandle.ptr +=
      (SIZE_T)DXR_HEAP_WAVEFRONT_SHADOW_CONTRIB_OFFSET * inc;
  list->ClearUnorderedAccessViewUint(
      s_wavefrontShadowContributionGpuHandle, cpuHandle,
      s_wavefrontShadowContributionUAV.Get(), zeros, 0, nullptr);

  D3D12_RESOURCE_BARRIER uavBarrier = {};
  uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  uavBarrier.UAV.pResource = s_wavefrontShadowContributionUAV.Get();
  list->ResourceBarrier(1, &uavBarrier);
}

static void DispatchWavefrontShadowIntegration(
    ID3D12GraphicsCommandList4 *list, ID3D12Resource *cameraCB,
    UINT integrateFlags = 0u) {
  if (!list || !cameraCB || !s_srvHeap || !s_device ||
      !s_wavefrontShadowContributionUAV || s_lastWavefrontBootstrapPathCount == 0) {
    return;
  }

  EnsureWavefrontShadowIntegratePipeline();
  if (!s_wavefrontShadowIntegratePSO || !s_wavefrontResolveRootSig) {
    return;
  }

  ID3D12DescriptorHeap *rtHeaps[] = {s_srvHeap.Get()};
  list->SetDescriptorHeaps(1, rtHeaps);
  list->SetPipelineState(s_wavefrontShadowIntegratePSO.Get());
  list->SetComputeRootSignature(s_wavefrontResolveRootSig.Get());
  list->SetComputeRootConstantBufferView(0, cameraCB->GetGPUVirtualAddress());

  const UINT integrateConstants[4] = {s_outputWidth, s_outputHeight,
                                      integrateFlags, 0u};
  list->SetComputeRoot32BitConstants(1, _countof(integrateConstants),
                                     integrateConstants, 0);
  list->SetComputeRootDescriptorTable(2, s_outputUAVGpu);

  const UINT groupCountX = (s_outputWidth + 7u) / 8u;
  const UINT groupCountY = (s_outputHeight + 7u) / 8u;
  list->Dispatch(groupCountX, groupCountY, 1);

  D3D12_RESOURCE_BARRIER uavBarrier = {};
  uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  uavBarrier.UAV.pResource = nullptr;
  list->ResourceBarrier(1, &uavBarrier);
}

static void DispatchWavefrontAccumulate(ID3D12GraphicsCommandList4 *list,
                                        ID3D12Resource *cameraCB) {
  if (!list || !cameraCB || !s_srvHeap || !s_device ||
      s_lastWavefrontBootstrapPathCount == 0) {
    return;
  }

  EnsureWavefrontAccumulatePipeline();
  if (!s_wavefrontAccumulatePSO || !s_wavefrontResolveRootSig) {
    return;
  }

  ID3D12DescriptorHeap *rtHeaps[] = {s_srvHeap.Get()};
  list->SetDescriptorHeaps(1, rtHeaps);
  list->SetPipelineState(s_wavefrontAccumulatePSO.Get());
  list->SetComputeRootSignature(s_wavefrontResolveRootSig.Get());
  list->SetComputeRootConstantBufferView(0, cameraCB->GetGPUVirtualAddress());

  const UINT accumulateConstants[4] = {s_outputWidth, s_outputHeight, 0u, 0u};
  list->SetComputeRoot32BitConstants(1, _countof(accumulateConstants),
                                     accumulateConstants, 0);
  list->SetComputeRootDescriptorTable(2, s_outputUAVGpu);

  const UINT groupCountX = (s_outputWidth + 7u) / 8u;
  const UINT groupCountY = (s_outputHeight + 7u) / 8u;
  list->Dispatch(groupCountX, groupCountY, 1);

  D3D12_RESOURCE_BARRIER uavBarrier = {};
  uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  uavBarrier.UAV.pResource = nullptr;
  list->ResourceBarrier(1, &uavBarrier);
}

static void PrepareWavefrontBackendPipelines() {
  if (!s_device) {
    return;
  }

  // Compile and create the queue-backed wavefront compute PSOs before the
  // first interactive camera invalidation. These Ensure* calls are cached, so
  // backend switches and pipeline recreates pay the one-time setup outside the
  // user's first movement frame.
  EnsureWavefrontBootstrapPipeline();
  EnsureWavefrontCounterResetPipeline();
  EnsureWavefrontPrepareIndirectArgsPipeline();
  EnsureWavefrontResolvePipeline();
  EnsureWavefrontIndirectCommandSignatures();

  if (s_pathTracingBackend ==
      DxrRenderer::PathTracingBackend::WavefrontOptimized) {
    EnsureWavefrontRestirSeedPipeline();
    EnsureWavefrontSecondaryResolvePipeline();
    EnsureWavefrontShadowIntegratePipeline();
    EnsureWavefrontAccumulatePipeline();
    EnsureRestirSpatialPipeline();
    EnsureRestirGiSpatialPipeline();
  }
}

static UINT ComputeWavefrontIndirectPassBudget() {
  const UINT maxSpecularBounces =
      (g_cameraData.maxSpecularBounces > 0.0f)
          ? static_cast<UINT>(g_cameraData.maxSpecularBounces)
          : 0u;
  const UINT maxRefractiveBounces =
      (g_cameraData.maxRefractiveBounces > 0.0f)
          ? static_cast<UINT>(g_cameraData.maxRefractiveBounces)
          : 0u;
  UINT maxGIBounces =
      (g_cameraData.maxGIBounces > 0.0f)
          ? static_cast<UINT>(g_cameraData.maxGIBounces)
          : 0u;
  // Bounces can alternate, we should allow enough passes to resolve the longest valid path.
  // The actual bounce checks are correctly enforced in the wave shaders.
  const UINT totalBudget =
      maxSpecularBounces + maxRefractiveBounces + maxGIBounces;
  return totalBudget;
}

static TextureStreamingPolicy ChooseAutoTextureStreamingPolicy() {
  // Simple adaptive policy based on measured GPU frame time.
  // 60 FPS target: favor quality under budget, trade mips when over budget.
  if (s_gpuFrameTimeMs > 26.0f) {
    return TextureStreamingPolicy::Aggressive;
  }
  if (s_gpuFrameTimeMs > 18.0f) {
    return TextureStreamingPolicy::Balanced;
  }
  return TextureStreamingPolicy::FullRes;
}

static UINT ComputeStreamingMostDetailedMip(const Asset::Texture &tex,
                                            TextureStreamingPolicy policy) {
  if (policy == TextureStreamingPolicy::FullRes || tex.mipLevels <= 1) {
    return 0;
  }

  const UINT maxDim = (tex.width > tex.height) ? tex.width : tex.height;
  UINT drop = 0;
  if (policy == TextureStreamingPolicy::Balanced) {
    if (maxDim >= 4096) {
      drop = 2;
    } else if (maxDim >= 2048) {
      drop = 1;
    }
  } else {
    if (maxDim >= 4096) {
      drop = 3;
    } else if (maxDim >= 2048) {
      drop = 2;
    } else if (maxDim >= 1024) {
      drop = 1;
    }
  }
  if (drop >= tex.mipLevels) {
    drop = tex.mipLevels - 1;
  }
  return drop;
}

static void
UpdateTextureDescriptorTable(D3D12_GPU_DESCRIPTOR_HANDLE texturesGpuStart,
                             UINT textureDescriptorCount) {
  if (!s_srvHeap || !s_device || textureDescriptorCount == 0) {
    return;
  }

  // Keep DXR material textures at the same most-detailed mip as raster.
  // Hidden frame-time based mip clamping makes imported DDS backdrops visibly
  // softer in DXR and breaks raster/DXR texture parity. If streaming returns,
  // it should be user-facing and opt-in.
  if (s_textureStreamingAuto) {
    TextureStreamingPolicy desired = ChooseAutoTextureStreamingPolicy();
    if (desired != s_textureStreamingPolicy) {
      s_textureStreamingPolicy = desired;
      s_textureTableDirty = true;
    }
  }

  static D3D12_GPU_DESCRIPTOR_HANDLE s_lastTexturesGpuStart = {0};
  static UINT s_lastTextureDescriptorCount = 0;
  static UINT s_lastRefreshFrame = 0;
  const bool sourceChanged =
      (texturesGpuStart.ptr != s_lastTexturesGpuStart.ptr) ||
      (textureDescriptorCount != s_lastTextureDescriptorCount);
  const bool policyChanged =
      (s_textureStreamingPolicy != s_lastAppliedTextureStreamingPolicy);
  const bool periodicRefresh = (s_jitterFrameIndex - s_lastRefreshFrame) > 120;

  if (!sourceChanged && !policyChanged && !s_textureTableDirty &&
      !periodicRefresh) {
    return;
  }

  const UINT descSize = s_device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  D3D12_CPU_DESCRIPTOR_HANDLE dst =
      s_srvHeap->GetCPUDescriptorHandleForHeapStart();
  dst.ptr += (SIZE_T)DXR_HEAP_TEX_OFFSET * descSize;

  const UINT maxCount = (textureDescriptorCount < DXR_HEAP_TEX_COUNT)
                            ? textureDescriptorCount
                            : DXR_HEAP_TEX_COUNT;
  const UINT availableTextures =
      (UINT)((g_loadedTextures.size() < maxCount) ? g_loadedTextures.size()
                                                  : maxCount);
  for (UINT i = 0; i < maxCount; ++i) {
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = dst;
    cpu.ptr += (SIZE_T)i * descSize;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;

    if (i < availableTextures && g_loadedTextures[(size_t)i].resource) {
      const Asset::Texture &tex = g_loadedTextures[(size_t)i];
      const UINT mostDetailedMip =
          ComputeStreamingMostDetailedMip(tex, s_textureStreamingPolicy);
      srvDesc.Format = tex.format;
      srvDesc.Texture2D.MostDetailedMip = mostDetailedMip;
      srvDesc.Texture2D.MipLevels = (mostDetailedMip < tex.mipLevels)
                                        ? (tex.mipLevels - mostDetailedMip)
                                        : 1u;
      srvDesc.Texture2D.ResourceMinLODClamp = (float)mostDetailedMip;
      s_device->CreateShaderResourceView(tex.resource.Get(), &srvDesc, cpu);
    } else {
      srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      srvDesc.Texture2D.MipLevels = 1;
      srvDesc.Texture2D.MostDetailedMip = 0;
      srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
      s_device->CreateShaderResourceView(nullptr, &srvDesc, cpu);
    }
  }

  s_lastTexturesGpuStart = texturesGpuStart;
  s_lastTextureDescriptorCount = textureDescriptorCount;
  s_lastAppliedTextureStreamingPolicy = s_textureStreamingPolicy;
  s_lastRefreshFrame = s_jitterFrameIndex;
  s_textureTableDirty = false;
}

static void EnsureNullCloudDescriptors() {
  if (!s_device || !s_srvHeap) {
    return;
  }

  const UINT descSize = s_device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  D3D12_CPU_DESCRIPTOR_HANDLE base =
      s_srvHeap->GetCPUDescriptorHandleForHeapStart();

  D3D12_SHADER_RESOURCE_VIEW_DESC volumeSrv = {};
  volumeSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  volumeSrv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  volumeSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
  volumeSrv.Texture3D.MipLevels = 1;
  volumeSrv.Texture3D.MostDetailedMip = 0;
  volumeSrv.Texture3D.ResourceMinLODClamp = 0.0f;

  D3D12_CPU_DESCRIPTOR_HANDLE cloudBase = base;
  cloudBase.ptr += (SIZE_T)DXR_HEAP_CLOUD_TEX_OFFSET * descSize;
  s_device->CreateShaderResourceView(nullptr, &volumeSrv, cloudBase);

  D3D12_CPU_DESCRIPTOR_HANDLE cloudDetail = base;
  cloudDetail.ptr += (SIZE_T)DXR_HEAP_CLOUD_DETAIL_TEX_OFFSET * descSize;
  s_device->CreateShaderResourceView(nullptr, &volumeSrv, cloudDetail);

  D3D12_SHADER_RESOURCE_VIEW_DESC bakedSrv = {};
  bakedSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  bakedSrv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  bakedSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  bakedSrv.Texture2D.MipLevels = 1;
  bakedSrv.Texture2D.MostDetailedMip = 0;
  bakedSrv.Texture2D.ResourceMinLODClamp = 0.0f;

  D3D12_CPU_DESCRIPTOR_HANDLE cloudBaked = base;
  cloudBaked.ptr += (SIZE_T)DXR_HEAP_CLOUD_BAKED_TEX_OFFSET * descSize;
  s_device->CreateShaderResourceView(nullptr, &bakedSrv, cloudBaked);
}

static UINT64 ReadbackUint64(ID3D12Resource *resource) {
  if (!resource) {
    return 0;
  }
  void *mapped = nullptr;
  if (FAILED(resource->Map(0, nullptr, &mapped)) || !mapped) {
    return 0;
  }
  UINT64 value = *((const UINT64 *)mapped);
  resource->Unmap(0, nullptr);
  return value;
}

static void EnsureNoiseStatsPipeline() {
  // We always need to ensure buffers are large enough – pipeline objects can
  // be reused, but the output/readback buffers may have to grow when the
  // render resolution changes.  Early-out only if everything is created and
  // capacity is nonzero; actual resizing happens later in the dispatch code.
  if (s_noiseStatsPSO && s_noiseStatsRootSig && s_noiseStatsCB &&
      s_noiseStatsOutputBuffer && s_noiseStatsCapacity > 0)
    return;
  if (!s_device)
    return;

  // Root signature: b0 (CB), u0(Tex), u1(Tex), u2(Buffer)
  D3D12_DESCRIPTOR_RANGE uavRanges[3];
  // u0 - Accumulation
  uavRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  uavRanges[0].NumDescriptors = 1;
  uavRanges[0].BaseShaderRegister = 0;
  uavRanges[0].RegisterSpace = 0;
  uavRanges[0].OffsetInDescriptorsFromTableStart = 0;

  // u1 - Variance
  uavRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  uavRanges[1].NumDescriptors = 1;
  uavRanges[1].BaseShaderRegister = 1;
  uavRanges[1].RegisterSpace = 0;
  uavRanges[1].OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  // u2 - Output Buffer
  uavRanges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  uavRanges[2].NumDescriptors = 1;
  uavRanges[2].BaseShaderRegister = 2;
  uavRanges[2].RegisterSpace = 0;
  uavRanges[2].OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  D3D12_ROOT_PARAMETER params[2] = {};
  // b0
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  params[0].Descriptor.ShaderRegister = 0;
  params[0].Descriptor.RegisterSpace = 0;
  params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  // table with u0, u1, u2
  params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[1].DescriptorTable.NumDescriptorRanges = 3;
  params[1].DescriptorTable.pDescriptorRanges = uavRanges;
  params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
  rsDesc.NumParameters = 2;
  rsDesc.pParameters = params;
  rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

  ComPtr<ID3DBlob> sig, err;
  if (FAILED(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                         &sig, &err))) {
    if (err)
      fprintf(stderr, "NoiseStats RS Error: %s\n",
              (char *)err->GetBufferPointer());
    return;
  }
  s_device->CreateRootSignature(0, sig->GetBufferPointer(),
                                sig->GetBufferSize(),
                                IID_PPV_ARGS(&s_noiseStatsRootSig));

  // Compile CS
  ComPtr<IDxcBlob> cs;
  try {
    std::vector<std::wstring> defines;
    cs = s_dxcHelper.Compile(L"shaders/noise_statistics_cs.hlsl", L"CSMain",
                             L"cs_6_3", defines);
  } catch (std::exception &e) {
    fprintf(stderr, "NoiseStats CS Compile Exception: %s\n", e.what());
    return;
  }

  if (!cs) {
    fprintf(stderr, "NoiseStats CS Compile Failed (null blob)\n");
    return;
  }

  D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature = s_noiseStatsRootSig.Get();
  psoDesc.CS.pShaderBytecode = cs->GetBufferPointer();
  psoDesc.CS.BytecodeLength = cs->GetBufferSize();
  s_device->CreateComputePipelineState(&psoDesc,
                                       IID_PPV_ARGS(&s_noiseStatsPSO));

  // Create CB
  D3D12_HEAP_PROPERTIES uploadProps = {};
  uploadProps.Type = D3D12_HEAP_TYPE_UPLOAD;
  D3D12_RESOURCE_DESC cbDesc = {};
  cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  cbDesc.Width = 256;
  cbDesc.Height = 1;
  cbDesc.DepthOrArraySize = 1;
  cbDesc.MipLevels = 1;
  cbDesc.SampleDesc.Count = 1;
  cbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  s_device->CreateCommittedResource(&uploadProps, D3D12_HEAP_FLAG_NONE, &cbDesc,
                                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                    IID_PPV_ARGS(&s_noiseStatsCB));

  // Create Output Buffer (UAV, Default Heap) - actual allocation deferred
  // until we know required sample count.  We'll create a minimal placeholder
  // here and grow it later when dispatching.
  D3D12_HEAP_PROPERTIES defaultProps = {};
  defaultProps.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC bufDesc = cbDesc;
  bufDesc.Width = sizeof(float) * 2; // two floats: sum and count
  bufDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  s_device->CreateCommittedResource(
      &defaultProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
      IID_PPV_ARGS(&s_noiseStatsOutputBuffer));
  s_noiseStatsCapacity = 1; // start with a single element to avoid zero-size

  // Create Readback Buffer (placeholder)
  D3D12_HEAP_PROPERTIES readbackProps = {};
  readbackProps.Type = D3D12_HEAP_TYPE_READBACK;
  bufDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
  s_device->CreateCommittedResource(&readbackProps, D3D12_HEAP_FLAG_NONE,
                                    &bufDesc, D3D12_RESOURCE_STATE_COPY_DEST,
                                    nullptr,
                                    IID_PPV_ARGS(&s_noiseStatsReadbackBuffer));

  // Descriptor Heap
  D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
  heapDesc.NumDescriptors = 3;
  heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  s_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&s_noiseStatsHeap));
}

static void EnsureAvgLumPipeline() {
  if (s_avgLumPSO && s_avgLumRootSig && s_avgLumCB && s_avgLumBuffer &&
      s_avgLumCapacity > 0)
    return;
  if (!s_device)
    return;

  // Root signature: b0 (CB), t0 (SRV), u0 (UAV)
  D3D12_DESCRIPTOR_RANGE srvRange{};
  srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  srvRange.NumDescriptors = 1;
  srvRange.BaseShaderRegister = 0;
  srvRange.RegisterSpace = 0;

  D3D12_DESCRIPTOR_RANGE uavRange{};
  uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  uavRange.NumDescriptors = 1;
  uavRange.BaseShaderRegister = 0;
  uavRange.RegisterSpace = 0;

  D3D12_ROOT_PARAMETER params[3] = {};
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  params[0].Descriptor.ShaderRegister = 0;
  params[0].Descriptor.RegisterSpace = 0;
  params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[1].DescriptorTable.NumDescriptorRanges = 1;
  params[1].DescriptorTable.pDescriptorRanges = &srvRange;
  params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[2].DescriptorTable.NumDescriptorRanges = 1;
  params[2].DescriptorTable.pDescriptorRanges = &uavRange;
  params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
  rsDesc.NumParameters = 3;
  rsDesc.pParameters = params;
  rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

  ComPtr<ID3DBlob> sig, err;
  if (FAILED(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                         &sig, &err))) {
    if (err)
      fprintf(stderr, "AvgLum RS Error: %s\n", (char *)err->GetBufferPointer());
    return;
  }
  s_device->CreateRootSignature(0, sig->GetBufferPointer(),
                                sig->GetBufferSize(),
                                IID_PPV_ARGS(&s_avgLumRootSig));

  // Compile CS
  ComPtr<IDxcBlob> cs;
  try {
    std::vector<std::wstring> defines;
    cs = s_dxcHelper.Compile(L"shaders/avg_luminance_cs.hlsl", L"CSMain",
                             L"cs_6_3", defines);
  } catch (std::exception &e) {
    fprintf(stderr, "AvgLum CS Compile Exception: %s\n", e.what());
    return;
  }

  if (!cs) {
    fprintf(stderr, "AvgLum CS Compile Failed (null blob)\n");
    return;
  }

  D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature = s_avgLumRootSig.Get();
  psoDesc.CS.pShaderBytecode = cs->GetBufferPointer();
  psoDesc.CS.BytecodeLength = cs->GetBufferSize();
  s_device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&s_avgLumPSO));

  // Create CB
  D3D12_HEAP_PROPERTIES uploadProps = {};
  uploadProps.Type = D3D12_HEAP_TYPE_UPLOAD;
  D3D12_RESOURCE_DESC cbDesc = {};
  cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  cbDesc.Width = 256;
  cbDesc.Height = 1;
  cbDesc.DepthOrArraySize = 1;
  cbDesc.MipLevels = 1;
  cbDesc.SampleDesc.Count = 1;
  cbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  s_device->CreateCommittedResource(&uploadProps, D3D12_HEAP_FLAG_NONE, &cbDesc,
                                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                    IID_PPV_ARGS(&s_avgLumCB));

  // Create Output Buffer (UAV, Default Heap)
  D3D12_HEAP_PROPERTIES defaultProps = {};
  defaultProps.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC bufDesc = cbDesc;
  bufDesc.Width = sizeof(float) * 2;
  bufDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  s_device->CreateCommittedResource(&defaultProps, D3D12_HEAP_FLAG_NONE,
                                    &bufDesc,
                                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                    nullptr, IID_PPV_ARGS(&s_avgLumBuffer));
  s_avgLumCapacity = 1;

  // Create Readback Buffer
  D3D12_HEAP_PROPERTIES readbackProps = {};
  readbackProps.Type = D3D12_HEAP_TYPE_READBACK;
  bufDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
  s_device->CreateCommittedResource(&readbackProps, D3D12_HEAP_FLAG_NONE,
                                    &bufDesc, D3D12_RESOURCE_STATE_COPY_DEST,
                                    nullptr,
                                    IID_PPV_ARGS(&s_avgLumReadbackBuffer));

  // Descriptor Heap
  D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
  heapDesc.NumDescriptors = 2; // SRV, UAV
  heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  s_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&s_avgLumHeap));
}

void Initialize(ID3D12Device *device) {
  s_device = device;
  if (!s_device) {
    g_rayTracingSupported = false;
    return;
  }
  ComPtr<ID3D12Device5> dev5;
  if (SUCCEEDED(s_device->QueryInterface(IID_PPV_ARGS(&dev5)))) {
    g_rayTracingSupported = true;
    s_dxrDevice = dev5;
    s_accumulation.Initialize(s_device, s_outputWidth, s_outputHeight);
    s_transmissionAccumulation.Initialize(s_device, s_outputWidth,
                                          s_outputHeight);
    fprintf(stderr, "DxrRenderer: DXR supported on device\n");
  } else {
    g_rayTracingSupported = false;
    s_dxrDevice.Reset();
  }
}

void SetCommandQueue(ID3D12CommandQueue *commandQueue, ID3D12Fence *fence,
                     UINT64 *fenceValues, UINT *frameIndexPtr,
                     HANDLE fenceEvent) {
  s_commandQueue = commandQueue;
  s_fence = fence;
  s_fenceValues = fenceValues;
  s_frameIndexPtr = frameIndexPtr;
  s_fenceEvent = fenceEvent;

  if (s_asyncComputeFenceEvent) {
    CloseHandle(s_asyncComputeFenceEvent);
    s_asyncComputeFenceEvent = nullptr;
  }
  s_asyncComputeQueue.Reset();
  s_asyncDirectFence.Reset();
  s_asyncComputeFence.Reset();
  s_asyncComputeAllocator.Reset();
  s_asyncComputeList.Reset();
  s_asyncRestirPending = false;
  s_asyncRestirCameraCB.Reset();
  s_asyncComputePendingFenceWait = 0;
  s_asyncRestirAvailable = false;
  s_asyncRestirInitTried = false;
  EnsureAsyncComputeContext();
}

static void EnsureCurrentFeatureResources() {
  const uint32_t desiredMask = ComputeResourceFeatureMask();
  if (desiredMask == s_resourceFeatureMask) {
    return;
  }

  if (g_verboseRenderLogs) {
    fprintf(stderr,
            "DxrRenderer: Feature mask changed (old=0x%X new=0x%X), "
            "recreating feature-owned DXR resources.\n",
            s_resourceFeatureMask, desiredMask);
  }

  CreateRayTracingPipeline(s_presentWidth, s_presentHeight);
}

void CreateRayTracingPipeline(UINT width, UINT height) {
  if (!g_rayTracingSupported || !s_dxrDevice)
    return;

  WaitForAsyncRestirIdle();

  // Track requested output (swapchain) size.
  if (width > 0)
    s_presentWidth = width;
  if (height > 0)
    s_presentHeight = height;

  const UINT outW = s_presentWidth;
  const UINT outH = s_presentHeight;
  const uint32_t resourceFeatureMask = ComputeResourceFeatureMask();
  const bool needsDepthAndMotionBuffers =
      NeedsDepthAndMotionBuffers(resourceFeatureMask);
  const bool needsSurfaceDataBuffers =
      NeedsSurfaceDataBuffers(resourceFeatureMask);
  const bool needsLinearDepthBuffer =
      NeedsLinearDepthBuffer(resourceFeatureMask);
  const bool needsSpecularAuxBuffers =
      NeedsSpecularAuxBuffers(resourceFeatureMask);
  const bool needsDlssOutputBuffer =
      NeedsDlssOutputBuffer(resourceFeatureMask);
  const bool needsOidnOutputBuffer =
      NeedsOidnOutputBuffer(resourceFeatureMask);

  const uint32_t previousFeatureMask = s_resourceFeatureMask;
  if (s_streamline && previousFeatureMask != resourceFeatureMask) {
    const bool wasDlss = (previousFeatureMask & ResourceFeature_Dlss) != 0;
    const bool wantsDlss = (resourceFeatureMask & ResourceFeature_Dlss) != 0;
    const bool wasRr =
        (previousFeatureMask & ResourceFeature_DlssRayReconstruction) != 0;
    const bool wantsRr =
        (resourceFeatureMask & ResourceFeature_DlssRayReconstruction) != 0;

    if (wasDlss && (!wantsDlss || wantsRr)) {
      s_streamline->ReleaseResourcesForMode(
          StreamlineManager::Mode::DLSS_SuperResolution);
    }
    if (wasRr && !wantsRr) {
      s_streamline->ReleaseResourcesForMode(
          StreamlineManager::Mode::DLSS_RayReconstruction);
    }
  }

  // Compute internal render size (DLSS wants us to render smaller and upscale).
  UINT renderW = outW;
  UINT renderH = outH;
  if (IsDlssActive()) {
    auto rec = s_streamline->GetRecommendedRenderSize(outW, outH);
    if (rec.renderWidth > 0 && rec.renderHeight > 0) {
      renderW = rec.renderWidth;
      renderH = rec.renderHeight;
    }
  }

  // Update module-local render size so Dispatch uses correct dimensions.
  s_outputWidth = renderW;
  s_outputHeight = renderH;

  // Resize accumulation buffer to match new render size
  s_accumulation.Resize(s_outputWidth, s_outputHeight);
  s_transmissionAccumulation.Resize(s_outputWidth, s_outputHeight);
  s_textureTableDirty = true;
  s_cloudDescriptorsDone = false;
  s_asyncRestirPending = false;
  s_asyncRestirCameraCB.Reset();

  if (g_verboseRenderLogs) {
    fprintf(stderr,
            "DxrRenderer: Creating Ray Tracing Pipeline (size=%u x %u, "
            "features=0x%X)...\n",
            s_outputWidth, s_outputHeight, resourceFeatureMask);
  }

  // Create a large shader-visible heap for all DXR resources early,
  // so that BuildAccelerationStructures doesn't crash if shader compile fails.
  if (!s_srvHeap) {
    s_cloudDescriptorsDone = false;
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = DXR_HEAP_TOTAL_COUNT;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    HRESULT hrHeap =
        s_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&s_srvHeap));
    if (FAILED(hrHeap)) {
      fprintf(stderr, "DxrRenderer: Failed to create DXR SRV heap: 0x%08x\n",
              (unsigned)hrHeap);
      return;
    }

    UINT descSize = s_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_GPU_DESCRIPTOR_HANDLE gpuStart =
        s_srvHeap->GetGPUDescriptorHandleForHeapStart();
    s_texTableGpu.ptr = gpuStart.ptr + (UINT64)DXR_HEAP_TEX_OFFSET * descSize;
    s_vbTableGpu.ptr = gpuStart.ptr + (UINT64)DXR_HEAP_VB_OFFSET * descSize;
    s_ibTableGpu.ptr = gpuStart.ptr + (UINT64)DXR_HEAP_IB_OFFSET * descSize;
    s_outputUAVGpu.ptr = gpuStart.ptr + (UINT64)DXR_HEAP_UAV_OFFSET * descSize;
    s_accumUAVGpu.ptr =
        gpuStart.ptr + (UINT64)DXR_HEAP_ACCUM_UAV_OFFSET * descSize;
    s_varianceUAVGpu.ptr =
        gpuStart.ptr + (UINT64)DXR_HEAP_VARIANCE_UAV_OFFSET * descSize;
    s_transmissionAccumUAVGpu.ptr =
        gpuStart.ptr + (UINT64)DXR_HEAP_TRANSMISSION_ACCUM_OFFSET * descSize;
    s_transmissionVarianceUAVGpu.ptr =
        gpuStart.ptr +
        (UINT64)DXR_HEAP_TRANSMISSION_VARIANCE_OFFSET * descSize;
    s_reservoirGpuHandle[0].ptr =
        gpuStart.ptr + (UINT64)DXR_HEAP_RESERVOIR_0_OFFSET * descSize;
    s_reservoirGpuHandle[1].ptr =
        gpuStart.ptr + (UINT64)DXR_HEAP_RESERVOIR_1_OFFSET * descSize;
    s_gi_reservoirGpuHandle[0].ptr =
        gpuStart.ptr + (UINT64)DXR_HEAP_GI_RESERVOIR_0_OFFSET_A * descSize;
    s_gi_reservoirGpuHandle[1].ptr =
        gpuStart.ptr + (UINT64)DXR_HEAP_GI_RESERVOIR_0_OFFSET_B * descSize;
    s_gi_reservoirGpuHandle[2].ptr =
        gpuStart.ptr + (UINT64)DXR_HEAP_GI_RESERVOIR_0_OFFSET_C * descSize;
    s_gi_reservoirGpuHandle[3].ptr =
        gpuStart.ptr + (UINT64)DXR_HEAP_GI_RESERVOIR_1_OFFSET_A * descSize;
    s_gi_reservoirGpuHandle[4].ptr =
        gpuStart.ptr + (UINT64)DXR_HEAP_GI_RESERVOIR_1_OFFSET_B * descSize;
    s_gi_reservoirGpuHandle[5].ptr =
        gpuStart.ptr + (UINT64)DXR_HEAP_GI_RESERVOIR_1_OFFSET_C * descSize;
    s_iblGpuHandle.ptr = gpuStart.ptr + (UINT64)DXR_HEAP_IBL_OFFSET * descSize;
    s_shaderCountersGpuHandle.ptr =
        gpuStart.ptr + (UINT64)DXR_HEAP_SHADER_COUNTERS_OFFSET * descSize;
  }
  EnsureNullCloudDescriptors();

  // Compile shader
  ComPtr<IDxcBlob> shaderBlob;
  try {
    std::vector<std::wstring> compileDefines;
#ifdef _DEBUG
    compileDefines.push_back(L"SHADER_ENABLE_DEBUG=1");
    if (::g_dxrHitDebug)
      compileDefines.push_back(L"HIT_DEBUG=1");
#else
    compileDefines.push_back(L"SHADER_ENABLE_DEBUG=0");
#endif
    shaderBlob = s_dxcHelper.Compile(L"shaders/raytracing.hlsl", L"",
                                     L"lib_6_5", compileDefines);
  } catch (const std::exception &e) {
    fprintf(stderr, "DxrRenderer: Shader Compilation Failed: %s\n", e.what());
    return;
  }
  if (!shaderBlob) {
    fprintf(stderr, "DxrRenderer: shader blob null\n");
    return;
  }

  // Create global root signature
  D3D12_ROOT_PARAMETER params[15] =
      {}; // Increased for Lights, material extras, grass data, and cloud resources
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  params[0].Descriptor.ShaderRegister = 0;
  params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_DESCRIPTOR_RANGE uavRange = {};
  uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  uavRange.NumDescriptors = DXR_HEAP_UAV_COUNT; // u0..u32
  uavRange.BaseShaderRegister = 0;
  params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[1].DescriptorTable.NumDescriptorRanges = 1;
  params[1].DescriptorTable.pDescriptorRanges = &uavRange;
  params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  // Texture Table (t1 onwards)
  static D3D12_DESCRIPTOR_RANGE texRange = {};
  texRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  texRange.NumDescriptors = 2048;
  texRange.BaseShaderRegister = 1;
  texRange.OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
  params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[2].DescriptorTable.NumDescriptorRanges = 1;
  params[2].DescriptorTable.pDescriptorRanges = &texRange;
  params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  params[3].Descriptor.ShaderRegister = 0;
  params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  params[4].Descriptor.ShaderRegister = 2049;
  params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  // Environment Descriptor Table (t0..t2, space1): env map + importance CDFs
  static D3D12_DESCRIPTOR_RANGE envRange = {};
  envRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  envRange.NumDescriptors = 3;
  envRange.BaseShaderRegister = 0;
  envRange.RegisterSpace = 1;
  envRange.OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
  params[8].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[8].DescriptorTable.NumDescriptorRanges = 1;
  params[8].DescriptorTable.pDescriptorRanges = &envRange;
  params[8].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  // Vertex Buffer Table (t2050 onwards)
  static D3D12_DESCRIPTOR_RANGE vbRange = {};
  vbRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  vbRange.NumDescriptors = 1024;
  vbRange.BaseShaderRegister = 2050;
  vbRange.OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
  params[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[5].DescriptorTable.NumDescriptorRanges = 1;
  params[5].DescriptorTable.pDescriptorRanges = &vbRange;
  params[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  // Index Buffer Table (t3074 onwards)
  static D3D12_DESCRIPTOR_RANGE ibRange = {};
  ibRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  ibRange.NumDescriptors = 1024;
  ibRange.BaseShaderRegister = 3074;
  ibRange.OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
  params[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[6].DescriptorTable.NumDescriptorRanges = 1;
  params[6].DescriptorTable.pDescriptorRanges = &ibRange;
  params[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  // Mesh Data SB (t4098 onwards)
  params[7].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  params[7].Descriptor.ShaderRegister = 4098;
  params[7].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  // Material Extra Data SB (t4099)
  params[12].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  params[12].Descriptor.ShaderRegister = 4099;
  params[12].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  // Grass blade instance buffer (t4100)
  params[13].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  params[13].Descriptor.ShaderRegister = 4100;
  params[13].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  // Grass TLAS append start index (b11)
  params[14].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  params[14].Constants.ShaderRegister = 11;
  params[14].Constants.RegisterSpace = 0;
  params[14].Constants.Num32BitValues = 1;
  params[14].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  // Lights SB (t5000)
  params[9].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  params[9].Descriptor.ShaderRegister = 5000;
  params[9].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  // Cloud Table (b10, t10) - Split into Root CBV (10) and Table (11)
  // Range 2: SRV t10, t11
  static D3D12_DESCRIPTOR_RANGE cloudSrvRange = {};
  cloudSrvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  cloudSrvRange.NumDescriptors = 3; // base, detail, baked
  cloudSrvRange.BaseShaderRegister = 10;
  cloudSrvRange.RegisterSpace = 2; // Space 2
  cloudSrvRange.OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  // Slot 10: Root CBV (b10, space2)
  params[10].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  params[10].Descriptor.ShaderRegister = 10;
  params[10].Descriptor.RegisterSpace = 2;
  params[10].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  // Slot 11: SRV Table (t10, t11, space2)
  params[11].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[11].DescriptorTable.NumDescriptorRanges = 1;
  params[11].DescriptorTable.pDescriptorRanges = &cloudSrvRange;
  params[11].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
  rootDesc.NumParameters = 15;
  rootDesc.pParameters = params;
  rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

  static D3D12_STATIC_SAMPLER_DESC staticSamplers[3] = {};
  // s0: Aniso Wrap (space 0)
  staticSamplers[0].Filter = D3D12_FILTER_ANISOTROPIC;
  staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  staticSamplers[0].MipLODBias = 0;
  staticSamplers[0].MaxAnisotropy = 16;
  staticSamplers[0].ShaderRegister = 0;
  staticSamplers[0].RegisterSpace = 0;
  staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  // s10: Linear Wrap (for 3D Noise) in space 0
  staticSamplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  staticSamplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  staticSamplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  staticSamplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  staticSamplers[1].MipLODBias = 0;
  staticSamplers[1].MaxAnisotropy = 1;
  staticSamplers[1].ShaderRegister = 10;
  staticSamplers[1].RegisterSpace = 0;
  staticSamplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  // s0 in space 2: Linear Wrap sampler for clouds (space2)
  staticSamplers[2].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  staticSamplers[2].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  staticSamplers[2].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  staticSamplers[2].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  staticSamplers[2].MipLODBias = 0;
  staticSamplers[2].MaxAnisotropy = 1;
  staticSamplers[2].ShaderRegister = 0;
  staticSamplers[2].RegisterSpace = 2;
  staticSamplers[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  rootDesc.NumStaticSamplers = _countof(staticSamplers);
  rootDesc.pStaticSamplers = staticSamplers;

  ComPtr<ID3DBlob> signature;
  ComPtr<ID3DBlob> error;
  HRESULT hrSerialize = D3D12SerializeRootSignature(
      &rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
  if (FAILED(hrSerialize)) {
    if (error)
      fprintf(stderr, "DxrRenderer: Root signature error: %s\n",
              (char *)error->GetBufferPointer());
    return;
  }
  HRESULT hrCreate = s_device->CreateRootSignature(
      0, signature->GetBufferPointer(), signature->GetBufferSize(),
      IID_PPV_ARGS(&s_rtGlobalRootSignature));
  if (FAILED(hrCreate)) {
    fprintf(stderr, "DxrRenderer: CreateRootSignature failed: 0x%08x\n",
            (unsigned)hrCreate);
    return;
  }

  // Create state object (DXIL lib etc.)
  static D3D12_DXIL_LIBRARY_DESC libDesc = {};
  static D3D12_EXPORT_DESC exports[6] = {};
  static D3D12_HIT_GROUP_DESC hitGroupDesc = {};
  static D3D12_RAYTRACING_SHADER_CONFIG shaderConfig = {};
  static D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig = {};
  static D3D12_GLOBAL_ROOT_SIGNATURE globalRootSigDesc = {};

  libDesc.DXILLibrary.pShaderBytecode = shaderBlob->GetBufferPointer();
  libDesc.DXILLibrary.BytecodeLength = shaderBlob->GetBufferSize();
  exports[0].Name = L"WavefrontPrimaryRayGen";
  exports[1].Name = L"WavefrontSecondaryRayGen";
  exports[2].Name = L"WavefrontShadowRayGen";
  exports[3].Name = L"Miss";
  exports[4].Name = L"AnyHit";
  exports[5].Name = L"WavefrontClosestHit";
  libDesc.NumExports = 6;
  libDesc.pExports = exports;
  D3D12_STATE_SUBOBJECT libSub = {};
  libSub.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
  libSub.pDesc = &libDesc;

  hitGroupDesc.HitGroupExport = L"WavefrontHitGroup";
  hitGroupDesc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
  hitGroupDesc.ClosestHitShaderImport = L"WavefrontClosestHit";
  hitGroupDesc.AnyHitShaderImport = L"AnyHit";
  D3D12_STATE_SUBOBJECT hitSub = {};
  hitSub.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
  hitSub.pDesc = &hitGroupDesc;

  constexpr UINT kRayPayloadSizeInBytes =
      sizeof(float) + 8u * sizeof(uint32_t) + 4u * sizeof(float);
  fprintf(stderr, "DxrRenderer: MaxPayloadSizeInBytes=%u\n",
          kRayPayloadSizeInBytes);
  shaderConfig.MaxPayloadSizeInBytes = kRayPayloadSizeInBytes;
  shaderConfig.MaxAttributeSizeInBytes = 2 * sizeof(float);
  D3D12_STATE_SUBOBJECT shaderConfigSub = {};
  shaderConfigSub.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
  shaderConfigSub.pDesc = &shaderConfig;

  pipelineConfig.MaxTraceRecursionDepth = 4;
  D3D12_STATE_SUBOBJECT pipeConfigSub = {};
  pipeConfigSub.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
  pipeConfigSub.pDesc = &pipelineConfig;

  globalRootSigDesc.pGlobalRootSignature = s_rtGlobalRootSignature.Get();
  D3D12_STATE_SUBOBJECT rootSigSub = {};
  rootSigSub.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
  rootSigSub.pDesc = &globalRootSigDesc;

  std::vector<D3D12_STATE_SUBOBJECT> subobjects;
  subobjects.push_back(libSub);
  subobjects.push_back(hitSub);
  subobjects.push_back(shaderConfigSub);
  subobjects.push_back(pipeConfigSub);
  subobjects.push_back(rootSigSub);

  D3D12_STATE_OBJECT_DESC stateObjDesc = {};
  stateObjDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
  stateObjDesc.NumSubobjects = (UINT)subobjects.size();
  stateObjDesc.pSubobjects = subobjects.data();

  HRESULT hrState = s_dxrDevice->CreateStateObject(
      &stateObjDesc, IID_PPV_ARGS(&s_rtStateObject));
  if (FAILED(hrState)) {
    fprintf(stderr, "DxrRenderer: CreateStateObject failed: 0x%08x\n",
            (unsigned)hrState);
    fprintf(stderr,
            "DxrRenderer: RT pipeline config dump: payload=%u attr=%u "
            "recursion=%u rootParams=%u\n",
            shaderConfig.MaxPayloadSizeInBytes,
            shaderConfig.MaxAttributeSizeInBytes,
            pipelineConfig.MaxTraceRecursionDepth, rootDesc.NumParameters);
    if (g_dxrDumpD3D12Messages) {
      ComPtr<ID3D12InfoQueue> infoQueue;
      if (SUCCEEDED(s_device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
        const UINT64 n =
            infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();
        for (UINT64 i = 0; i < n; ++i) {
          SIZE_T messageLength = 0;
          infoQueue->GetMessage(i, nullptr, &messageLength);
          std::vector<char> message(messageLength);
          D3D12_MESSAGE *pMsg =
              reinterpret_cast<D3D12_MESSAGE *>(message.data());
          if (SUCCEEDED(infoQueue->GetMessage(i, pMsg, &messageLength))) {
            fprintf(stderr,
                    "D3D12 INFO (CreateStateObject): Cat=%d Sev=%d ID=%d: %s\n",
                    (int)pMsg->Category, (int)pMsg->Severity, (int)pMsg->ID,
                    pMsg->pDescription);
          }
        }
      }
    }
    return;
  }

  // Create Shader Table
  ComPtr<ID3D12StateObjectProperties> properties;
  ThrowIfFailed(s_rtStateObject.As(&properties));
  void *wavefrontRayGenId =
      properties->GetShaderIdentifier(L"WavefrontPrimaryRayGen");
  void *wavefrontSecondaryRayGenId =
      properties->GetShaderIdentifier(L"WavefrontSecondaryRayGen");
  void *wavefrontShadowRayGenId =
      properties->GetShaderIdentifier(L"WavefrontShadowRayGen");
  void *missId = properties->GetShaderIdentifier(L"Miss");
  void *wavefrontHitGroupId =
      properties->GetShaderIdentifier(L"WavefrontHitGroup");
  if (!wavefrontRayGenId || !wavefrontSecondaryRayGenId ||
      !wavefrontShadowRayGenId || !missId || !wavefrontHitGroupId) {
    fprintf(stderr, "DxrRenderer: Shader IDs null\n");
    return;
  }
  UINT shaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
  // RayGen record must be aligned to 64 bytes (shader table alignment),
  // miss/hit records must be aligned to 32 bytes (shader record alignment).
  UINT64 s_rayGenEntrySize =
      Align(shaderIdentifierSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
  s_wavefrontRayGenShaderTableEntrySize = s_rayGenEntrySize;
  s_shaderTableEntrySize = Align(shaderIdentifierSize,
                                 D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
  // Total SBT size: 3 wavefront raygen records + miss + wavefront hit.
  UINT64 shaderTableSize =
      (3 * s_rayGenEntrySize) + (2 * s_shaderTableEntrySize);
  AllocateUploadBuffer(s_device, nullptr, shaderTableSize, &s_sbtStorage,
                       L"Shader Table");
  UINT8 *pData = nullptr;
  s_sbtStorage->Map(0, nullptr, (void **)&pData);
  memcpy(pData, wavefrontRayGenId, shaderIdentifierSize);
  memcpy(pData + s_rayGenEntrySize, wavefrontSecondaryRayGenId,
         shaderIdentifierSize);
  memcpy(pData + (2 * s_rayGenEntrySize), wavefrontShadowRayGenId,
         shaderIdentifierSize);
  memcpy(pData + (3 * s_rayGenEntrySize), missId, shaderIdentifierSize);
  memcpy(pData + (3 * s_rayGenEntrySize) + s_shaderTableEntrySize,
         wavefrontHitGroupId, shaderIdentifierSize);
  s_sbtStorage->Unmap(0, nullptr);
  D3D12_GPU_VIRTUAL_ADDRESS baseAddr = s_sbtStorage->GetGPUVirtualAddress();
  s_wavefrontPrimaryRayGenShaderTable = baseAddr;
  s_wavefrontSecondaryRayGenShaderTable = baseAddr + s_rayGenEntrySize;
  s_wavefrontShadowRayGenShaderTable = baseAddr + (2 * s_rayGenEntrySize);
  s_missShaderTable = baseAddr + (3 * s_rayGenEntrySize);
  s_wavefrontHitGroupShaderTable = s_missShaderTable + s_shaderTableEntrySize;
  s_uploadedWavefrontSecondaryRayGen = 0;
  s_uploadedWavefrontShadowRayGen = 0;

  // Create a default heap 2D texture to hold raytracing output (render-size)
  D3D12_RESOURCE_DESC texDesc = {};
  texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  texDesc.Alignment = 0;
  texDesc.Width = s_outputWidth;
  texDesc.Height = s_outputHeight;
  texDesc.DepthOrArraySize = 1;
  texDesc.MipLevels = 1;
  // Render output is linear HDR (pre-tonemap / pre-DLSS).
  texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
  texDesc.SampleDesc.Count = 1;
  texDesc.SampleDesc.Quality = 0;
  texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

  D3D12_HEAP_PROPERTIES heapProps = {};
  heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
  // If an output texture already exists, release it so we recreate with correct
  // size
  s_outputUAV.Reset();
  s_depthUAV.Reset();
  s_mvecUAV.Reset();
  s_albedoUAV.Reset();
  s_linearDepthUAV.Reset();
  s_specularAlbedoUAV.Reset();
  s_specHitDistanceUAV.Reset();
  s_specularMotionVectorsUAV.Reset();
  s_normalRoughnessUAV.Reset();
  s_wavefrontShadowContributionUAV.Reset();
  s_dlssOutputUAV.Reset();
  s_tonemapOutputUAV.Reset();
  // Enable SHARED flag for OIDN interop (and potentially DLSS/Streamline)
  ThrowIfFailed(s_device->CreateCommittedResource(
      &heapProps, D3D12_HEAP_FLAG_SHARED, &texDesc,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
      IID_PPV_ARGS(&s_outputUAV)));
  if (s_outputUAV)
    s_outputUAV->SetName(L"RT Output Texture");

  D3D12_RESOURCE_DESC outDesc = texDesc;
  outDesc.Width = outW;
  outDesc.Height = outH;

  auto CreateUavTexture =
      [&](ComPtr<ID3D12Resource> &out, const D3D12_RESOURCE_DESC &baseDesc,
          DXGI_FORMAT format, const wchar_t *name, bool shared = false) {
        D3D12_RESOURCE_DESC desc = baseDesc;
        desc.Format = format;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        ThrowIfFailed(s_device->CreateCommittedResource(
            &heapProps, shared ? D3D12_HEAP_FLAG_SHARED : D3D12_HEAP_FLAG_NONE,
            &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
            IID_PPV_ARGS(&out)));
        if (out)
          out->SetName(name);
      };

  // DXR auxiliary inputs used by DLSS and denoisers.
  if (needsDepthAndMotionBuffers) {
    CreateUavTexture(s_depthUAV, texDesc, DXGI_FORMAT_R32_FLOAT, L"RT Depth");
    CreateUavTexture(s_mvecUAV, texDesc, DXGI_FORMAT_R16G16_FLOAT,
                     L"RT Motion Vectors");
  }
  if (needsSurfaceDataBuffers) {
    CreateUavTexture(s_albedoUAV, texDesc, DXGI_FORMAT_R16G16B16A16_FLOAT,
                     L"RT Albedo", true); // Shared for OIDN
    CreateUavTexture(s_normalRoughnessUAV, texDesc,
                     DXGI_FORMAT_R16G16B16A16_FLOAT, L"RT NormalRoughness",
                     true); // Shared for OIDN
  }
  if (needsSpecularAuxBuffers) {
    CreateUavTexture(s_specularAlbedoUAV, texDesc,
                     DXGI_FORMAT_R16G16B16A16_FLOAT, L"RT Specular Albedo");
    CreateUavTexture(s_specHitDistanceUAV, texDesc, DXGI_FORMAT_R32_FLOAT,
                     L"RT Specular HitDistance");
    CreateUavTexture(s_specularMotionVectorsUAV, texDesc,
                     DXGI_FORMAT_R16G16_FLOAT, L"RT Specular MotionVectors");
  }
  {
    const UINT64 shadowContributionElements =
        std::max<UINT64>(1ull, (UINT64)s_outputWidth * s_outputHeight * 4ull);
    D3D12_RESOURCE_DESC shadowDesc = {};
    shadowDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    shadowDesc.Alignment = 0;
    shadowDesc.Width = shadowContributionElements * sizeof(UINT);
    shadowDesc.Height = 1;
    shadowDesc.DepthOrArraySize = 1;
    shadowDesc.MipLevels = 1;
    shadowDesc.Format = DXGI_FORMAT_UNKNOWN;
    shadowDesc.SampleDesc.Count = 1;
    shadowDesc.SampleDesc.Quality = 0;
    shadowDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    shadowDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ThrowIfFailed(s_device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &shadowDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
        IID_PPV_ARGS(&s_wavefrontShadowContributionUAV)));
    if (s_wavefrontShadowContributionUAV) {
      s_wavefrontShadowContributionUAV->SetName(
          L"Wavefront Shadow Contributions");
    }
  }

  // DLSS output is output-size in linear HDR (pre-tonemap).
  if (needsDlssOutputBuffer) {
    CreateUavTexture(s_dlssOutputUAV, outDesc, DXGI_FORMAT_R16G16B16A16_FLOAT,
                     L"RT DLSS Output (HDR)");
  }

  // OIDN output (HDR) - same format as DLSS output for tonemapping.
  if (needsOidnOutputBuffer) {
    CreateUavTexture(s_oidnOutputUAV, outDesc, DXGI_FORMAT_R16G16B16A16_FLOAT,
                     L"RT OIDN Output (HDR)", true); // Shared for OIDN
  }

  // Tonemap output is swapchain-format (output-size) for easy CopyResource.
  // Not shared (only used by internal CS and Copy)
  CreateUavTexture(s_tonemapOutputUAV, outDesc, DXGI_FORMAT_R10G10B10A2_UNORM,
                   L"RT Tonemap Output");
  if (needsLinearDepthBuffer) {
    CreateUavTexture(s_linearDepthUAV, texDesc, DXGI_FORMAT_R32_FLOAT,
                     L"RT Linear Depth");
  }

  D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
  uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
  uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  uavDesc.Texture2D.MipSlice = 0;
  uavDesc.Texture2D.PlaneSlice = 0;

  // Create UAV at its slot
  D3D12_CPU_DESCRIPTOR_HANDLE uavCpu =
      s_srvHeap->GetCPUDescriptorHandleForHeapStart();
  uavCpu.ptr +=
      (SIZE_T)DXR_HEAP_UAV_OFFSET * s_device->GetDescriptorHandleIncrementSize(
                                        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  s_device->CreateUnorderedAccessView(s_outputUAV.Get(), nullptr, &uavDesc,
                                      uavCpu);

  auto CreateUavAt = [&](ID3D12Resource *res, DXGI_FORMAT fmt,
                         UINT heapOffset) {
    D3D12_UNORDERED_ACCESS_VIEW_DESC d = {};
    d.Format = fmt;
    d.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    d.Texture2D.MipSlice = 0;
    d.Texture2D.PlaneSlice = 0;

    D3D12_CPU_DESCRIPTOR_HANDLE h =
        s_srvHeap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += (SIZE_T)heapOffset * s_device->GetDescriptorHandleIncrementSize(
                                      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    s_device->CreateUnorderedAccessView(res, nullptr, &d, h);
  };

  auto CreateStructuredUavAt = [&](ID3D12Resource *res, UINT numElements,
                                   UINT strideBytes, UINT heapOffset) {
    D3D12_UNORDERED_ACCESS_VIEW_DESC d = {};
    d.Format = DXGI_FORMAT_UNKNOWN;
    d.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    d.Buffer.FirstElement = 0;
    d.Buffer.NumElements = numElements;
    d.Buffer.StructureByteStride = strideBytes;
    d.Buffer.CounterOffsetInBytes = 0;
    d.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

    D3D12_CPU_DESCRIPTOR_HANDLE h =
        s_srvHeap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += (SIZE_T)heapOffset * s_device->GetDescriptorHandleIncrementSize(
                                      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    s_device->CreateUnorderedAccessView(res, nullptr, &d, h);
  };

  // u10+
  CreateUavAt(s_depthUAV.Get(), DXGI_FORMAT_R32_FLOAT,
              DXR_HEAP_DEPTH_UAV_OFFSET);
  CreateUavAt(s_mvecUAV.Get(), DXGI_FORMAT_R16G16_FLOAT,
              DXR_HEAP_MVEC_UAV_OFFSET);
  CreateUavAt(s_specularAlbedoUAV.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT,
              DXR_HEAP_SPEC_ALBEDO_OFFSET);
  CreateUavAt(s_specHitDistanceUAV.Get(), DXGI_FORMAT_R32_FLOAT,
              DXR_HEAP_SPEC_HITDIST_OFFSET);
  CreateUavAt(s_specularMotionVectorsUAV.Get(), DXGI_FORMAT_R16G16_FLOAT,
              DXR_HEAP_SPEC_MVEC_OFFSET);
  CreateUavAt(s_albedoUAV.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT,
              DXR_HEAP_ALBEDO_UAV_OFFSET);
  CreateUavAt(s_normalRoughnessUAV.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT,
              DXR_HEAP_NORMAL_ROUGHNESS_UAV_OFFSET);
  CreateUavAt(s_dlssOutputUAV.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT,
              DXR_HEAP_DLSS_OUT_UAV_OFFSET);
  CreateUavAt(s_linearDepthUAV.Get(), DXGI_FORMAT_R32_FLOAT,
              DXR_HEAP_LINEAR_DEPTH_UAV_OFFSET);
  CreateUavAt(s_oidnOutputUAV.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT,
              DXR_HEAP_OIDN_OUT_UAV_OFFSET);
  const UINT shadowContributionElements =
      (UINT)std::min<UINT64>(
          std::max<UINT64>(1ull,
                           (UINT64)s_outputWidth * s_outputHeight * 4ull),
          0xFFFFFFFFull);
  CreateStructuredUavAt(s_wavefrontShadowContributionUAV.Get(),
                        shadowContributionElements, sizeof(UINT),
                        DXR_HEAP_WAVEFRONT_SHADOW_CONTRIB_OFFSET);
  s_wavefrontShadowContributionGpuHandle = s_outputUAVGpu;
  s_wavefrontShadowContributionGpuHandle.ptr +=
      (UINT64)23 *
      (UINT64)s_device->GetDescriptorHandleIncrementSize(
          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  // Prepare tonemap pipeline resources.
  EnsureTonemapPipeline();
  s_resourceFeatureMask = resourceFeatureMask;
  PrepareWavefrontBackendPipelines();
  PrepareSelectedFinalDenoiserResources();

  // Create Accumulation UAV
  s_accumulation.Resize(s_outputWidth, s_outputHeight);
  s_transmissionAccumulation.Resize(s_outputWidth, s_outputHeight);
  D3D12_CPU_DESCRIPTOR_HANDLE accumUavCpu =
      s_srvHeap->GetCPUDescriptorHandleForHeapStart();
  accumUavCpu.ptr += (SIZE_T)DXR_HEAP_ACCUM_UAV_OFFSET *
                     s_device->GetDescriptorHandleIncrementSize(
                         D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  D3D12_UNORDERED_ACCESS_VIEW_DESC accumUavDesc = {};
  accumUavDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
  accumUavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  accumUavDesc.Texture2D.MipSlice = 0;
  accumUavDesc.Texture2D.PlaneSlice = 0;
  s_device->CreateUnorderedAccessView(s_accumulation.GetAccumulationBuffer(),
                                      nullptr, &accumUavDesc, accumUavCpu);

  // Create Variance UAV (for Noise Calculation / Adaptive Sampling)
  D3D12_CPU_DESCRIPTOR_HANDLE varUavCpu =
      s_srvHeap->GetCPUDescriptorHandleForHeapStart();
  varUavCpu.ptr += (SIZE_T)DXR_HEAP_VARIANCE_UAV_OFFSET *
                   s_device->GetDescriptorHandleIncrementSize(
                       D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  D3D12_UNORDERED_ACCESS_VIEW_DESC varUavDesc = {};
  varUavDesc.Format = DXGI_FORMAT_R32_FLOAT;
  varUavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  varUavDesc.Texture2D.MipSlice = 0;
  varUavDesc.Texture2D.PlaneSlice = 0;
  s_device->CreateUnorderedAccessView(s_accumulation.GetVarianceBuffer(),
                                      nullptr, &varUavDesc, varUavCpu);

  D3D12_CPU_DESCRIPTOR_HANDLE transAccumUavCpu =
      s_srvHeap->GetCPUDescriptorHandleForHeapStart();
  transAccumUavCpu.ptr +=
      (SIZE_T)DXR_HEAP_TRANSMISSION_ACCUM_OFFSET *
      s_device->GetDescriptorHandleIncrementSize(
          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  s_device->CreateUnorderedAccessView(
      s_transmissionAccumulation.GetAccumulationBuffer(), nullptr,
      &accumUavDesc, transAccumUavCpu);

  D3D12_CPU_DESCRIPTOR_HANDLE transVarUavCpu =
      s_srvHeap->GetCPUDescriptorHandleForHeapStart();
  transVarUavCpu.ptr +=
      (SIZE_T)DXR_HEAP_TRANSMISSION_VARIANCE_OFFSET *
      s_device->GetDescriptorHandleIncrementSize(
          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  s_device->CreateUnorderedAccessView(
      s_transmissionAccumulation.GetVarianceBuffer(), nullptr, &varUavDesc,
      transVarUavCpu);

  // Create Reservoir UAVs
  for (int i = 0; i < 2; ++i) {
    D3D12_RESOURCE_DESC resDesc = texDesc;
    resDesc.Format =
        DXGI_FORMAT_R32G32B32A32_FLOAT; // Reservoirs need 16 bytes: index,
                                        // w_sum, M, W
    s_reservoirBuffers[i].Reset();
    ThrowIfFailed(s_device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
        IID_PPV_ARGS(&s_reservoirBuffers[i])));

    D3D12_CPU_DESCRIPTOR_HANDLE resUavCpu =
        s_srvHeap->GetCPUDescriptorHandleForHeapStart();
    resUavCpu.ptr += (SIZE_T)(i == 0 ? DXR_HEAP_RESERVOIR_0_OFFSET
                                     : DXR_HEAP_RESERVOIR_1_OFFSET) *
                     s_device->GetDescriptorHandleIncrementSize(
                         D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_UNORDERED_ACCESS_VIEW_DESC resUavDesc = {};
    resUavDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    resUavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    resUavDesc.Texture2D.MipSlice = 0;
    s_device->CreateUnorderedAccessView(s_reservoirBuffers[i].Get(), nullptr,
                                        &resUavDesc, resUavCpu);
  }

  // Create GI Reservoir UAVs (3 per frame for ping-ponging, 2 frames total = 6
  // textures)
  for (int i = 0; i < 6; ++i) {
    D3D12_RESOURCE_DESC resDesc = texDesc;
    resDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    s_gi_reservoirBuffers[i].Reset();
    ThrowIfFailed(s_device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
        IID_PPV_ARGS(&s_gi_reservoirBuffers[i])));

    D3D12_CPU_DESCRIPTOR_HANDLE resUavCpu =
        s_srvHeap->GetCPUDescriptorHandleForHeapStart();
    UINT offset = 0;
    switch (i) {
    case 0:
      offset = DXR_HEAP_GI_RESERVOIR_0_OFFSET_A;
      break;
    case 1:
      offset = DXR_HEAP_GI_RESERVOIR_0_OFFSET_B;
      break;
    case 2:
      offset = DXR_HEAP_GI_RESERVOIR_0_OFFSET_C;
      break;
    case 3:
      offset = DXR_HEAP_GI_RESERVOIR_1_OFFSET_A;
      break;
    case 4:
      offset = DXR_HEAP_GI_RESERVOIR_1_OFFSET_B;
      break;
    case 5:
      offset = DXR_HEAP_GI_RESERVOIR_1_OFFSET_C;
      break;
    }

    resUavCpu.ptr +=
        (SIZE_T)offset * s_device->GetDescriptorHandleIncrementSize(
                             D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_UNORDERED_ACCESS_VIEW_DESC resUavDesc = {};
    resUavDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    resUavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    resUavDesc.Texture2D.MipSlice = 0;
    s_device->CreateUnorderedAccessView(s_gi_reservoirBuffers[i].Get(), nullptr,
                                        &resUavDesc, resUavCpu);
  }

  // Create a small GPU buffer for shader instrumentation counters (u24)
  {
    const UINT kNumCounters = 16;
    D3D12_RESOURCE_DESC bufDesc = {};
    bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Alignment = 0;
    bufDesc.Width = sizeof(UINT) * kNumCounters;
    bufDesc.Height = 1;
    bufDesc.DepthOrArraySize = 1;
    bufDesc.MipLevels = 1;
    bufDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufDesc.SampleDesc.Count = 1;
    bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    ThrowIfFailed(s_device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
        IID_PPV_ARGS(&s_shaderCountersBuffer)));
    s_shaderCountersBuffer->SetName(L"Shader Counters Buffer");

    // Create UAV descriptor for counters in global DXR heap (u24)
    D3D12_UNORDERED_ACCESS_VIEW_DESC bufUav = {};
    bufUav.Format = DXGI_FORMAT_UNKNOWN;
    bufUav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    bufUav.Buffer.FirstElement = 0;
    bufUav.Buffer.NumElements = kNumCounters;
    bufUav.Buffer.StructureByteStride = sizeof(UINT);
    bufUav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle =
        s_srvHeap->GetCPUDescriptorHandleForHeapStart();
    cpuHandle.ptr += (SIZE_T)DXR_HEAP_SHADER_COUNTERS_OFFSET *
                     s_device->GetDescriptorHandleIncrementSize(
                         D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    s_device->CreateUnorderedAccessView(s_shaderCountersBuffer.Get(), nullptr,
                                        &bufUav, cpuHandle);

    // Readback buffer (host-readable)
    D3D12_RESOURCE_DESC readDesc = {};
    readDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readDesc.Alignment = 0;
    readDesc.Width = sizeof(UINT) * kNumCounters;
    readDesc.Height = 1;
    readDesc.DepthOrArraySize = 1;
    readDesc.MipLevels = 1;
    readDesc.Format = DXGI_FORMAT_UNKNOWN;
    readDesc.SampleDesc.Count = 1;
    readDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    readDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES readbackHeap = {};
    readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
    ThrowIfFailed(s_device->CreateCommittedResource(
        &readbackHeap, D3D12_HEAP_FLAG_NONE, &readDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&s_shaderCountersReadbackBuffer)));
    s_shaderCountersReadbackBuffer->SetName(L"Shader Counters Readback");
  }

    // Reserve the unused UAV tail for the queue-driven wavefront backend.
    // These buffers are not consumed yet, but keeping them in the main DXR heap
    // stabilizes the ABI before the first queue bootstrap pass lands.
    {
    auto CreateStructuredBufferUav =
      [&](ComPtr<ID3D12Resource> &buffer, UINT64 elementCount,
        UINT structureStride, UINT heapOffset, const wchar_t *name) {
        D3D12_RESOURCE_DESC bufDesc = {};
        bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufDesc.Alignment = 0;
        bufDesc.Width = std::max<UINT64>(
          static_cast<UINT64>(structureStride), elementCount * structureStride);
        bufDesc.Height = 1;
        bufDesc.DepthOrArraySize = 1;
        bufDesc.MipLevels = 1;
        bufDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufDesc.SampleDesc.Count = 1;
        bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        bufDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        buffer.Reset();
        ThrowIfFailed(s_device->CreateCommittedResource(
          &heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
          D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
          IID_PPV_ARGS(&buffer)));
        buffer->SetName(name);

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = static_cast<UINT>(elementCount);
        uavDesc.Buffer.StructureByteStride = structureStride;
        uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

        D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle =
          s_srvHeap->GetCPUDescriptorHandleForHeapStart();
        cpuHandle.ptr +=
          static_cast<SIZE_T>(heapOffset) *
          s_device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        s_device->CreateUnorderedAccessView(buffer.Get(), nullptr, &uavDesc,
                          cpuHandle);
      };

    s_wavefrontPathQueueCapacity = ComputeWavefrontQueueCapacity(
      s_outputWidth, s_outputHeight, kWavefrontMaxPathQueueEntries,
      kWavefrontPathQueueMultiplier);
    s_wavefrontHitQueueCapacity = ComputeWavefrontQueueCapacity(
      s_outputWidth, s_outputHeight, kWavefrontMaxPathQueueEntries,
      kWavefrontPathQueueMultiplier);
    // Secondary resolve can enqueue sun, local-light, and environment
    // visibility tasks for each active path. If this queue overflows, atomic
    // allocation keeps a nondeterministic subset of tasks, which reads as
    // random lighting/reservoir flicker while accumulation continues.
    s_wavefrontShadowQueueCapacity = ComputeWavefrontQueueCapacity(
      s_outputWidth, s_outputHeight, kWavefrontMaxShadowQueueEntries,
      kWavefrontShadowQueueMultiplier);

    CreateStructuredBufferUav(s_wavefrontQueueCountersBuffer,
                  kWavefrontQueueCounterCount, sizeof(UINT),
                  DXR_HEAP_WAVEFRONT_COUNTERS_OFFSET,
                  L"Wavefront Queue Counters");
    CreateStructuredBufferUav(s_wavefrontPathQueueABuffer,
                  s_wavefrontPathQueueCapacity,
                  sizeof(WavefrontPathStateGpu),
                  DXR_HEAP_WAVEFRONT_PATH_A_OFFSET,
                  L"Wavefront Path Queue A");
    CreateStructuredBufferUav(s_wavefrontPathQueueBBuffer,
                  s_wavefrontPathQueueCapacity,
                  sizeof(WavefrontPathStateGpu),
                  DXR_HEAP_WAVEFRONT_PATH_B_OFFSET,
                  L"Wavefront Path Queue B");
    CreateStructuredBufferUav(s_wavefrontHitQueueBuffer,
                  s_wavefrontHitQueueCapacity,
                  sizeof(WavefrontHitRecordGpu),
                  DXR_HEAP_WAVEFRONT_HIT_OFFSET,
                  L"Wavefront Hit Queue");
    CreateStructuredBufferUav(s_wavefrontShadowQueueBuffer,
                  s_wavefrontShadowQueueCapacity,
                  sizeof(WavefrontShadowTaskGpu),
                  DXR_HEAP_WAVEFRONT_SHADOW_OFFSET,
                  L"Wavefront Shadow Queue");
    CreateStructuredBufferUav(s_wavefrontDispatchArgsBuffer,
                  kWavefrontDispatchArgCount,
                  sizeof(WavefrontDispatchArgsGpu),
                  DXR_HEAP_WAVEFRONT_DISPATCH_ARGS_OFFSET,
                  L"Wavefront Dispatch Args");
    CreateStructuredBufferUav(s_wavefrontStatsBuffer, kWavefrontStatsCount,
                  sizeof(UINT), DXR_HEAP_WAVEFRONT_STATS_OFFSET,
                  L"Wavefront Stats");
    {
      D3D12_RESOURCE_DESC readDesc = {};
      readDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
      readDesc.Alignment = 0;
      readDesc.Width = sizeof(UINT) * kWavefrontStatsCount;
      readDesc.Height = 1;
      readDesc.DepthOrArraySize = 1;
      readDesc.MipLevels = 1;
      readDesc.Format = DXGI_FORMAT_UNKNOWN;
      readDesc.SampleDesc.Count = 1;
      readDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
      readDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

      D3D12_HEAP_PROPERTIES readbackHeap = {};
      readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
      s_wavefrontStatsReadbackBuffer.Reset();
      ThrowIfFailed(s_device->CreateCommittedResource(
          &readbackHeap, D3D12_HEAP_FLAG_NONE, &readDesc,
          D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
          IID_PPV_ARGS(&s_wavefrontStatsReadbackBuffer)));
      s_wavefrontStatsReadbackBuffer->SetName(L"Wavefront Stats Readback");
    }
    CreateStructuredBufferUav(s_wavefrontReservedBuffer,
                  kWavefrontReservedUint4Count,
                  sizeof(uint32_t) * 4,
                  DXR_HEAP_WAVEFRONT_RESERVED_OFFSET,
                  L"Wavefront Reserved Buffer");
    CreateStructuredBufferUav(s_wavefrontMaterialBinIndicesBuffer,
                  s_wavefrontPathQueueCapacity * kWavefrontMaterialBinCount,
                  sizeof(UINT),
                  DXR_HEAP_WAVEFRONT_BIN_INDICES_OFFSET,
                  L"Wavefront Material Bin Indices");

    if (g_verboseRenderLogs) {
      fprintf(stderr,
          "DxrRenderer: Wavefront scaffolding buffers allocated "
          "(path=%llu hit=%llu shadow=%llu)\n",
          static_cast<unsigned long long>(s_wavefrontPathQueueCapacity),
          static_cast<unsigned long long>(s_wavefrontHitQueueCapacity),
          static_cast<unsigned long long>(s_wavefrontShadowQueueCapacity));
    }
    }

  // Create query heap for GPU profiling
  D3D12_QUERY_HEAP_DESC queryHeapDesc = {};
  queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
  queryHeapDesc.Count = 10; // frame_start, restir_start, restir_end,
                            // dispatch_start, dispatch_end, denoise_start,
                            // denoise_end, noise_start, noise_end, frame_end
  ThrowIfFailed(
      s_device->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&s_queryHeap)));

  // Create readback buffer for query results
  D3D12_RESOURCE_DESC readbackDesc = {};
  readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  readbackDesc.Alignment = 0;
  readbackDesc.Width = sizeof(UINT64) * 10;
  readbackDesc.Height = 1;
  readbackDesc.DepthOrArraySize = 1;
  readbackDesc.MipLevels = 1;
  readbackDesc.Format = DXGI_FORMAT_UNKNOWN;
  readbackDesc.SampleDesc.Count = 1;
  readbackDesc.SampleDesc.Quality = 0;
  readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  readbackDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

  D3D12_HEAP_PROPERTIES readbackHeap = {};
  readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
  ThrowIfFailed(s_device->CreateCommittedResource(
      &readbackHeap, D3D12_HEAP_FLAG_NONE, &readbackDesc,
      D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
      IID_PPV_ARGS(&s_queryReadbackBuffer)));
  s_queryReadbackBuffer->SetName(L"Query Readback Buffer");

  if (g_verboseRenderLogs) {
    fprintf(stderr, "DxrRenderer: Ray Tracing Pipeline ready\n");
  }
}

static bool IsMaterialAlphaTestedOrGlass(const Asset::Material &m) {
  const bool alphaTested =
      (m.alphaMode != "OPAQUE") || (m.diffuseColor[3] < 0.999f);
  const float metalness = (std::clamp)(m.metalness, 0.0f, 1.0f);
  const float transmission =
      (std::clamp)(m.transmissionWeight, 0.0f, 1.0f) * (1.0f - metalness);
  const bool glassLike = (transmission > 1.0e-5f) || (m.thinWalled > 0.5f);
  return alphaTested || glassLike;
}

static bool IsMeshOpaqueForRt(const Asset::GpuMesh &mesh) {
  const int matIdx = mesh.materialIndex;
  if (matIdx < 0 || matIdx >= (int)g_loadedMaterials.size()) {
    return true;
  }
  return !IsMaterialAlphaTestedOrGlass(g_loadedMaterials[(size_t)matIdx]);
}

static bool
HasDirtyMaterialsForMeshes(const std::vector<const Asset::GpuMesh *> &meshes) {
  if (s_dirtyMaterialFlags.empty()) {
    return false;
  }
  for (const Asset::GpuMesh *mesh : meshes) {
    if (!mesh) {
      continue;
    }
    const int matIdx = mesh->materialIndex;
    if (matIdx >= 0 && matIdx < (int)s_dirtyMaterialFlags.size() &&
        s_dirtyMaterialFlags[(size_t)matIdx] != 0) {
      return true;
    }
  }
  return false;
}

void MarkMaterialDirty(int materialIndex) {
  if (materialIndex < 0) {
    return;
  }
  const size_t idx = (size_t)materialIndex;
  if (idx >= s_dirtyMaterialFlags.size()) {
    s_dirtyMaterialFlags.resize(idx + 1, 0);
  }
  s_dirtyMaterialFlags[idx] = 1;
  QueueInteractiveWake("material dirtied");
}

void RequestAccelerationStructureRebuild() {
  s_forceAsRebuild = true;
  s_forceTlasUpdate = false;
  QueueInteractiveWake("acceleration structure rebuild");
}

void RequestAccelerationStructureUpdate() {
  if (!s_forceAsRebuild) {
    s_forceTlasUpdate = true;
  }
  QueueInteractiveWake("acceleration structure update");
}

static DirectX::XMFLOAT3 CurrentGrassCameraPos() {
  return {g_cameraData.pos[0], g_cameraData.pos[1], g_cameraData.pos[2]};
}

static float DistanceSq(const DirectX::XMFLOAT3 &a,
                        const DirectX::XMFLOAT3 &b) {
  const float dx = a.x - b.x;
  const float dy = a.y - b.y;
  const float dz = a.z - b.z;
  return dx * dx + dy * dy + dz * dz;
}

static void ResetGrassTlasCameraTracking() {
  s_hasGrassTlasCameraPos = false;
  s_hasGrassCameraMotionSample = false;
}

static bool GrassTlasNeedsCameraRefresh() {
  if (GrassManager::GetPatchCount() == 0) {
    ResetGrassTlasCameraTracking();
    return false;
  }

  const DirectX::XMFLOAT3 cameraPos = CurrentGrassCameraPos();
  if (!s_hasGrassTlasCameraPos) {
    s_lastObservedGrassCameraPos = cameraPos;
    s_hasGrassCameraMotionSample = true;
    return true;
  }

  constexpr float kGrassTlasCameraRefreshMeters = 0.25f;
  constexpr float kGrassCameraMotionEpsilonMeters = 0.001f;

  if (!s_hasGrassCameraMotionSample) {
    s_lastObservedGrassCameraPos = cameraPos;
    s_hasGrassCameraMotionSample = true;
  } else if (DistanceSq(cameraPos, s_lastObservedGrassCameraPos) >=
             (kGrassCameraMotionEpsilonMeters *
              kGrassCameraMotionEpsilonMeters)) {
    s_lastObservedGrassCameraPos = cameraPos;
    return false;
  }

  const bool movedPastRefreshDistance =
      DistanceSq(cameraPos, s_lastGrassTlasCameraPos) >=
      (kGrassTlasCameraRefreshMeters * kGrassTlasCameraRefreshMeters);
  if (!movedPastRefreshDistance) {
    return false;
  }

  return true;
}

static void CaptureGrassTlasCameraPos() {
  if (GrassManager::GetPatchCount() == 0) {
    ResetGrassTlasCameraTracking();
    return;
  }
  s_lastGrassTlasCameraPos = CurrentGrassCameraPos();
  s_lastObservedGrassCameraPos = s_lastGrassTlasCameraPos;
  s_hasGrassTlasCameraPos = true;
  s_hasGrassCameraMotionSample = true;
}

static void ClearDirtyMaterialsForMeshes(
    const std::vector<const Asset::GpuMesh *> &meshes) {
  if (s_dirtyMaterialFlags.empty()) {
    return;
  }
  for (const Asset::GpuMesh *mesh : meshes) {
    if (!mesh) {
      continue;
    }
    const int matIdx = mesh->materialIndex;
    if (matIdx >= 0 && matIdx < (int)s_dirtyMaterialFlags.size()) {
      s_dirtyMaterialFlags[(size_t)matIdx] = 0;
    }
  }
}

void BuildAccelerationStructures(
    const std::vector<const Asset::GpuMesh *> &meshes,
    const std::vector<Scene::Instance> &instances) {
  s_grassTlasStartIndex = 0xFFFFFFFFu;
  if (g_debugLog) {
    std::ostringstream _oss;
    _oss << "DxrRenderer::BuildAccelerationStructures: ENTRY meshes="
         << meshes.size() << " instances=" << instances.size() << "\n";
    fprintf(stderr, "%s", _oss.str().c_str());
    fflush(stderr);
  }
  if (!g_rayTracingSupported || !s_dxrDevice)
    return;
  try {
    if (g_debugLog) {
      std::ostringstream _oss2;
      _oss2 << "DxrRenderer::BuildAccelerationStructures: after check - "
               "commandQueue="
            << s_commandQueue << " fence=" << s_fence
            << " s_srvHeap=" << s_srvHeap.Get() << "\n";
      fprintf(stderr, "%s", _oss2.str().c_str());
      fflush(stderr);
    }
    if (meshes.empty() || instances.empty()) {
      if (g_verboseRenderLogs)
        fprintf(stderr, "DxrRenderer: Empty scene - clearing TLAS\n");
      s_tlas.result = nullptr;
      s_tlas.scratch = nullptr;
      s_tlasSupportsUpdate = false;
      s_allBLAS.clear();
      s_cachedMeshBuffersForBlas.clear();
      s_cachedMeshOpaqueForBlas.clear();
      s_cachedTlasMeshOrder.clear();
      return;
    }

    // Basic validation of command queue/fence setup
    if (!s_commandQueue || !s_fence || !s_fenceValues || !s_frameIndexPtr ||
        !s_fenceEvent) {
      fprintf(stderr,
              "DxrRenderer: Cannot build AS - command queue / fence not set\n");
      return;
    }

    // Ensure meshes are valid and DXR heap is ready
    if (!s_srvHeap) {
      fprintf(stderr, "DxrRenderer: Cannot build AS - SRV heap not created "
                      "(shader compile failed?)\n");
      return;
    }
    fprintf(stderr, "DxrRenderer::BuildAccelerationStructures: srvHeap OK. "
                    "validating meshes...\n");
    fflush(stderr);
    for (size_t i = 0; i < meshes.size(); ++i) {
      const auto &m = *meshes[i];
      if (!m.vertexBuffer || !m.indexBuffer) {
        fprintf(stderr,
                "DxrRenderer: Mesh %zu missing vertex or index buffer - "
                "aborting AS build\n",
                i);
        return;
      }
      if (m.vertexCount == 0 || m.indexCount == 0) {
        fprintf(stderr,
                "DxrRenderer: Mesh %zu has zero vertices or indices - aborting "
                "AS build\n",
                i);
        return;
      }

      // Create SRVs for VB and IB in our persistent DXR heap
      UINT descSize = s_device->GetDescriptorHandleIncrementSize(
          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

      // Defensive: ensure descriptor indices don't overflow the heap
      size_t vbIndex = (size_t)DXR_HEAP_VB_OFFSET + i;
      size_t ibIndex = (size_t)DXR_HEAP_IB_OFFSET + i;
      if (vbIndex >= DXR_HEAP_TOTAL_COUNT || ibIndex >= DXR_HEAP_TOTAL_COUNT) {
        fprintf(stderr,
                "DxrRenderer: Descriptor heap overflow for mesh %zu "
                "(vbIndex=%zu ibIndex=%zu total=%u)\n",
                i, vbIndex, ibIndex, DXR_HEAP_TOTAL_COUNT);
        fflush(stderr);
        return;
      }

      D3D12_CPU_DESCRIPTOR_HANDLE vbCpu =
          s_srvHeap->GetCPUDescriptorHandleForHeapStart();
      vbCpu.ptr += (SIZE_T)vbIndex * descSize;
      D3D12_CPU_DESCRIPTOR_HANDLE ibCpu =
          s_srvHeap->GetCPUDescriptorHandleForHeapStart();
      ibCpu.ptr += (SIZE_T)ibIndex * descSize;

      if (g_debugLog) {
        fprintf(stderr,
                "DxrRenderer: Creating VB/IB SRV for mesh %llu (vb=%p ib=%p "
                "verts=%u idx=%u)\n",
                (unsigned long long)i, m.vertexBuffer.Get(),
                m.indexBuffer.Get(), m.vertexCount, m.indexCount);
        fflush(stderr);
      }

      // Extra defensive checks to avoid crashing the process when a malformed
      // mesh or descriptor calculation slips through earlier validation.
      if (!m.vertexBuffer.Get()) {
        fprintf(stderr,
                "DxrRenderer: Null vertex buffer for mesh %zu - aborting AS "
                "build\n",
                i);
        fflush(stderr);
        return;
      }
      if (!m.indexBuffer.Get()) {
        fprintf(
            stderr,
            "DxrRenderer: Null index buffer for mesh %zu - aborting AS build\n",
            i);
        fflush(stderr);
        return;
      }
      if (vbCpu.ptr == 0 || ibCpu.ptr == 0) {
        fprintf(stderr,
                "DxrRenderer: Computed empty CPU descriptor handle for mesh "
                "%zu (vbCpu=%llu ibCpu=%llu) - aborting\n",
                i, (unsigned long long)vbCpu.ptr,
                (unsigned long long)ibCpu.ptr);
        fflush(stderr);
        return;
      }
      D3D12_SHADER_RESOURCE_VIEW_DESC vbSrv = {};
      vbSrv.Format = DXGI_FORMAT_UNKNOWN;
      vbSrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
      vbSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      vbSrv.Buffer.FirstElement = 0;
      vbSrv.Buffer.NumElements = m.vertexCount;
      vbSrv.Buffer.StructureByteStride = sizeof(Asset::Vertex);
      vbSrv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
      s_device->CreateShaderResourceView(m.vertexBuffer.Get(), &vbSrv, vbCpu);
      if (g_debugLog) {
        fprintf(stderr, "DxrRenderer: VB SRV created for mesh %zu\n", i);
        fflush(stderr);
      }

      D3D12_SHADER_RESOURCE_VIEW_DESC ibSrv = {};
      ibSrv.Format = DXGI_FORMAT_R32_UINT;
      ibSrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
      ibSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      ibSrv.Buffer.FirstElement = 0;
      ibSrv.Buffer.NumElements = m.indexCount;
      ibSrv.Buffer.StructureByteStride = 0; // Typed buffer
      ibSrv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
      s_device->CreateShaderResourceView(m.indexBuffer.Get(), &ibSrv, ibCpu);
      if (g_debugLog) {
        fprintf(stderr, "DxrRenderer: IB SRV created for mesh %zu\n", i);
        fflush(stderr);
      }
    }

    // Wait for GPU (simple sync)
    const UINT64 fence = s_fenceValues[*s_frameIndexPtr];
    HRESULT hr = s_commandQueue->Signal(s_fence, fence);
    if (FAILED(hr)) {
      fprintf(stderr, "DxrRenderer: Signal before AS build failed: 0x%08x\n",
              (unsigned)hr);
    }
    s_fenceValues[*s_frameIndexPtr]++;
    if (s_fence->GetCompletedValue() < fence) {
      s_fence->SetEventOnCompletion(fence, s_fenceEvent);
      if (WaitForSingleObject(s_fenceEvent, 5000) == WAIT_TIMEOUT) {
        fprintf(stderr, "DxrRenderer: Timeout waiting for AS build sync (5s). "
                        "GPU might have hung.\n");
      }
    }

    fprintf(stderr, "DxrRenderer: Creating command allocator/list\n");
    fflush(stderr);
    // Create command list
    ComPtr<ID3D12CommandAllocator> cmdAlloc;
    ComPtr<ID3D12GraphicsCommandList4> cmdList;
    HRESULT hrAlloc = s_device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAlloc));
    if (FAILED(hrAlloc)) {
      fprintf(stderr, "DxrRenderer: CreateCommandAllocator failed: 0x%08x\n",
              (unsigned)hrAlloc);
      return;
    }
    HRESULT hrList = s_device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc.Get(), nullptr,
        IID_PPV_ARGS(&cmdList));
    if (FAILED(hrList)) {
      fprintf(stderr, "DxrRenderer: CreateCommandList failed: 0x%08x\n",
              (unsigned)hrList);
      return;
    }

    // BLAS
    // Rebuild when geometry buffers change, opaque/transparent material
    // classification changes, or a material was explicitly marked dirty.
    std::vector<uint8_t> meshOpaqueStates(meshes.size(), 1u);
    for (size_t i = 0; i < meshes.size(); ++i) {
      meshOpaqueStates[i] = IsMeshOpaqueForRt(*meshes[i]) ? 1u : 0u;
    }

    bool meshesChanged = (meshes.size() != s_cachedMeshBuffersForBlas.size()) ||
                         (meshes.size() != s_cachedMeshOpaqueForBlas.size());
    if (!meshesChanged) {
      for (size_t i = 0; i < meshes.size(); ++i) {
        if (meshes[i]->vertexBuffer.Get() != s_cachedMeshBuffersForBlas[i] ||
            meshOpaqueStates[i] != s_cachedMeshOpaqueForBlas[i]) {
          meshesChanged = true;
          break;
        }
      }
    }

    auto WaitForFenceWithTimeout = [&](UINT64 fenceValue, DWORD timeoutMs,
                                       const char *timeoutMsg) -> bool {
      if (s_fence->GetCompletedValue() >= fenceValue) {
        return true;
      }
      s_fence->SetEventOnCompletion(fenceValue, s_fenceEvent);
      if (WaitForSingleObject(s_fenceEvent, timeoutMs) == WAIT_TIMEOUT) {
        fprintf(stderr, "%s\n", timeoutMsg);
        return false;
      }
      return true;
    };

    // Pipelining:
    // Create separate allocators for each batch so we can submit them without
    // blocking/waiting on the CPU. We only wait once at the very end.
    const bool conservativeBlasBuild =
        s_forceAsRebuild || IsDlssActive() || meshes.size() > 1500;
    const size_t BLAS_BATCH_SIZE = conservativeBlasBuild ? 64 : 500;
    // Avoid compaction ramps and extra peak VRAM when rebuilding from
    // live-update paths or when DLSS/Streamline is active.
    const bool enableBlasCompaction =
        (meshes.size() <= 1500) && !conservativeBlasBuild;
    const BlasBuildPreference blasBuildPreference =
        conservativeBlasBuild ? BlasBuildPreference::FastBuild
                              : BlasBuildPreference::FastTrace;
    size_t batchCount = 0;

    std::vector<ComPtr<ID3D12CommandAllocator>> submittedBatchAllocators;
    submittedBatchAllocators.push_back(cmdAlloc); // keep alive until fence wait

    if (meshesChanged || s_allBLAS.empty()) {
      s_allBLAS.clear();
      s_cachedMeshBuffersForBlas.clear();
      s_cachedMeshOpaqueForBlas.clear();
      s_tlasSupportsUpdate = false;
      s_cachedTlasMeshOrder.clear();
      try {
        if (conservativeBlasBuild && g_verboseRenderLogs) {
          fprintf(stderr,
                  "DxrRenderer: Using conservative BLAS build mode (%zu meshes, DLSS=%s, forceRebuild=%s).\n",
                  meshes.size(), IsDlssActive() ? "on" : "off",
                  s_forceAsRebuild ? "yes" : "no");
        }

        size_t batchBlasStartIndex = 0;
        auto submitCurrentBatch = [&](bool reopenList, const char *timeoutMsg) {
          ThrowIfFailed(cmdList->Close());
          ID3D12CommandList *lists[] = {cmdList.Get()};
          s_commandQueue->ExecuteCommandLists(1, lists);

          const UINT64 fenceVal = s_fenceValues[*s_frameIndexPtr];
          s_commandQueue->Signal(s_fence, fenceVal);
          s_fenceValues[*s_frameIndexPtr]++;
          if (!WaitForFenceWithTimeout(fenceVal, 10000, timeoutMsg)) {
            return false;
          }

          if (!enableBlasCompaction) {
            for (size_t k = batchBlasStartIndex; k < s_allBLAS.size(); ++k) {
              s_allBLAS[k].buffers.scratch.Reset();
              s_allBLAS[k].buffers.compactedSizeBuffer.Reset();
              s_allBLAS[k].buffers.compactedSizeReadback.Reset();
            }
          }
          batchBlasStartIndex = s_allBLAS.size();

          if (reopenList) {
            ThrowIfFailed(cmdAlloc->Reset());
            ThrowIfFailed(cmdList->Reset(cmdAlloc.Get(), nullptr));
          }
          return true;
        };

        if (conservativeBlasBuild) {
          for (size_t i = 0; i < meshes.size(); ++i) {
            const auto &mesh = *meshes[i];
            if (!mesh.vertexBuffer || !mesh.indexBuffer) {
              continue;
            }

            auto vbAddr = mesh.vertexBuffer->GetGPUVirtualAddress();
            auto ibAddr = mesh.indexBuffer->GetGPUVirtualAddress();

            auto bl = BuildBLAS(s_dxrDevice.Get(), cmdList.Get(), vbAddr,
                                mesh.vertexCount, sizeof(Asset::Vertex), ibAddr,
                                mesh.indexCount, meshOpaqueStates[i] != 0, false,
                                enableBlasCompaction, blasBuildPreference);
            if (bl.result && bl.scratch) {
              s_allBLAS.push_back({bl, (UINT64)i});
              s_cachedMeshBuffersForBlas.push_back(mesh.vertexBuffer.Get());
              s_cachedMeshOpaqueForBlas.push_back(meshOpaqueStates[i]);
            }

            batchCount++;
            if (batchCount >= BLAS_BATCH_SIZE) {
              fprintf(stderr,
                      "DxrRenderer: Submitting conservative BLAS batch (mesh %zu/%zu)...\n",
                      i, meshes.size());
              if (!submitCurrentBatch(
                      true,
                      "DxrRenderer: Timeout waiting for conservative BLAS build batch (10s). Aborting AS rebuild for this frame.")) {
                return;
              }
              batchCount = 0;
            }
          }

          if (batchCount > 0) {
            if (!submitCurrentBatch(
                    false,
                    "DxrRenderer: Timeout waiting for conservative BLAS build batch (10s). Aborting AS rebuild for this frame.")) {
              return;
            }
            batchCount = 0;
          }
        } else {
          for (size_t i = 0; i < meshes.size(); ++i) {
            const auto &mesh = *meshes[i];
            if (!mesh.vertexBuffer || !mesh.indexBuffer)
              continue;

            auto vbAddr = mesh.vertexBuffer->GetGPUVirtualAddress();
            auto ibAddr = mesh.indexBuffer->GetGPUVirtualAddress();

            auto bl =
                BuildBLAS(s_dxrDevice.Get(), cmdList.Get(), vbAddr,
                          mesh.vertexCount, sizeof(Asset::Vertex), ibAddr,
                          mesh.indexCount, meshOpaqueStates[i] != 0, false,
                          enableBlasCompaction, blasBuildPreference);
            if (bl.result && bl.scratch) {
              s_allBLAS.push_back({bl, (UINT64)i});
              s_cachedMeshBuffersForBlas.push_back(mesh.vertexBuffer.Get());
              s_cachedMeshOpaqueForBlas.push_back(meshOpaqueStates[i]);
            }

            batchCount++;
            if (batchCount >= BLAS_BATCH_SIZE) {
              fprintf(stderr,
                      "DxrRenderer: Submitting BLAS batch (mesh %zu/%zu)...\n",
                      i, meshes.size());
              ThrowIfFailed(cmdList->Close());
              ID3D12CommandList *lists[] = {cmdList.Get()};
              s_commandQueue->ExecuteCommandLists(1, lists);

              // DO NOT WAIT. Create new allocator and continue recording.
              ComPtr<ID3D12CommandAllocator> nextAlloc;
              ThrowIfFailed(s_device->CreateCommandAllocator(
                  D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&nextAlloc)));
              submittedBatchAllocators.push_back(nextAlloc);

              // Reset same list with new allocator
              ThrowIfFailed(cmdList->Reset(nextAlloc.Get(), nullptr));
              batchCount = 0;
            }
          }

          // Final flush if any remaining (and ensure list is closed regardless)
          ThrowIfFailed(cmdList->Close());
          ID3D12CommandList *lists[] = {cmdList.Get()};
          s_commandQueue->ExecuteCommandLists(1, lists);

          // NOW we wait for everything to finish (Single Wait)
          const UINT64 fenceVal = s_fenceValues[*s_frameIndexPtr];
          s_commandQueue->Signal(s_fence, fenceVal);
          s_fenceValues[*s_frameIndexPtr]++;
          if (!WaitForFenceWithTimeout(
                  fenceVal, 10000,
                  "DxrRenderer: Timeout waiting for BLAS build batch (10s). Aborting AS rebuild for this frame.")) {
            return;
          }
        }

        submittedBatchAllocators.clear();

        if (enableBlasCompaction) {
          // BLAS compaction pass (reduces AS VRAM footprint).
          std::vector<ComPtr<ID3D12Resource>> compactedResults(s_allBLAS.size());
          std::vector<UINT64> compactedSizes(s_allBLAS.size(), 0);
          size_t compactCount = 0;
          for (size_t k = 0; k < s_allBLAS.size(); ++k) {
            MeshBLAS &meshBlas = s_allBLAS[k];
            UINT64 compactedSize =
                ReadbackUint64(meshBlas.buffers.compactedSizeReadback.Get());
            meshBlas.buffers.compactedSizeInBytes = compactedSize;
            if (compactedSize == 0 ||
                compactedSize >= meshBlas.buffers.resultSizeInBytes) {
              continue;
            }

            UINT64 compactedAligned =
                Align(compactedSize,
                      D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
            if (compactedAligned + 1024 >= meshBlas.buffers.resultSizeInBytes) {
              continue;
            }

            AllocateUAVBuffer(
                s_device, compactedAligned, &compactedResults[k],
                D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                L"BLAS Result (Compacted)");
            compactedSizes[k] = compactedAligned;
            compactCount++;
          }

          if (compactCount > 0) {
            ThrowIfFailed(cmdAlloc->Reset());
            ThrowIfFailed(cmdList->Reset(cmdAlloc.Get(), nullptr));
            for (size_t k = 0; k < s_allBLAS.size(); ++k) {
              if (!compactedResults[k]) {
                continue;
              }
              cmdList->CopyRaytracingAccelerationStructure(
                  compactedResults[k]->GetGPUVirtualAddress(),
                  s_allBLAS[k].buffers.result->GetGPUVirtualAddress(),
                  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT);
            }

            D3D12_RESOURCE_BARRIER compactBarrier = {};
            compactBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            compactBarrier.UAV.pResource = nullptr;
            cmdList->ResourceBarrier(1, &compactBarrier);

            ThrowIfFailed(cmdList->Close());
            ID3D12CommandList *compactLists[] = {cmdList.Get()};
            s_commandQueue->ExecuteCommandLists(1, compactLists);

            const UINT64 compactFenceVal = s_fenceValues[*s_frameIndexPtr];
            s_commandQueue->Signal(s_fence, compactFenceVal);
            s_fenceValues[*s_frameIndexPtr]++;
            if (!WaitForFenceWithTimeout(
                    compactFenceVal, 10000,
                    "DxrRenderer: Timeout waiting for BLAS compaction batch (10s). Keeping original BLAS for safety.")) {
              return;
            }

            for (size_t k = 0; k < s_allBLAS.size(); ++k) {
              if (compactedResults[k]) {
                s_allBLAS[k].buffers.result = compactedResults[k];
                s_allBLAS[k].buffers.resultSizeInBytes = compactedSizes[k];
              }
            }
          }
        } else if (g_verboseRenderLogs) {
          fprintf(stderr,
                  "DxrRenderer: Skipping BLAS compaction for large scene (%zu meshes) to reduce peak VRAM and load time.\n",
                  meshes.size());
        }

        // Safe to release temporary BLAS resources now.
        for (size_t k = 0; k < s_allBLAS.size(); ++k) {
          s_allBLAS[k].buffers.scratch.Reset();
          s_allBLAS[k].buffers.compactedSizeBuffer.Reset();
          s_allBLAS[k].buffers.compactedSizeReadback.Reset();
        }

        // Restore a fresh allocator/list for TLAS build.
        ThrowIfFailed(cmdAlloc->Reset());
        ThrowIfFailed(cmdList->Reset(cmdAlloc.Get(), nullptr));

      } catch (...) {
        fprintf(stderr, "DxrRenderer: BLAS Build crashed\n");
        return;
      }
      printf("DxrRenderer: BLAS creation completed. Total BLAS count: %zu\n",
             s_allBLAS.size());
    }

    ClearDirtyMaterialsForMeshes(meshes);

    if (s_allBLAS.empty()) {
      fprintf(stderr, "DxrRenderer: No BLAS built - aborting TLAS build\n");
      s_tlasSupportsUpdate = false;
      s_cachedTlasMeshOrder.clear();
      return;
    }

    // TLAS

    // Optimization: Pre-compute map from VertexBuffer -> BLAS Index
    std::unordered_map<ID3D12Resource *, size_t> meshToBlasIndex;
    meshToBlasIndex.reserve(s_allBLAS.size());
    for (size_t k = 0; k < s_allBLAS.size(); ++k) {
      // s_allBLAS[k].meshId stores originalMeshIndex
      size_t origIdx = (size_t)s_allBLAS[k].meshId;
      if (origIdx < meshes.size() && meshes[origIdx]->vertexBuffer) {
        meshToBlasIndex[meshes[origIdx]->vertexBuffer.Get()] = k;
      }
    }

    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs;
    instanceDescs.reserve(instances.size() + 1); // +1 for potential dummy
    std::vector<const Asset::GpuMesh *> instanceMeshOrder;
    instanceMeshOrder.reserve(instances.size() + 1);

    for (const auto &sceneInst : instances) {
      if (!sceneInst.mesh || !sceneInst.mesh->vertexBuffer)
        continue;

      auto it = meshToBlasIndex.find(sceneInst.mesh->vertexBuffer.Get());
      if (it == meshToBlasIndex.end())
        continue;

      size_t blasIndex = it->second;
      UINT originalMeshIdx = (UINT)s_allBLAS[blasIndex].meshId;

      D3D12_RAYTRACING_INSTANCE_DESC inst = {};
      // Extract from XMMATRIX
      DirectX::XMFLOAT4X4 m;
      DirectX::XMStoreFloat4x4(&m, sceneInst.transform);

      // Convert Column-Major 4x4 (stored in m) to Row-Major 3x4
      // s_allBLAS[blasIndex].meshId stores originalMeshIndex
      inst.Transform[0][0] = m._11;
      inst.Transform[0][1] = m._21;
      inst.Transform[0][2] = m._31;
      inst.Transform[0][3] = m._41;
      inst.Transform[1][0] = m._12;
      inst.Transform[1][1] = m._22;
      inst.Transform[1][2] = m._32;
      inst.Transform[1][3] = m._42;
      inst.Transform[2][0] = m._13;
      inst.Transform[2][1] = m._23;
      inst.Transform[2][2] = m._33;
      inst.Transform[2][3] = m._43;

      inst.InstanceID = originalMeshIdx; // Use mesh index for shader binding
      inst.InstanceMask = 0xFF;
      inst.InstanceContributionToHitGroupIndex = 0;
      inst.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
      inst.AccelerationStructure =
          s_allBLAS[blasIndex].buffers.result->GetGPUVirtualAddress();
      instanceDescs.push_back(inst);
      instanceMeshOrder.push_back(sceneInst.mesh);
    }

    // Workaround: some drivers crash when TLAS contains a single instance.
    // Add a second "dummy" instance referencing the same BLAS but placed
    // far outside the view frustum.  We leave the InstanceMask unchanged
    // (0xFF) so the driver treats it as a valid instance; the translation
    // ensures it will never be hit by a ray.  This avoids the one-instance
    // optimization/pathology while keeping the scene effectively unchanged.
    if (instanceDescs.size() == 1) {
      D3D12_RAYTRACING_INSTANCE_DESC dummy = instanceDescs[0];
      // translate the dummy a large distance along X (and Y/Z) so it's off-
      // screen.  Use a translation of e.g. 1e6 units; the exact value isn't
      // important as long as it's outside typical scene bounds.
      dummy.Transform[0][3] += 1e6f;
      dummy.Transform[1][3] += 1e6f;
      dummy.Transform[2][3] += 1e6f;
      // keep mask=0xFF so the TLAS sees two valid instances
      // InstanceContributionToHitGroupIndex etc are same as original
      instanceDescs.push_back(dummy);
      instanceMeshOrder.push_back(instanceMeshOrder[0]);
      if (g_verboseRenderLogs) {
        fprintf(stderr,
                "DxrRenderer: Added off-screen dummy TLAS instance to avoid "
                "single-instance driver bug\n");
      }
    }
    if (instanceDescs.empty()) {
      fprintf(stderr, "DxrRenderer: No valid TLAS instances - clearing TLAS\n");
      s_tlas.result.Reset();
      s_tlas.scratch.Reset();
      s_tlasSupportsUpdate = false;
      s_cachedTlasMeshOrder.clear();
      return;
    }

    // --- append grass TLAS instances on CPU (stable fallback path) ---
    {
      s_grassTlasStartIndex = 0xFFFFFFFFu;
      const UINT grassRequested = GrassManager::GetPatchCount();
      if (grassRequested > 0) {
        const Asset::GpuMesh *patchMesh = GrassManager::GetPatchMesh();
        const Asset::GpuMesh *midPatchMesh = GrassManager::GetMidPatchMesh();
        auto findGrassBlas = [&](const Asset::GpuMesh *mesh,
                                 UINT64 &blasAddr,
                                 UINT &meshIndex) {
          blasAddr = 0;
          meshIndex = 0;
          if (!mesh || !mesh->vertexBuffer) {
            return;
          }
          auto patchIt = meshToBlasIndex.find(mesh->vertexBuffer.Get());
          if (patchIt == meshToBlasIndex.end()) {
            return;
          }
          const size_t patchBlasIndex = patchIt->second;
          if (patchBlasIndex < s_allBLAS.size() &&
              s_allBLAS[patchBlasIndex].buffers.result) {
            blasAddr =
                s_allBLAS[patchBlasIndex].buffers.result->GetGPUVirtualAddress();
            meshIndex = (UINT)s_allBLAS[patchBlasIndex].meshId;
          }
        };

        UINT64 patchBlasAddr = 0;
        UINT64 midPatchBlasAddr = 0;
        UINT patchMeshIndex = 0;
        UINT midPatchMeshIndex = 0;
        findGrassBlas(patchMesh, patchBlasAddr, patchMeshIndex);
        findGrassBlas(midPatchMesh, midPatchBlasAddr, midPatchMeshIndex);

        if (patchBlasAddr != 0 || midPatchBlasAddr != 0) {
          const auto &patches = GrassManager::GetPatches();
          std::vector<FGrassPatch> rtPatches;
          rtPatches.reserve(patches.size());
          const float nearDistance = GrassManager::GetNearDistance();
          const float midDistance = GrassManager::GetMidDistance();
          const float midDistanceSq = midDistance * midDistance;
          const float transitionStart = nearDistance * 0.72f;
          const float transitionEnd =
              (std::min)(midDistance * 0.68f, nearDistance * 1.85f);
          const DirectX::XMFLOAT3 cameraPos = {g_cameraData.pos[0],
                                               g_cameraData.pos[1],
                                               g_cameraData.pos[2]};
          for (const FGrassPatch &b : patches) {
            const float dx = b.position.x - cameraPos.x;
            const float dy = b.position.y - cameraPos.y;
            const float dz = b.position.z - cameraPos.z;
            const float distSq = dx * dx + dy * dy + dz * dz;
            if (distSq > midDistanceSq) {
              continue;
            }

            const float dist = sqrtf(distSq);
            const float transitionDenom =
                (std::max)(transitionEnd - transitionStart, 0.001f);
            float nearWeight =
                1.0f - (dist - transitionStart) / transitionDenom;
            nearWeight = (std::clamp)(nearWeight, 0.0f, 1.0f);
            nearWeight = nearWeight * nearWeight * (3.0f - 2.0f * nearWeight);
            const float lodNoise =
                (float)((b.colorVariation >> 8) & 0xFFFFu) / 65535.0f;
            const bool useNearMesh =
                (patchBlasAddr != 0) &&
                (midPatchBlasAddr == 0 || dist <= transitionStart ||
                 (dist < transitionEnd && lodNoise < nearWeight));

            UINT64 selectedBlasAddr = patchBlasAddr;
            UINT selectedMeshIndex = patchMeshIndex;
            const Asset::GpuMesh *selectedMesh = patchMesh;
            if (!useNearMesh) {
              if (midPatchBlasAddr == 0) {
                continue;
              }
              selectedBlasAddr = midPatchBlasAddr;
              selectedMeshIndex = midPatchMeshIndex;
              selectedMesh = midPatchMesh;
            } else if (selectedBlasAddr == 0) {
              continue;
            }

            if (rtPatches.empty()) {
              s_grassTlasStartIndex = (UINT)instanceDescs.size();
            }
            rtPatches.push_back(b);
            const float s = sinf(b.yawRadians);
            const float c = cosf(b.yawRadians);
            const float sc = (std::max)(b.scale, 1e-3f);
            DirectX::XMVECTOR up =
                DirectX::XMLoadFloat3(&b.normal);
            if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(up)) < 1e-8f) {
              up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
            }
            up = DirectX::XMVector3Normalize(up);
            DirectX::XMVECTOR helper =
                (fabsf(DirectX::XMVectorGetY(up)) > 0.9f)
                    ? DirectX::XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f)
                    : DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
            DirectX::XMVECTOR right = DirectX::XMVector3Cross(helper, up);
            if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(right)) < 1e-8f) {
              right = DirectX::XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
            }
            right = DirectX::XMVector3Normalize(right);
            DirectX::XMVECTOR forward =
                DirectX::XMVector3Normalize(DirectX::XMVector3Cross(up, right));
            DirectX::XMVECTOR yawRight = DirectX::XMVectorSubtract(
                DirectX::XMVectorScale(right, c),
                DirectX::XMVectorScale(forward, s));
            DirectX::XMVECTOR yawForward = DirectX::XMVectorAdd(
                DirectX::XMVectorScale(right, s),
                DirectX::XMVectorScale(forward, c));

            DirectX::XMFLOAT3 rightF = {};
            DirectX::XMFLOAT3 upF = {};
            DirectX::XMFLOAT3 forwardF = {};
            DirectX::XMStoreFloat3(&rightF, yawRight);
            DirectX::XMStoreFloat3(&upF, up);
            DirectX::XMStoreFloat3(&forwardF, yawForward);

            D3D12_RAYTRACING_INSTANCE_DESC inst = {};
            inst.Transform[0][0] = rightF.x * sc;
            inst.Transform[0][1] = upF.x * sc;
            inst.Transform[0][2] = forwardF.x * sc;
            inst.Transform[0][3] = b.position.x;
            inst.Transform[1][0] = rightF.y * sc;
            inst.Transform[1][1] = upF.y * sc;
            inst.Transform[1][2] = forwardF.y * sc;
            inst.Transform[1][3] = b.position.y;
            inst.Transform[2][0] = rightF.z * sc;
            inst.Transform[2][1] = upF.z * sc;
            inst.Transform[2][2] = forwardF.z * sc;
            inst.Transform[2][3] = b.position.z;
            inst.InstanceID = selectedMeshIndex;
            inst.InstanceMask = 0xFF;
            inst.InstanceContributionToHitGroupIndex = 0;
            inst.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
            inst.AccelerationStructure = selectedBlasAddr;
            instanceDescs.push_back(inst);
            instanceMeshOrder.push_back(selectedMesh);
          }
          GrassManager::UploadRayTracingPatches(cmdList.Get(), rtPatches);
        } else if (g_verboseRenderLogs) {
          fprintf(stderr,
                  "DxrRenderer: grass instances present but no valid patch BLAS;"
                  " skipping grass TLAS append this frame\n");
        }
      }
    }
    const UINT totalCount = (UINT)instanceDescs.size();

    ComPtr<ID3D12Resource> instanceDescBuffer;
    AllocateUploadBuffer(s_device, instanceDescs.data(),
                         (UINT64)totalCount * sizeof(D3D12_RAYTRACING_INSTANCE_DESC),
                         &instanceDescBuffer, L"TLAS Instance Buffer");

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.NumDescs = totalCount;
    inputs.InstanceDescs = instanceDescBuffer->GetGPUVirtualAddress();

    bool canRefitTlas =
        !meshesChanged && s_tlasSupportsUpdate && s_tlas.result &&
        s_tlas.scratch &&
        (instanceMeshOrder.size() == s_cachedTlasMeshOrder.size());
    if (canRefitTlas) {
      for (size_t i = 0; i < instanceMeshOrder.size(); ++i) {
        if (instanceMeshOrder[i] != s_cachedTlasMeshOrder[i]) {
          canRefitTlas = false;
          break;
        }
      }
    }

    if (!canRefitTlas) {
      inputs.Flags =
          D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE |
          D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;

      D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
      s_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs,
                                                                  &info);
      const UINT64 requiredScratchSize =
          Align((std::max)(info.ScratchDataSizeInBytes,
                           info.UpdateScratchDataSizeInBytes),
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
      const UINT64 requiredResultSize =
          Align(info.ResultDataMaxSizeInBytes,
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);

      if (!s_tlas.scratch || s_tlas.scratchSizeInBytes < requiredScratchSize) {
        s_tlas.scratch.Reset();
        AllocateUAVBuffer(s_device, requiredScratchSize, &s_tlas.scratch,
                          D3D12_RESOURCE_STATE_COMMON, L"TLAS Scratch");
        s_tlas.scratchSizeInBytes = requiredScratchSize;
      }
      if (!s_tlas.result || s_tlas.resultSizeInBytes < requiredResultSize) {
        s_tlas.result.Reset();
        AllocateUAVBuffer(
            s_device, requiredResultSize, &s_tlas.result,
            D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
            L"TLAS Result");
        s_tlas.resultSizeInBytes = requiredResultSize;
      }
    } else {
      inputs.Flags =
          D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE |
          D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
    }

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    buildDesc.Inputs = inputs;
    buildDesc.DestAccelerationStructureData =
        s_tlas.result->GetGPUVirtualAddress();
    buildDesc.ScratchAccelerationStructureData =
        s_tlas.scratch->GetGPUVirtualAddress();
    if (canRefitTlas) {
      buildDesc.SourceAccelerationStructureData =
          s_tlas.result->GetGPUVirtualAddress();
    }
    cmdList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

    D3D12_RESOURCE_BARRIER uavBarrier = {};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = s_tlas.result.Get();
    cmdList->ResourceBarrier(1, &uavBarrier);

    ThrowIfFailed(cmdList->Close());
    ID3D12CommandList *lists[] = {cmdList.Get()};
    s_commandQueue->ExecuteCommandLists(1, lists);

    // Wait for finish
    const UINT64 fence2 = s_fenceValues[*s_frameIndexPtr];
    s_commandQueue->Signal(s_fence, fence2);
    s_fenceValues[*s_frameIndexPtr]++;
    if (!WaitForFenceWithTimeout(
            fence2, 5000,
            "DxrRenderer: Timeout waiting for TLAS build (5s). Keeping previous AS state for this frame.")) {
      return;
    }
    if (g_verboseRenderLogs) {
      fprintf(stderr, "DxrRenderer: Acceleration structures %s\n",
              canRefitTlas ? "updated (TLAS refit)" : "rebuilt");
    }

    if (!canRefitTlas) {
      s_cachedTlasMeshOrder = instanceMeshOrder;
      s_tlasSupportsUpdate = true;
    }
    CaptureGrassTlasCameraPos();

  } catch (const std::exception &e) {
    fprintf(stderr, "DxrRenderer: Exception during AS build: %s\n", e.what());
#ifdef _DEBUG
    // Dump recent D3D12 info queue messages if available
    ComPtr<ID3D12InfoQueue> infoQueue;
    if (SUCCEEDED(s_device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
      UINT64 num = infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();
      for (UINT64 i = 0; i < num; ++i) {
        SIZE_T messageLength = 0;
        infoQueue->GetMessage(i, nullptr, &messageLength);
        std::vector<char> message(messageLength);
        D3D12_MESSAGE *pMsg = reinterpret_cast<D3D12_MESSAGE *>(message.data());
        infoQueue->GetMessage(i, pMsg, &messageLength);
        fprintf(stderr,
                "D3D12 INFO (AS build): Category=%d Severity=%d ID=%d: %s\n",
                (int)pMsg->Category, (int)pMsg->Severity, (int)pMsg->ID,
                pMsg->pDescription);
      }
    }
#endif
  }
}

void UpdateLights(const std::vector<Light> &lights, bool resetAccumulation) {
  WaitForAsyncRestirIdleForLightUpdates();

  if (lights.empty()) {
    if (s_lightCount != 0) {
      s_lightCount = 0;
      s_lastLightsCpu.clear();
      if (resetAccumulation) {
        ResetAccumulation();
      }
    }
    return;
  }

  // Avoid resetting accumulation / Streamline history when lights didn't
  // change.
  if (lights.size() == s_lastLightsCpu.size()) {
    const size_t byteSize = lights.size() * sizeof(Light);
    if (byteSize > 0 &&
        memcmp(lights.data(), s_lastLightsCpu.data(), byteSize) == 0) {
      // Keep s_lightCount accurate (and allow the caller to still call
      // UpdateLights every frame).
      s_lightCount = (UINT)lights.size();
      return;
    }
  }

  s_lightCount = (UINT)lights.size();

  // Ensure buffer size is at least 1 element to avoid creation errors/null
  // descriptors
  UINT bufferSize = (UINT)(lights.size() * sizeof(Light));
  if (bufferSize == 0)
    bufferSize = sizeof(Light);

  // Recreate buffer if size changed
  if (!s_lightBuffer || s_lightBuffer->GetDesc().Width < bufferSize) {
    s_lightBuffer.Reset();
    D3D12_HEAP_PROPERTIES heapProps = {D3D12_HEAP_TYPE_UPLOAD,
                                       D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                                       D3D12_MEMORY_POOL_UNKNOWN, 1, 1};
    D3D12_RESOURCE_DESC resDesc = {D3D12_RESOURCE_DIMENSION_BUFFER,
                                   0,
                                   bufferSize,
                                   1,
                                   1,
                                   1,
                                   DXGI_FORMAT_UNKNOWN,
                                   {1, 0},
                                   D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                                   D3D12_RESOURCE_FLAG_NONE};
    ThrowIfFailed(s_device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&s_lightBuffer)));
  }

  void *pData = nullptr;
  ThrowIfFailed(s_lightBuffer->Map(0, nullptr, &pData));
  memcpy(pData, lights.data(), bufferSize);
  s_lightBuffer->Unmap(0, nullptr);

  s_lastLightsCpu = lights;

  if (resetAccumulation) {
    ResetAccumulation();
  }
}

void ResetAccumulation() {
  QueueInteractiveWake("accumulation reset");
  s_accumulation.Reset();
  s_transmissionAccumulation.Reset();
  s_rrStillFrameSpp = 0;
  s_lastNoiseLevel = 0.0f; // Reset noise level so rendering restarts
  s_hasNoiseEstimate = false;
  s_noiseStatsDispatchCount = 0;
  s_noiseConvergedLatched = false;
  s_hasTonemappedFrame = false;
  s_hasDenoised = false; // Reset auto-denoiser state
  s_finalDisplayState = FinalDisplayState::WakePending;
  // Keep Streamline history reset separate from accumulation decisions.
  // Accumulation resets happen on real camera/settings changes; per-frame
  // jitter changes must not trigger this.
  s_streamlineResetHistory = true;
  if (g_verboseRenderLogs) {
    fprintf(stderr, "DxrRenderer: Accumulation Reset\n");
  }
}

void RequestInteractiveWake(const char *reason) { QueueInteractiveWake(reason); }

bool ConsumeInteractiveWake() {
  if (!s_interactiveWakeRequested && s_interactiveWakeFrameBudget == 0) {
    return false;
  }

  s_interactiveWakeRequested = false;
  if (s_interactiveWakeFrameBudget == 0) {
    s_interactiveWakeFrameBudget = kInteractiveWakeFrameBudget;
  }
  --s_interactiveWakeFrameBudget;
  if (s_interactiveWakeFrameBudget == 0 &&
      s_finalDisplayState == FinalDisplayState::WakePending) {
    s_finalDisplayState = FinalDisplayState::Rendering;
  }
  return true;
}

bool HasInteractiveWake() {
  return s_interactiveWakeRequested || s_interactiveWakeFrameBudget > 0 ||
         s_finalDisplayState == FinalDisplayState::WakePending;
}

void RequestSceneLoadWarmup(const char *reason) {
  s_sceneLoadWarmupFramesRemaining =
      (std::max)(s_sceneLoadWarmupFramesRemaining,
                 kSceneLoadWarmupFrameBudget);
  QueueInteractiveWake(reason ? reason : "scene load warmup");
}

bool HasSceneLoadWarmup() { return s_sceneLoadWarmupFramesRemaining > 0; }

bool ConsumeSceneLoadWarmupFrame() {
  if (s_sceneLoadWarmupFramesRemaining == 0) {
    return false;
  }
  --s_sceneLoadWarmupFramesRemaining;
  return true;
}

void MarkTextureDescriptorTableDirty() {
  s_textureTableDirty = true;
  QueueInteractiveWake("texture descriptor table dirty");
}

void RequestPipelineRecreate(const char *context) {
  s_pipelineRecreateRequested = true;
  s_pipelineRecreateContext = context ? context : "unspecified";
  QueueInteractiveWake("pipeline recreate requested");
}

bool ConsumePipelineRecreateRequest(std::string *outContext) {
  if (!s_pipelineRecreateRequested) {
    return false;
  }
  s_pipelineRecreateRequested = false;
  if (outContext) {
    *outContext = s_pipelineRecreateContext;
  }
  s_pipelineRecreateContext.clear();
  return true;
}

bool HasPipelineRecreateRequest() { return s_pipelineRecreateRequested; }

void SetStreamlineManager(StreamlineManager *streamline) {
  s_streamline = streamline;
  s_streamlineResetHistory = true;
}

void ResetStreamlineHistory() {
  // Resetting DLSS history should resume sampling even if we previously froze.
  QueueInteractiveWake("streamline history reset");
  s_rrStillFrameSpp = 0;
  s_hasTonemappedFrame = false;
  s_streamlineResetHistory = true;
}

void SetPathTracingBackend(PathTracingBackend backend) {
  if (s_pathTracingBackend == backend) {
    return;
  }
  WaitForAsyncRestirIdleForLightUpdates();
  s_asyncRestirPending = false;
  s_pathTracingBackend = backend;
  if (g_verboseRenderLogs) {
    fprintf(stderr, "DxrRenderer: Path tracing backend set to %d\n",
            static_cast<int>(backend));
  }
  PrepareWavefrontBackendPipelines();
  DxrRenderer::ResetAccumulation();
}

PathTracingBackend GetPathTracingBackend() { return s_pathTracingBackend; }

const char *GetWavefrontStageName() { return s_wavefrontStageName; }

UINT GetWavefrontBootstrapPathCount() {
  return s_lastWavefrontBootstrapPathCount;
}

UINT GetWavefrontBootstrapOverflowCount() {
  return s_lastWavefrontBootstrapOverflowCount;
}

UINT GetWavefrontContinuationOverflowCount() {
  return s_lastWavefrontContinuationOverflowCount;
}

UINT GetWavefrontShadowOverflowCount() {
  return s_lastWavefrontShadowOverflowCount;
}

UINT GetWavefrontMaterialBinOverflowCount() {
  return s_lastWavefrontMaterialBinOverflowCount;
}

UINT GetWavefrontBootstrapDispatchGroups() {
  return s_lastWavefrontBootstrapDispatchGroups;
}

UINT GetWavefrontPrimaryRecordCount() {
  return s_lastWavefrontPrimaryRecordCount;
}

UINT GetWavefrontPrimaryHitCount() { return s_lastWavefrontPrimaryHitCount; }

UINT GetWavefrontPrimaryMissCount() { return s_lastWavefrontPrimaryMissCount; }

UINT GetWavefrontResolveRecordCount() {
  return s_lastWavefrontResolveRecordCount;
}

UINT GetWavefrontResolveSurfaceCount() {
  return s_lastWavefrontResolveSurfaceCount;
}

UINT GetWavefrontResolveDiffuseCount() {
  return s_lastWavefrontResolveDiffuseCount;
}

UINT GetWavefrontResolveSpecularCount() {
  return s_lastWavefrontResolveSpecularCount;
}

UINT GetWavefrontResolveTransmissionCount() {
  return s_lastWavefrontResolveTransmissionCount;
}

UINT GetWavefrontResolveSkyCount() { return s_lastWavefrontResolveSkyCount; }

UINT GetWavefrontSecondaryPathCount() {
  return s_lastWavefrontSecondaryPathCount;
}

UINT GetWavefrontSecondaryDiffuseCount() {
  return s_lastWavefrontSecondaryDiffuseCount;
}

UINT GetWavefrontSecondarySpecularCount() {
  return s_lastWavefrontSecondarySpecularCount;
}

UINT GetWavefrontSecondaryTransmissionCount() {
  return s_lastWavefrontSecondaryTransmissionCount;
}

UINT GetWavefrontShadowTaskCount() { return s_lastWavefrontShadowTaskCount; }

UINT GetWavefrontSecondaryVisibilityRecordCount() {
  return s_lastWavefrontSecondaryVisibilityRecordCount;
}

UINT GetWavefrontSecondaryVisibilityDiffuseLaneCount() {
  return s_lastWavefrontStats[47];
}

UINT GetWavefrontSecondaryVisibilitySpecularLaneCount() {
  return s_lastWavefrontStats[48];
}

UINT GetWavefrontSecondaryVisibilityHitCount() {
  return s_lastWavefrontSecondaryVisibilityHitCount;
}

UINT GetWavefrontSecondaryVisibilityMissCount() {
  return s_lastWavefrontSecondaryVisibilityMissCount;
}

UINT GetWavefrontSecondaryResolveRecordCount() {
  return s_lastWavefrontSecondaryResolveRecordCount;
}

UINT GetWavefrontSecondaryResolveSurfaceCount() {
  return s_lastWavefrontSecondaryResolveSurfaceCount;
}

UINT GetWavefrontSecondaryResolveSkyCount() {
  return s_lastWavefrontSecondaryResolveSkyCount;
}

UINT GetWavefrontShadowVisibilityTaskCount() {
  return s_lastWavefrontShadowVisibilityTaskCount;
}

UINT GetWavefrontShadowVisibleCount() {
  return s_lastWavefrontShadowVisibleCount;
}

UINT GetWavefrontShadowOccludedCount() {
  return s_lastWavefrontShadowOccludedCount;
}

UINT GetWavefrontPrimaryMaterialBinCount(WavefrontMaterialBin materialBin) {
  const UINT materialBinIndex = static_cast<UINT>(materialBin);
  if (materialBinIndex >= kWavefrontMaterialBinCount) {
    return 0;
  }
  return s_lastWavefrontStats[kWavefrontPrimaryMaterialBinStatsBase +
                              materialBinIndex];
}

UINT GetWavefrontSecondaryMaterialBinCount(WavefrontMaterialBin materialBin) {
  const UINT materialBinIndex = static_cast<UINT>(materialBin);
  if (materialBinIndex >= kWavefrontMaterialBinCount) {
    return 0;
  }
  return s_lastWavefrontStats[kWavefrontSecondaryMaterialBinStatsBase +
                              materialBinIndex];
}

void SetDenoiserMode(DenoiserMode m) {
  if (s_denoiserMode == m)
    return;
  s_denoiserMode = m;

  if (s_denoiserMode == DenoiserMode::OIDN_CPU ||
      s_denoiserMode == DenoiserMode::OIDN_GPU) {
    s_oidnDenoiser.Initialize(s_device);
    s_optixDenoiser.Shutdown();
  } else if (s_denoiserMode == DenoiserMode::OptiX) {
    s_optixDenoiser.Initialize(s_device);
    s_oidnDenoiser.Shutdown();
  } else {
    s_oidnDenoiser.Shutdown();
    s_optixDenoiser.Shutdown();
  }
  PrepareSelectedFinalDenoiserResources();
  // Reset accumulation as denoiser mode change may affect post-process outputs
  DxrRenderer::ResetAccumulation();
}

DenoiserMode GetDenoiserMode() { return s_denoiserMode; }

void SetOidnQuality(OidnDenoiser::Quality q) {
  if (s_oidnQuality == q) {
    return;
  }
  s_oidnQuality = q;
  s_oidnDenoiser.SetQuality(q);
  s_hasDenoised = false;
  s_hasTonemappedFrame = false;
  PrepareSelectedFinalDenoiserResources();
  QueueInteractiveWake("OIDN quality changed");
}

OidnDenoiser::Quality GetOidnQuality() { return s_oidnQuality; }

UINT GetAccumulationFrameCount() { return s_accumulation.GetFrameCount(); }

UINT GetDisplayedSampleCount() {
  const bool dlssActive =
      (s_streamline && s_streamline->IsInitialized() &&
       s_streamline->IsDeviceSet() && s_streamline->IsEnabled() &&
       s_streamline->GetMode() != StreamlineManager::Mode::Off);
  const bool rrActive =
      dlssActive && (s_streamline->GetMode() ==
                     StreamlineManager::Mode::DLSS_RayReconstruction);
  return rrActive ? s_rrStillFrameSpp : s_accumulation.GetFrameCount();
}

bool CanIdleWithoutRendering() {
  if (s_finalDisplayState == FinalDisplayState::FinalDenoisePending) {
    return false;
  }

  if (!s_outputUAV || !s_tonemapOutputUAV) {
    return false;
  }

  const bool dlssActive =
      (s_streamline && s_streamline->IsInitialized() &&
       s_streamline->IsDeviceSet() && s_streamline->IsEnabled() &&
       s_streamline->GetMode() != StreamlineManager::Mode::Off);
  const bool rrActive =
      dlssActive && (s_streamline->GetMode() ==
                     StreamlineManager::Mode::DLSS_RayReconstruction);

  const UINT currSpp = GetDisplayedSampleCount();
  const UINT maxSpp =
      (g_cameraData.maxSPP > 0.0f) ? (UINT)g_cameraData.maxSPP : 0u;

  bool isConverged = false;
  if (s_hasNoiseEstimate) {
    const bool adaptiveEnabled = (g_cameraData.useAdaptiveSampling > 0.5f);
    const UINT minNoiseStopSpp = adaptiveEnabled ? 32u : 24u;
    const float stopThreshold = g_cameraData.noiseThreshold * 0.90f;
    const float resumeThreshold = g_cameraData.noiseThreshold * 1.20f;
    if (currSpp >= minNoiseStopSpp) {
      if (s_noiseConvergedLatched) {
        if (s_lastNoiseLevel > resumeThreshold) {
          isConverged = false;
        } else {
          isConverged = true;
        }
      } else if (s_lastNoiseLevel <= stopThreshold) {
        isConverged = true;
      }
    }
  }

  const bool reachedEndCondition =
      ((maxSpp > 0 && currSpp >= maxSpp) || isConverged);
  if (!reachedEndCondition) {
    return false;
  }

  const bool isFinalDenoiserMode =
      (s_denoiserMode != DxrRenderer::DenoiserMode::Off && !dlssActive);
  if (isFinalDenoiserMode && !s_hasDenoised) {
    return false;
  }

  if (!s_hasTonemappedFrame) {
    return false;
  }

  if (s_asyncRestirPending || s_asyncComputePendingFenceWait > 0) {
    return false;
  }

  if (rrActive && s_streamlineResetHistory) {
    return false;
  }

  return true;
}

UINT GetLightCount() { return s_lightCount; }

// RR jitter scale accessors
void SetRrJitterScale(float scale) {
  if (scale < 0.0f)
    scale = 0.0f;
  if (scale > 1.0f)
    scale = 1.0f;
  s_rrJitterScale = scale;
}

float GetRrJitterScale() { return s_rrJitterScale; }

bool IsReady() {
  return g_rayTracingSupported && s_rtStateObject != nullptr;
}

bool RenderFrame(ID3D12GraphicsCommandList *commandListBase,
                 ID3D12CommandAllocator *cmdAlloc, UINT frameIndex,
                 ID3D12Resource *renderTarget,
                 D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
                 ID3D12Resource *cameraCB, ID3D12Resource *materialCB,
                 D3D12_GPU_DESCRIPTOR_HANDLE texturesGpuStart,
                 UINT textureDescriptorCount,
                 const std::vector<const Asset::GpuMesh *> &meshes,
                 ID3D12Resource *meshDataSB,
                 ID3D12Resource *materialExtraSB, UINT presentationX,
                 UINT presentationY, UINT presentationWidth,
                 UINT presentationHeight) {
  auto ReturnFail = [&](int reason, const char *message) -> bool {
    if (s_lastRenderFrameFailReason != reason) {
      fprintf(stderr, "DxrRenderer::RenderFrame FAIL[%d]: %s\n", reason,
              message);
      s_lastRenderFrameFailReason = reason;
    }
    return false;
  };

  (void)frameIndex;
  if (!g_rayTracingSupported || !s_rtStateObject || !s_srvHeap) {
    return ReturnFail(1,
                      "DXR core state missing (support/stateObject/srvHeap)");
  }
  EnsureCurrentFeatureResources();
  if (!renderTarget) {
    return ReturnFail(2, "renderTarget is null");
  }

  // Material edits only require AS rebuild when opaque-vs-nonopaque state
  // changes (affects BLAS geometry flags / AnyHit path).
  const bool grassTlasCameraChanged = GrassTlasNeedsCameraRefresh();
  if (s_forceAsRebuild || s_forceTlasUpdate || grassTlasCameraChanged ||
      (!s_tlas.result && !meshes.empty())) {
    BuildAccelerationStructures(meshes, Scene::GetInstances());
    if (!s_tlas.result) {
      return ReturnFail(16, "TLAS missing after forced rebuild");
    }
    if (grassTlasCameraChanged) {
      ResetAccumulation();
    }
    s_forceAsRebuild = false;
    s_forceTlasUpdate = false;
  }

  // Handle empty scene or missing TLAS gracefully after honoring any pending
  // rebuild/update request, so deferred invalidation can bootstrap DXR.
  if (meshes.empty() || !s_tlas.result) {
    s_lastRenderFrameFailReason = -1;
    TransitionResource(commandListBase, renderTarget,
                       D3D12_RESOURCE_STATE_PRESENT,
                       D3D12_RESOURCE_STATE_RENDER_TARGET);
    FLOAT clearColor[] = {0.1f, 0.1f, 0.12f, 1.0f};
    commandListBase->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    commandListBase->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
    return true;
  }

  if (HasDirtyMaterialsForMeshes(meshes)) {
    bool opacityStateChanged = (s_cachedMeshOpaqueForBlas.size() != meshes.size());
    if (!opacityStateChanged) {
      for (size_t i = 0; i < meshes.size(); ++i) {
        const uint8_t nowOpaque = IsMeshOpaqueForRt(*meshes[i]) ? 1u : 0u;
        if (nowOpaque != s_cachedMeshOpaqueForBlas[i]) {
          opacityStateChanged = true;
          break;
        }
      }
    }

    if (opacityStateChanged) {
      BuildAccelerationStructures(meshes, Scene::GetInstances());
      if (!s_tlas.result) {
        return ReturnFail(15,
                          "TLAS missing after dirty-material rebuild attempt");
      }
    } else {
      // Pure shading edits (e.g. roughness/albedo) don't need BLAS/TLAS work.
      ClearDirtyMaterialsForMeshes(meshes);
    }
  }

  if (!s_outputUAV)
    return ReturnFail(3, "s_outputUAV is null");

  ComPtr<ID3D12GraphicsCommandList4> dxrList;
  HRESULT hrAsList4 = commandListBase->QueryInterface(IID_PPV_ARGS(&dxrList));
  if (FAILED(hrAsList4))
    return ReturnFail(4, "QueryInterface(ID3D12GraphicsCommandList4) failed");

  s_lastRenderFrameFailReason = -1;
  EnsureAsyncComputeContext();
  if (s_asyncRestirAvailable && s_asyncComputePendingFenceWait > 0) {
    s_commandQueue->Wait(s_asyncComputeFence.Get(),
                         s_asyncComputePendingFenceWait);
    s_asyncComputePendingFenceWait = 0;
  }

  // If the user changed manual intensity or exposure compensation while the
  // renderer is frozen at max SPP, ensure we re-run tonemapping so the
  // displayed image updates immediately.
  {
    float curIntensity = g_cameraData.intensity;
    if (std::abs(curIntensity - s_lastCameraIntensity) > 1e-6f) {
      s_lastCameraIntensity = curIntensity;
      s_hasTonemappedFrame = false;
    }
    if (std::abs(s_exposureCompensation - s_lastExposureCompensation) > 1e-6f) {
      s_lastExposureCompensation = s_exposureCompensation;
      s_hasTonemappedFrame = false;
    }
  }

  const bool dlssActive =
      (s_streamline && s_streamline->IsInitialized() &&
       s_streamline->IsDeviceSet() && s_streamline->IsEnabled() &&
       s_streamline->GetMode() != StreamlineManager::Mode::Off);
  const bool rrActive =
      dlssActive && (s_streamline->GetMode() ==
                     StreamlineManager::Mode::DLSS_RayReconstruction);

  bool usedFinalDenoiser = false;

  // If we've hit maxSPP and the camera/settings haven't changed (meaning
  // ResetAccumulation hasn't been called), freeze rendering and keep presenting
  // the last tonemapped output. This works for both accumulation and DLSS-RR.
  const UINT maxSpp =
      (g_cameraData.maxSPP > 0.0f) ? (UINT)g_cameraData.maxSPP : 0u;
  const UINT currSpp =
      rrActive ? s_rrStillFrameSpp : s_accumulation.GetFrameCount();
  const bool debugViewActive = (g_cameraData.debugMode != 0.0f) ||
                               (g_cameraData.debugVisualizationMode == 1.0f);

  // Global stop by measured noise with hysteresis to avoid stop/resume flicker.
  bool isConverged = false;
  if (s_hasNoiseEstimate) {
    const bool adaptiveEnabled = (g_cameraData.useAdaptiveSampling > 0.5f);
    const UINT minNoiseStopSpp = adaptiveEnabled ? 32u : 24u;
    const float stopThreshold = g_cameraData.noiseThreshold * 0.90f;
    const float resumeThreshold = g_cameraData.noiseThreshold * 1.20f;
    if (currSpp >= minNoiseStopSpp) {
      if (s_noiseConvergedLatched) {
        if (s_lastNoiseLevel > resumeThreshold) {
          s_noiseConvergedLatched = false;
        }
      } else if (s_lastNoiseLevel <= stopThreshold) {
        s_noiseConvergedLatched = true;
      }
    }
    isConverged = s_noiseConvergedLatched;
  }
  bool isFinalDenoiserMode =
      (s_denoiserMode != DxrRenderer::DenoiserMode::Off && !dlssActive);
  const uint32_t dxrFeatureMask =
      ComputeDxrFeatureMask(dlssActive, rrActive, debugViewActive,
                            isFinalDenoiserMode);
  bool reachedEndCondition = ((maxSpp > 0 && currSpp >= maxSpp) || isConverged);

  bool canAutoDenoise =
      isFinalDenoiserMode && reachedEndCondition && !s_hasDenoised;
  bool doDenoise = canAutoDenoise;
  if (!reachedEndCondition) {
    s_finalDisplayState = FinalDisplayState::Rendering;
  } else if (doDenoise) {
    s_finalDisplayState = FinalDisplayState::FinalDenoisePending;
  }

  // Flag to freeze after tonemapping instead of early return
  bool shouldFreezeAfterTonemap = reachedEndCondition && !doDenoise;

  // Bake clouds before DXR state binding. Keep this cooperative with the path
  // tracer: preview bakes can run in tiny background slices, but the full 4K
  // final bake waits until DXR is not actively adding samples. Otherwise a
  // scene load can stack cloud raymarch compute and DXR transport into one
  // TDR-prone frame.
  if (g_cloudManager.NeedsBake()) {
    const bool dxrAddingSamples = !doDenoise && !reachedEndCondition;
    if (!dxrAddingSamples || !g_cloudManager.FinalBakeRequested()) {
      g_cloudManager.BakeSky(commandListBase, cameraCB, dxrAddingSamples);
    }
  }

  // Set pipeline and root signature
  dxrList->SetPipelineState1(s_rtStateObject.Get());
  dxrList->SetComputeRootSignature(s_rtGlobalRootSignature.Get());

  // Bind TLAS
  dxrList->SetComputeRootShaderResourceView(
      0, s_tlas.result->GetGPUVirtualAddress());

  // fprintf(stderr, "DxrRenderer: RenderFrame - SetRootSignature done\n");

  if (g_cbvSrvAllocator.Heap() && textureDescriptorCount > 0) {
    // Keep local texture table resident in the DXR heap, with adaptive mip
    // clamping under GPU pressure.
    UpdateTextureDescriptorTable(texturesGpuStart, textureDescriptorCount);
  }
  // fprintf(stderr, "DxrRenderer: RenderFrame - CopyDescriptorsSimple done\n");

  // Bind descriptor heaps
  ID3D12DescriptorHeap *heaps[] = {s_srvHeap.Get()};
  dxrList->SetDescriptorHeaps(1, heaps);

  // Compute jitter for this frame.
  // Note: DLSS expects jitter in range [-0.5, 0.5] pixel space.
  s_jitterFrameIndex++;
  uint32_t frameIdx = s_jitterFrameIndex;
  float jitterX = Halton(frameIdx, 2) - 0.5f;
  float jitterY = Halton(frameIdx, 3) - 0.5f;

  // DLSS-RR can shimmer at silhouettes because pixel jitter causes much larger
  // ray-direction changes near the screen edges in a perspective camera.
  // Allow reducing jitter amplitude in RR mode as a stability/quality trade.
  if (rrActive) {
    jitterX *= s_rrJitterScale;
    jitterY *= s_rrJitterScale;
  }

  // Expose the final jitter values for UI/debug overlays.
  s_lastJitterX = jitterX;
  s_lastJitterY = jitterY;

  CameraCB asyncCameraSnapshot = {};
  bool hasAsyncCameraSnapshot = false;

  // Update frame count in camera CB if present
  if (cameraCB) {
    void *pData = nullptr;
    D3D12_RANGE readRange = {0, 0};
    if (SUCCEEDED(cameraCB->Map(0, &readRange, &pData))) {
      CameraCB *cam = reinterpret_cast<CameraCB *>(pData);
      cam->_pad1 = jitterX;
      cam->_pad2 = jitterY;

      // Monotonic frame count for RNG / temporal logic.
      cam->frameCount = (float)s_jitterFrameIndex;
      cam->lightCount = (float)s_lightCount;
      cam->tonemapAoIntensity = s_tonemapAoIntensity;
      cam->tonemapAoRadiusMeters = s_tonemapAoLengthMm * 0.001f;
      cam->tonemapAoMode = static_cast<float>(static_cast<int>(s_tonemapAoMode));
      cam->triPlanarWorldRotationDegrees =
          g_cameraData.triPlanarWorldRotationDegrees;
      cam->dxrFeatureFlags = static_cast<float>(dxrFeatureMask);

      // Keep actual still-frame count even for RR so shaders can compute
      // variance/noise for adaptive sampling and diagnostics.
      cam->accumulationCount = (float)currSpp;

      // Streamline flags used by raytracing shaders.
      cam->dlssEnabled = dlssActive ? 1.0f : 0.0f;
      cam->dlssRayReconstruction = rrActive ? 1.0f : 0.0f;
      asyncCameraSnapshot = *cam;
      hasAsyncCameraSnapshot = true;

      cameraCB->Unmap(0, nullptr);
    }
  }

  // Bind Tables
  dxrList->SetComputeRootDescriptorTable(1, s_outputUAVGpu);
  dxrList->SetComputeRootDescriptorTable(2, s_texTableGpu);
  if (cameraCB)
    dxrList->SetComputeRootConstantBufferView(3,
                                              cameraCB->GetGPUVirtualAddress());
  if (materialCB)
    dxrList->SetComputeRootShaderResourceView(
        4, materialCB->GetGPUVirtualAddress());
  if (materialExtraSB)
    dxrList->SetComputeRootShaderResourceView(
        12, materialExtraSB->GetGPUVirtualAddress());
  D3D12_GPU_VIRTUAL_ADDRESS grassBladesGpu =
      GrassManager::GetRayTracingInstanceBufferGpuAddress();
  dxrList->SetComputeRootShaderResourceView(13, grassBladesGpu);
  dxrList->SetComputeRoot32BitConstants(14, 1, &s_grassTlasStartIndex, 0);

  // --- Bind Cloud Resources (Slot 10) ---
  if (g_cloudManager.GetBaseTexture() && g_cloudManager.GetDetailTexture()) {
    if (!s_cloudDescriptorsDone) {
      // 1. Skip Cloud CBV creation in Heap (Used Root Descriptor instead)

      // 2. Create Cloud Base SRV at DXR_HEAP_CLOUD_TEX_OFFSET
      D3D12_CPU_DESCRIPTOR_HANDLE srvCpu =
          s_srvHeap->GetCPUDescriptorHandleForHeapStart();
      srvCpu.ptr += (SIZE_T)DXR_HEAP_CLOUD_TEX_OFFSET *
                    s_device->GetDescriptorHandleIncrementSize(
                        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

      D3D12_RESOURCE_DESC noiseDesc =
          g_cloudManager.GetBaseTexture()->GetDesc();
      D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
      srvDesc.Format = noiseDesc.Format;
      srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
      srvDesc.Shader4ComponentMapping =
          D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      srvDesc.Texture3D.MipLevels = noiseDesc.MipLevels;
      srvDesc.Texture3D.MostDetailedMip = 0;
      srvDesc.Texture3D.ResourceMinLODClamp = 0.0f;
      s_device->CreateShaderResourceView(g_cloudManager.GetBaseTexture(),
                                         &srvDesc, srvCpu);

      // 3. Create Cloud Detail SRV at DXR_HEAP_CLOUD_DETAIL_TEX_OFFSET
      D3D12_CPU_DESCRIPTOR_HANDLE srvCpuDetail =
          s_srvHeap->GetCPUDescriptorHandleForHeapStart();
      srvCpuDetail.ptr += (SIZE_T)DXR_HEAP_CLOUD_DETAIL_TEX_OFFSET *
                          s_device->GetDescriptorHandleIncrementSize(
                              D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

      D3D12_RESOURCE_DESC detailDesc =
          g_cloudManager.GetDetailTexture()->GetDesc();
      D3D12_SHADER_RESOURCE_VIEW_DESC srvDescDetail = {};
      srvDescDetail.Format = detailDesc.Format;
      srvDescDetail.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
      srvDescDetail.Shader4ComponentMapping =
          D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      srvDescDetail.Texture3D.MipLevels = detailDesc.MipLevels;
      srvDescDetail.Texture3D.MostDetailedMip = 0;
      srvDescDetail.Texture3D.ResourceMinLODClamp = 0.0f;
      s_device->CreateShaderResourceView(g_cloudManager.GetDetailTexture(),
                                         &srvDescDetail, srvCpuDetail);

      s_cloudDescriptorsDone = true;
    }

  }
  // Preview and final cloud bakes use different textures. Keep the DXR heap
  // pointed at whichever bake the cloud manager most recently published.
  if (s_device && s_srvHeap && g_cloudManager.GetBakedSkyTexture()) {
    D3D12_CPU_DESCRIPTOR_HANDLE srvCpuBaked =
        s_srvHeap->GetCPUDescriptorHandleForHeapStart();
    srvCpuBaked.ptr += (SIZE_T)DXR_HEAP_CLOUD_BAKED_TEX_OFFSET *
                       s_device->GetDescriptorHandleIncrementSize(
                           D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDescBaked = {};
    srvDescBaked.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    srvDescBaked.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDescBaked.Texture2D.MipLevels = 1;
    srvDescBaked.Texture2D.MostDetailedMip = 0;
    srvDescBaked.Shader4ComponentMapping =
        D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    s_device->CreateShaderResourceView(g_cloudManager.GetBakedSkyTexture(),
                                       &srvDescBaked, srvCpuBaked);
  }
  dxrList->SetComputeRootConstantBufferView(
      10, g_cloudManager.GetConstantBufferAddr());
  D3D12_GPU_DESCRIPTOR_HANDLE cloudSRV =
      s_srvHeap->GetGPUDescriptorHandleForHeapStart();
  cloudSRV.ptr += (UINT64)DXR_HEAP_CLOUD_TEX_OFFSET *
                  s_device->GetDescriptorHandleIncrementSize(
                      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  dxrList->SetComputeRootDescriptorTable(11, cloudSRV);

  // Always bind IBL descriptors (env map + importance CDFs)
  {
    // 1) Env map descriptor copied from global heap (allocated in main.cpp)
    D3D12_CPU_DESCRIPTOR_HANDLE dst =
        s_srvHeap->GetCPUDescriptorHandleForHeapStart();
    dst.ptr += (SIZE_T)DXR_HEAP_IBL_OFFSET *
               s_device->GetDescriptorHandleIncrementSize(
                   D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // GetCPUHandle should now always be valid (allocated in main.cpp)
    D3D12_CPU_DESCRIPTOR_HANDLE src = IBLManager::Get().GetCPUHandle();
    if (src.ptr != 0) {
      s_device->CopyDescriptorsSimple(1, dst, src,
                                      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    // 2) Conditional CDF (t1, space1)
    D3D12_CPU_DESCRIPTOR_HANDLE dstConditional =
        s_srvHeap->GetCPUDescriptorHandleForHeapStart();
    dstConditional.ptr += (SIZE_T)DXR_HEAP_IBL_CONDITIONAL_CDF_OFFSET *
                          s_device->GetDescriptorHandleIncrementSize(
                              D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_SHADER_RESOURCE_VIEW_DESC condSrv = {};
    condSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    condSrv.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    condSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    condSrv.Texture2D.MostDetailedMip = 0;
    condSrv.Texture2D.MipLevels = 1;
    condSrv.Texture2D.ResourceMinLODClamp = 0.0f;

    if (IBLManager::Get().HasEnvImportanceData() &&
        IBLManager::Get().GetEnvConditionalCdf().resource) {
      condSrv.Format = IBLManager::Get().GetEnvConditionalCdf().format;
      condSrv.Texture2D.MipLevels =
          IBLManager::Get().GetEnvConditionalCdf().mipLevels;
      s_device->CreateShaderResourceView(
          IBLManager::Get().GetEnvConditionalCdf().resource.Get(), &condSrv,
          dstConditional);
    } else {
      s_device->CreateShaderResourceView(nullptr, &condSrv, dstConditional);
    }

    // 3) Marginal CDF (t2, space1)
    D3D12_CPU_DESCRIPTOR_HANDLE dstMarginal =
        s_srvHeap->GetCPUDescriptorHandleForHeapStart();
    dstMarginal.ptr += (SIZE_T)DXR_HEAP_IBL_MARGINAL_CDF_OFFSET *
                       s_device->GetDescriptorHandleIncrementSize(
                           D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_SHADER_RESOURCE_VIEW_DESC marginalSrv = {};
    marginalSrv.Shader4ComponentMapping =
        D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    marginalSrv.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    marginalSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    marginalSrv.Texture2D.MostDetailedMip = 0;
    marginalSrv.Texture2D.MipLevels = 1;
    marginalSrv.Texture2D.ResourceMinLODClamp = 0.0f;

    if (IBLManager::Get().HasEnvImportanceData() &&
        IBLManager::Get().GetEnvMarginalCdf().resource) {
      marginalSrv.Format = IBLManager::Get().GetEnvMarginalCdf().format;
      marginalSrv.Texture2D.MipLevels =
          IBLManager::Get().GetEnvMarginalCdf().mipLevels;
      s_device->CreateShaderResourceView(
          IBLManager::Get().GetEnvMarginalCdf().resource.Get(), &marginalSrv,
          dstMarginal);
    } else {
      s_device->CreateShaderResourceView(nullptr, &marginalSrv, dstMarginal);
    }

    dxrList->SetComputeRootDescriptorTable(8, s_iblGpuHandle);
  }

  // Bind VB and IB Tables
  dxrList->SetComputeRootDescriptorTable(5, s_vbTableGpu);
  dxrList->SetComputeRootDescriptorTable(6, s_ibTableGpu);
  if (meshDataSB)
    dxrList->SetComputeRootShaderResourceView(
        7, meshDataSB->GetGPUVirtualAddress());

  // Lights SB (t5000)
  dxrList->SetComputeRootShaderResourceView(
      9, s_lightBuffer ? s_lightBuffer->GetGPUVirtualAddress() : 0);

  auto BindRayTracingGlobalRoot = [&]() {
    dxrList->SetPipelineState1(s_rtStateObject.Get());
    dxrList->SetComputeRootSignature(s_rtGlobalRootSignature.Get());

    ID3D12DescriptorHeap *rtHeaps[] = {s_srvHeap.Get()};
    dxrList->SetDescriptorHeaps(1, rtHeaps);

    dxrList->SetComputeRootShaderResourceView(
        0, s_tlas.result->GetGPUVirtualAddress());
    dxrList->SetComputeRootDescriptorTable(1, s_outputUAVGpu);
    dxrList->SetComputeRootDescriptorTable(2, s_texTableGpu);
    if (cameraCB) {
      dxrList->SetComputeRootConstantBufferView(
          3, cameraCB->GetGPUVirtualAddress());
    }
    if (materialCB) {
      dxrList->SetComputeRootShaderResourceView(
          4, materialCB->GetGPUVirtualAddress());
    }
    dxrList->SetComputeRootDescriptorTable(5, s_vbTableGpu);
    dxrList->SetComputeRootDescriptorTable(6, s_ibTableGpu);
    if (meshDataSB) {
      dxrList->SetComputeRootShaderResourceView(
          7, meshDataSB->GetGPUVirtualAddress());
    }
    dxrList->SetComputeRootDescriptorTable(8, s_iblGpuHandle);
    dxrList->SetComputeRootShaderResourceView(
        9, s_lightBuffer ? s_lightBuffer->GetGPUVirtualAddress() : 0);
    dxrList->SetComputeRootConstantBufferView(
        10, g_cloudManager.GetConstantBufferAddr());
    D3D12_GPU_DESCRIPTOR_HANDLE cloudSRV =
        s_srvHeap->GetGPUDescriptorHandleForHeapStart();
    cloudSRV.ptr += (UINT64)DXR_HEAP_CLOUD_TEX_OFFSET *
                    s_device->GetDescriptorHandleIncrementSize(
                        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    dxrList->SetComputeRootDescriptorTable(11, cloudSRV);
    if (materialExtraSB) {
      dxrList->SetComputeRootShaderResourceView(
          12, materialExtraSB->GetGPUVirtualAddress());
    }
    dxrList->SetComputeRootShaderResourceView(13, grassBladesGpu);
    dxrList->SetComputeRoot32BitConstants(14, 1, &s_grassTlasStartIndex, 0);
  };

  // (Legacy) maxSPP early-out used to be here. We now freeze using the
  // tonemapped output above so it also works with DLSS-RR.

  // Clear accumulation buffer if needed
  if (s_accumulation.NeedsClear()) {
    // Start ReSTIR timer
    if (s_queryHeap) {
      dxrList->EndQuery(s_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                        1); // ReSTIR start
    }

    D3D12_CPU_DESCRIPTOR_HANDLE accumCpu =
        s_srvHeap->GetCPUDescriptorHandleForHeapStart();
    accumCpu.ptr += (SIZE_T)DXR_HEAP_ACCUM_UAV_OFFSET *
                    s_device->GetDescriptorHandleIncrementSize(
                        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE varCpu =
        s_srvHeap->GetCPUDescriptorHandleForHeapStart();
    varCpu.ptr += (SIZE_T)DXR_HEAP_VARIANCE_UAV_OFFSET *
                  s_device->GetDescriptorHandleIncrementSize(
                      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    s_accumulation.Clear(dxrList.Get(), s_accumUAVGpu, accumCpu,
                         s_varianceUAVGpu, varCpu);
    D3D12_CPU_DESCRIPTOR_HANDLE transAccumCpu =
        s_srvHeap->GetCPUDescriptorHandleForHeapStart();
    transAccumCpu.ptr +=
        (SIZE_T)DXR_HEAP_TRANSMISSION_ACCUM_OFFSET *
        s_device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE transVarCpu =
        s_srvHeap->GetCPUDescriptorHandleForHeapStart();
    transVarCpu.ptr +=
        (SIZE_T)DXR_HEAP_TRANSMISSION_VARIANCE_OFFSET *
        s_device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    s_transmissionAccumulation.Clear(
        dxrList.Get(), s_transmissionAccumUAVGpu, transAccumCpu,
        s_transmissionVarianceUAVGpu, transVarCpu);

    // Also clear reservoir buffers to prevent artifacts from stale data
    // Important: lightIndex should be cleared to 0xFFFFFFFF (invalid)
    uint32_t clearUint[4] = {0xFFFFFFFF, 0, 0, 0};
    float clearRes[4];
    memcpy(clearRes, clearUint, sizeof(clearUint));

    for (int i = 0; i < 2; ++i) {
      if (s_reservoirBuffers[i]) {
        D3D12_CPU_DESCRIPTOR_HANDLE resCpu =
            s_srvHeap->GetCPUDescriptorHandleForHeapStart();
        resCpu.ptr += (SIZE_T)(i == 0 ? DXR_HEAP_RESERVOIR_0_OFFSET
                                      : DXR_HEAP_RESERVOIR_1_OFFSET) *
                      s_device->GetDescriptorHandleIncrementSize(
                          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        dxrList->ClearUnorderedAccessViewUint(s_reservoirGpuHandle[i], resCpu,
                                              s_reservoirBuffers[i].Get(),
                                              clearUint, 0, nullptr);
      }
    }
    // GI reservoirs use different packing but hitPos=0 is fine for clearing
    float clearGI[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    for (int i = 0; i < 6; ++i) {
      if (s_gi_reservoirBuffers[i]) {
        UINT offset = 0;
        switch (i) {
        case 0:
          offset = DXR_HEAP_GI_RESERVOIR_0_OFFSET_A;
          break;
        case 1:
          offset = DXR_HEAP_GI_RESERVOIR_0_OFFSET_B;
          break;
        case 2:
          offset = DXR_HEAP_GI_RESERVOIR_0_OFFSET_C;
          break;
        case 3:
          offset = DXR_HEAP_GI_RESERVOIR_1_OFFSET_A;
          break;
        case 4:
          offset = DXR_HEAP_GI_RESERVOIR_1_OFFSET_B;
          break;
        case 5:
          offset = DXR_HEAP_GI_RESERVOIR_1_OFFSET_C;
          break;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE resCpu =
            s_srvHeap->GetCPUDescriptorHandleForHeapStart();
        resCpu.ptr +=
            (SIZE_T)offset * s_device->GetDescriptorHandleIncrementSize(
                                 D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        dxrList->ClearUnorderedAccessViewFloat(
            s_gi_reservoirGpuHandle[i], resCpu, s_gi_reservoirBuffers[i].Get(),
            clearGI, 0, nullptr);
      }
    }

    // Ensure clears are finished before DispatchRays / ReSTIR
    D3D12_RESOURCE_BARRIER uavBarrier = {};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = nullptr; // Global UAV barrier
    dxrList->ResourceBarrier(1, &uavBarrier);

    // Clear shader counters (debug instrumentation)
    if (ShaderCountersEnabled() && s_shaderCountersBuffer) {
      UINT zeroVals[4] = {0, 0, 0, 0};
      UINT inc = s_device->GetDescriptorHandleIncrementSize(
          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
      D3D12_CPU_DESCRIPTOR_HANDLE cpuCounters =
          s_srvHeap->GetCPUDescriptorHandleForHeapStart();
      cpuCounters.ptr += (SIZE_T)DXR_HEAP_SHADER_COUNTERS_OFFSET * inc;
      dxrList->ClearUnorderedAccessViewUint(
          s_shaderCountersGpuHandle, cpuCounters, s_shaderCountersBuffer.Get(),
          zeroVals, 0, nullptr);
    }

    // End ReSTIR timer
    if (s_queryHeap) {
      dxrList->EndQuery(s_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                        2); // ReSTIR end
    }

    s_accumulation.SetNeedsClear(false);
    s_transmissionAccumulation.SetNeedsClear(false);
  }

  // Only Dispatch Rays if we are NOT in a pure denoise pass.
  // If we are denoising an already-completed frame (e.g. at MaxSPP),
  // we do not want to add more samples or modify the accumulation buffer.

  bool didPathTracingWork = false;
  bool didWavefrontRestirSeed = false;
  if (!doDenoise && !reachedEndCondition) {
    // Start DispatchRays timer
    if (s_queryHeap) {
      dxrList->EndQuery(s_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                        3); // Dispatch start
    }

    // Ensure shader counters are reset per-frame (debug instrumentation)
    if (ShaderCountersEnabled() && s_shaderCountersBuffer) {
      UINT zeros[4] = {0, 0, 0, 0};
      UINT inc = s_device->GetDescriptorHandleIncrementSize(
          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
      D3D12_CPU_DESCRIPTOR_HANDLE cpuCounters =
          s_srvHeap->GetCPUDescriptorHandleForHeapStart();
      cpuCounters.ptr += (SIZE_T)DXR_HEAP_SHADER_COUNTERS_OFFSET * inc;
      dxrList->ClearUnorderedAccessViewUint(
          s_shaderCountersGpuHandle, cpuCounters, s_shaderCountersBuffer.Get(),
          zeros, 0, nullptr);
    }
    if (cameraCB) {
      SetWavefrontStage("bootstrap");
      DispatchWavefrontBootstrap(dxrList.Get(), cameraCB);
      DispatchWavefrontPrepareIndirectArgs(
          dxrList.Get(), kWavefrontQueuePathA, 0u, ~0u, 0u);
      DispatchWavefrontCounterReset(dxrList.Get(), kWavefrontMaterialBinCounterBase, 0u, kWavefrontMaterialBinCount);
      BindRayTracingGlobalRoot();
      SetWavefrontStage("primary-visibility");
      DispatchWavefrontPrimaryVisibility(dxrList.Get());

      // Record indirect dispatch updates whenever the active shader table
      // pointers change. The cache is module state so pipeline recreates can
      // reset it explicitly instead of leaving hidden stale function statics.
      if (s_wavefrontSecondaryRayGenShaderTable !=
              s_uploadedWavefrontSecondaryRayGen ||
          s_wavefrontShadowRayGenShaderTable !=
              s_uploadedWavefrontShadowRayGen) {
        UploadWavefrontIndirectDispatchRecords(dxrList.Get());
        if (s_wavefrontSecondaryRayGenShaderTable != 0 &&
            s_wavefrontShadowRayGenShaderTable != 0) {
          s_uploadedWavefrontSecondaryRayGen =
              s_wavefrontSecondaryRayGenShaderTable;
          s_uploadedWavefrontShadowRayGen = s_wavefrontShadowRayGenShaderTable;
        }
      }

      if (s_pathTracingBackend == PathTracingBackend::WavefrontParity) {
        // Phase 2 parity gate: resolve queue-backed primary hits into the
        // existing output/AOV surfaces without scheduling transport work.
        SetWavefrontStage("primary-surface-resolve");
        DispatchWavefrontResolvePrimary(
            dxrList.Get(), cameraCB, materialCB, meshDataSB, materialExtraSB,
            kWavefrontResolveFlagPrimarySurfaceOnly);
        didPathTracingWork = true;
      } else if (s_pathTracingBackend == PathTracingBackend::WavefrontOptimized) {
        ClearWavefrontShadowContribution(dxrList.Get());
        const UINT wavefrontTransportFlags =
            kWavefrontResolveFlagDeferAccumulation;

        // ReSTIR DI seed: consume queue-produced primary hit records as the
        // scheduler task source. Spatial reuse still runs later on the same
        // reservoir textures used by legacy.
        SetWavefrontStage("restir-seed");
        for (UINT materialBin = 0; materialBin < kWavefrontMaterialBinCount; ++materialBin) {
          DispatchWavefrontPrepareIndirectArgs(
              dxrList.Get(), kWavefrontMaterialBinCounterBase + materialBin,
              materialBin, ~0u, 0u);
        }
        
        D3D12_RESOURCE_BARRIER toArgsBarrier = {};
        toArgsBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toArgsBarrier.Transition.pResource = s_wavefrontDispatchArgsBuffer.Get();
        toArgsBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        toArgsBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        toArgsBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        dxrList->ResourceBarrier(1, &toArgsBarrier);

        for (UINT materialBin = 0; materialBin < kWavefrontMaterialBinCount; ++materialBin) {
          const UINT binFlags =
              kWavefrontQueueFlagUseMaterialBinList |
              (materialBin << kWavefrontQueueFlagMaterialBinShift);
          DispatchWavefrontRestirSeed(dxrList.Get(), cameraCB, binFlags, true, materialBin, false);
        }
        
        D3D12_RESOURCE_BARRIER toUavBarrier = toArgsBarrier;
        toUavBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
        toUavBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        dxrList->ResourceBarrier(1, &toUavBarrier);

        DispatchWavefrontRestirSeed(dxrList.Get(), cameraCB,
                                    kWavefrontQueueFlagMissOnly, false);

        // Primary resolve: shade primary hits by material bin, read
        // scheduler-seeded DI reservoirs for direct-light tasks, seed GI,
        // enqueue secondary paths into queue B (counter 4), and emit primary
        // shadow tasks into shadow queue (counter 5). Misses run as a
        // separate full-range pass so sky pixels keep their AOV writes.
        SetWavefrontStage("primary-resolve");
        for (UINT materialBin = 0; materialBin < kWavefrontMaterialBinCount; ++materialBin) {
          DispatchWavefrontPrepareIndirectArgs(
              dxrList.Get(), kWavefrontMaterialBinCounterBase + materialBin,
              materialBin, ~0u, 0u);
        }
        
        dxrList->ResourceBarrier(1, &toArgsBarrier);

        for (UINT materialBin = 0; materialBin < kWavefrontMaterialBinCount; ++materialBin) {
          const UINT binFlags =
              kWavefrontQueueFlagUseMaterialBinList |
              (materialBin << kWavefrontQueueFlagMaterialBinShift) |
              wavefrontTransportFlags;
          DispatchWavefrontResolvePrimary(
              dxrList.Get(), cameraCB, materialCB, meshDataSB,
              materialExtraSB, binFlags, true, materialBin, false);
        }
        
        dxrList->ResourceBarrier(1, &toUavBarrier);

        DispatchWavefrontResolvePrimary(
            dxrList.Get(), cameraCB, materialCB, meshDataSB, materialExtraSB,
            kWavefrontQueueFlagMissOnly | wavefrontTransportFlags, false);

        // Barrier for s_outputUAV (g_output) to ensure ResolvePrimary writes are visible
        {
          D3D12_RESOURCE_BARRIER uavBarrier = {};
          uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
          uavBarrier.UAV.pResource = s_outputUAV.Get();
          dxrList->ResourceBarrier(1, &uavBarrier);
        }

        // Primary shadow visibility: trace the shadow rays queued by primary
        // resolve and accumulate direct lighting into the output buffer.
        DispatchWavefrontPrepareIndirectArgs(dxrList.Get(), kWavefrontQueueShadow, ~0u,
                                            kWavefrontShadowDispatchRaysReservedSlot,
                                            0u);
        BindRayTracingGlobalRoot();
        SetWavefrontStage("primary-shadow");
        DispatchWavefrontShadowVisibility(dxrList.Get());
        DispatchWavefrontCounterReset(dxrList.Get(), kWavefrontQueueShadow, 0u);

        // Secondary bounce loop: ping-pong between queue A (counter 0) and
        // queue B (counter 4) for at most ComputeWavefrontIndirectPassBudget()
        // bounce iterations.  Each iteration: prepare indirect args → secondary
        // visibility → secondary resolve → shadow visibility.
        const UINT maxBounces = ComputeWavefrontIndirectPassBudget();
        for (UINT bounce = 0u; bounce < maxBounces; ++bounce) {
          // Even bounce: source = queue B (counter 4), dest = queue A (counter 0)
          // Odd  bounce: source = queue A (counter 0), dest = queue B (counter 4)
          const bool sourceIsA = (bounce & 1u) != 0u;
          const UINT sourceCounter =
              sourceIsA ? kWavefrontQueuePathA : kWavefrontQueuePathB;
          const UINT destCounter =
              sourceIsA ? kWavefrontQueuePathB : kWavefrontQueuePathA;
          const UINT queueFlags    = sourceIsA ? kWavefrontQueueFlagSourceIsA : 0u;

          // Reset destination counter so secondary resolve allocates from 0.
          DispatchWavefrontCounterReset(dxrList.Get(), destCounter, 0u);

          // Write the indirect dispatch width and queue-selection flags for
          // secondary visibility (reserved slot) and secondary resolve (args[2]).
          DispatchWavefrontPrepareIndirectArgs(
              dxrList.Get(), sourceCounter,
              kWavefrontSecondaryResolveDispatchArgsIndex,
              kWavefrontSecondaryDispatchRaysReservedSlot, queueFlags);

          // Secondary visibility: trace continuation rays from source queue.
          DispatchWavefrontCounterReset(
              dxrList.Get(), kWavefrontMaterialBinCounterBase, 0u,
              kWavefrontMaterialBinCount);
          BindRayTracingGlobalRoot();
          SetWavefrontStage("secondary-visibility");
          DispatchWavefrontSecondaryVisibility(dxrList.Get(), sourceCounter);

          // Reset shadow counter before secondary resolve enqueues new tasks.
          DispatchWavefrontCounterReset(dxrList.Get(), kWavefrontQueueShadow, 0u);

          // Secondary resolve: shade secondary hits, enqueue continuations into
          // destination queue and new shadow tasks into shadow queue.
          SetWavefrontStage("secondary-resolve");
          for (UINT materialBin = 0; materialBin < kWavefrontMaterialBinCount; ++materialBin) {
            DispatchWavefrontPrepareIndirectArgs(
                dxrList.Get(), kWavefrontMaterialBinCounterBase + materialBin,
                materialBin, ~0u, 0u);
          }
          
          dxrList->ResourceBarrier(1, &toArgsBarrier);

          for (UINT materialBin = 0; materialBin < kWavefrontMaterialBinCount; ++materialBin) {
            const UINT binFlags =
                kWavefrontQueueFlagUseMaterialBinList |
                (materialBin << kWavefrontQueueFlagMaterialBinShift) |
                wavefrontTransportFlags;
            DispatchWavefrontResolveSecondary(dxrList.Get(), cameraCB,
                                             sourceCounter, binFlags, true, materialBin, false);
          }
          
          dxrList->ResourceBarrier(1, &toUavBarrier);

          DispatchWavefrontPrepareIndirectArgs(dxrList.Get(), sourceCounter, kWavefrontSecondaryResolveDispatchArgsIndex, ~0u, 0u);
          DispatchWavefrontResolveSecondary(
              dxrList.Get(), cameraCB, sourceCounter,
              kWavefrontQueueFlagMissOnly | wavefrontTransportFlags, true);

          // Secondary shadow visibility: trace shadow rays from this bounce.
          DispatchWavefrontPrepareIndirectArgs(
              dxrList.Get(), kWavefrontQueueShadow, ~0u,
              kWavefrontShadowDispatchRaysReservedSlot, 0u);
          SetWavefrontStage("secondary-shadow");
          BindRayTracingGlobalRoot();
          DispatchWavefrontShadowVisibility(dxrList.Get());
          DispatchWavefrontCounterReset(dxrList.Get(), kWavefrontQueueShadow, 0u);
        }

        SetWavefrontStage("shadow-integrate");
        DispatchWavefrontShadowIntegration(
            dxrList.Get(), cameraCB, kWavefrontResolveFlagDeferAccumulation);

        SetWavefrontStage("accumulate");
        DispatchWavefrontAccumulate(dxrList.Get(), cameraCB);

        didPathTracingWork = true;
        didWavefrontRestirSeed = true;
      }
    }

    // End DispatchRays timer
    if (s_queryHeap) {
      dxrList->EndQuery(s_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                        4); // Dispatch end
    }

    // Increment accumulation history only when actually used and not at end
    // condition.
    if (didPathTracingWork && !rrActive && !reachedEndCondition)
      s_accumulation.IncrementFrame();
    if (didPathTracingWork && !rrActive && !reachedEndCondition)
      s_transmissionAccumulation.IncrementFrame();
    else if (didPathTracingWork && rrActive && !reachedEndCondition)
      s_rrStillFrameSpp++;
  } else {
    // We are skipping dispatch to run the final denoiser on the existing
    // buffer or because we reached an end condition (noise/maxSPP).
    // Still write the queries to avoid stale data
    if (s_queryHeap) {
      dxrList->EndQuery(s_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                        3); // Dispatch start (skipped)
      dxrList->EndQuery(s_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                        4); // Dispatch end (skipped)
    }
    if (reachedEndCondition && !doDenoise && g_verboseRenderLogs) {
      // Optional: Log convergence?
      // fprintf(stderr, "DxrRenderer: Converged (Noise: %.4f < %.4f)\n",
      // s_lastNoiseLevel, g_cameraData.noiseThreshold);
    }
  }

  // Decoupled ReSTIR DI temporal/spatial reuse in a dedicated compute pass.
  // Wavefront resolve writes initial candidates, and this pass performs reuse.
  if (didWavefrontRestirSeed) {
    if (cameraCB) {
      SetWavefrontStage("restir-spatial");
      DispatchRestirSpatialPasses(dxrList.Get(), cameraCB);
    }
  }

  // Optional Streamline / DLSS evaluation
  ID3D12Resource *postColor = s_outputUAV.Get();
  ID3D12Resource *denoiserInput = s_outputUAV.Get();

  // After one-shot final denoising at end conditions, keep showing the denoised
  // HDR buffer instead of falling back to raw output on following frames.
  if (reachedEndCondition && isFinalDenoiserMode && s_hasDenoised &&
      s_oidnOutputUAV) {
    postColor = s_oidnOutputUAV.Get();
  }
  bool usedDlss = false;
  const D3D12_RESOURCE_DESC dstDesc = renderTarget->GetDesc();
  const uint32_t targetWidth = static_cast<uint32_t>(dstDesc.Width);
  const uint32_t targetHeight = dstDesc.Height;
  const uint32_t outX = (presentationX < targetWidth) ? presentationX : 0u;
  const uint32_t outY = (presentationY < targetHeight) ? presentationY : 0u;
  uint32_t outW = (presentationWidth > 0) ? presentationWidth : targetWidth;
  uint32_t outH = (presentationHeight > 0) ? presentationHeight : targetHeight;
  outW = (std::min)(outW, (targetWidth > outX) ? (targetWidth - outX) : 0u);
  outH = (std::min)(outH, (targetHeight > outY) ? (targetHeight - outY) : 0u);
  if (outW == 0 || outH == 0) {
    return ReturnFail(17, "presentation rect is empty");
  }
  const bool partialPresent =
      outX != 0 || outY != 0 || outW != targetWidth || outH != targetHeight;
  auto CopyPresentedTexture = [&](ID3D12Resource *source,
                                  D3D12_RESOURCE_STATES sourceState) {
    TransitionResource(dxrList.Get(), source, sourceState,
                       D3D12_RESOURCE_STATE_COPY_SOURCE);
    if (partialPresent) {
      TransitionResource(dxrList.Get(), renderTarget,
                         D3D12_RESOURCE_STATE_PRESENT,
                         D3D12_RESOURCE_STATE_RENDER_TARGET);
      const FLOAT clearColor[] = {0.0f, 0.0f, 0.0f, 1.0f};
      dxrList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
      TransitionResource(dxrList.Get(), renderTarget,
                         D3D12_RESOURCE_STATE_RENDER_TARGET,
                         D3D12_RESOURCE_STATE_COPY_DEST);

      D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
      dstLoc.pResource = renderTarget;
      dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      dstLoc.SubresourceIndex = 0;

      D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
      srcLoc.pResource = source;
      srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      srcLoc.SubresourceIndex = 0;

      D3D12_BOX srcBox = {0, 0, 0, outW, outH, 1};
      dxrList->CopyTextureRegion(&dstLoc, outX, outY, 0, &srcLoc, &srcBox);

      TransitionResource(dxrList.Get(), renderTarget,
                         D3D12_RESOURCE_STATE_COPY_DEST,
                         D3D12_RESOURCE_STATE_RENDER_TARGET);
    } else {
      TransitionResource(dxrList.Get(), renderTarget,
                         D3D12_RESOURCE_STATE_PRESENT,
                         D3D12_RESOURCE_STATE_COPY_DEST);
      dxrList->CopyResource(renderTarget, source);
      TransitionResource(dxrList.Get(), renderTarget,
                         D3D12_RESOURCE_STATE_COPY_DEST,
                         D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    TransitionResource(dxrList.Get(), source, D3D12_RESOURCE_STATE_COPY_SOURCE,
                       sourceState);
  };

  // If a shader debug view is active, do not run DLSS/DLSS-RR.
  // DLSS is temporal and will "process" the debug visualization itself,
  // which can look like shimmer even when the underlying buffer is stable.

  // --- Noise Statistics (Moved outside Streamline block) ---
  // Run periodically, but faster in adaptive mode so stop decisions react
  // sooner. Also run if s_lastNoiseLevel is 0 (initial calculation) regardless
  // of modulo.
  const UINT frameCounter =
      rrActive ? s_rrStillFrameSpp : s_accumulation.GetFrameCount();
  bool shouldCalcNoise = s_accumulation.IsAccumulating() && frameCounter > 0;
  if (shouldCalcNoise) {
    const UINT noiseEvalPeriod =
        (g_cameraData.useAdaptiveSampling > 0.5f) ? 8u : 20u;
    if (s_lastNoiseLevel == 0.0f || (frameCounter % noiseEvalPeriod == 0)) {
      // Start noise calculation timer
      if (s_queryHeap) {
        dxrList->EndQuery(s_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                          7); // Noise start
      }

      EnsureNoiseStatsPipeline();
      if (s_noiseStatsPSO) {
        // 1. Readback previous result FIRST (from readback buffer populated in
        // previous run). The buffer contains one float per sampled texel.
        {
          float *data = nullptr;
          if (s_noiseStatsDispatchCount > 0 &&
              SUCCEEDED(s_noiseStatsReadbackBuffer->Map(0, nullptr,
                                                        (void **)&data))) {
            const UINT stride = 4;
            UINT gridW = (s_outputWidth + stride - 1) / stride;
            UINT gridH = (s_outputHeight + stride - 1) / stride;
            UINT total = gridW * gridH;
            double sumSq = 0.0;
            UINT positiveCount = 0;
            UINT validCount = 0;
            for (UINT i = 0; i < total; ++i) {
              float v = data[i];
              if (std::isfinite(v) && v >= 0.0f) {
                ++validCount;
                if (v > 1e-12f) {
                  sumSq += v;
                  ++positiveCount;
                }
              }
            }
            if (positiveCount > 0) {
              // Average only positive entries so "not-yet-sampled" texels
              // (zeroed entries) don't bias noise toward 0.
              s_lastNoiseLevel = sqrtf((float)(sumSq / positiveCount));
              s_hasNoiseEstimate = true;
            }
            s_noiseStatsReadbackBuffer->Unmap(0, nullptr);
          }
        }

        // 2. UAV Barrier to ensure RayTrace writes are visible to Compute
        D3D12_RESOURCE_BARRIER uavBarrier = {};
        uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uavBarrier.UAV.pResource = nullptr;
        dxrList->ResourceBarrier(1, &uavBarrier);

        // 3. Dispatch Noise Stats
        // Update constants
        NoiseStatsConstants nsc = {s_outputWidth, s_outputHeight};
        void *mapPtr = nullptr;
        if (SUCCEEDED(s_noiseStatsCB->Map(0, nullptr, &mapPtr))) {
          memcpy(mapPtr, &nsc, sizeof(nsc));
          s_noiseStatsCB->Unmap(0, nullptr);
        }

        // Bind & Dispatch
        dxrList->SetPipelineState(s_noiseStatsPSO.Get());
        dxrList->SetComputeRootSignature(s_noiseStatsRootSig.Get());
        ID3D12DescriptorHeap *nsHeaps[] = {s_noiseStatsHeap.Get()};
        dxrList->SetDescriptorHeaps(1, nsHeaps);

        // Update Descriptors: u0(Accum), u1(Var), u2(Out) in heap
        UINT inc = s_device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_CPU_DESCRIPTOR_HANDLE cpuStart =
            s_noiseStatsHeap->GetCPUDescriptorHandleForHeapStart();
        D3D12_GPU_DESCRIPTOR_HANDLE gpuStart =
            s_noiseStatsHeap->GetGPUDescriptorHandleForHeapStart();

        // u0
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        s_device->CreateUnorderedAccessView(
            s_accumulation.GetAccumulationBuffer(), nullptr, &uavDesc,
            cpuStart);

        // u1
        uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
        D3D12_CPU_DESCRIPTOR_HANDLE u1 = cpuStart;
        u1.ptr += inc;
        s_device->CreateUnorderedAccessView(s_accumulation.GetVarianceBuffer(),
                                            nullptr, &uavDesc, u1);

        // Determine required output size (grid dims)
        const UINT stride = 4;
        UINT gridW = (s_outputWidth + stride - 1) / stride;
        UINT gridH = (s_outputHeight + stride - 1) / stride;
        UINT required = gridW * gridH;
        if (required > s_noiseStatsCapacity) {
          // recreate output and readback buffers with new size
          s_noiseStatsOutputBuffer.Reset();
          s_noiseStatsReadbackBuffer.Reset();
          D3D12_RESOURCE_DESC outDesc = {};
          outDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
          outDesc.Width = sizeof(float) * required;
          outDesc.Height = 1;
          outDesc.DepthOrArraySize = 1;
          outDesc.MipLevels = 1;
          outDesc.SampleDesc.Count = 1;
          outDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
          outDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

          D3D12_HEAP_PROPERTIES defaultPropsLocal = {};
          defaultPropsLocal.Type = D3D12_HEAP_TYPE_DEFAULT;

          s_device->CreateCommittedResource(
              &defaultPropsLocal, D3D12_HEAP_FLAG_NONE, &outDesc,
              D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
              IID_PPV_ARGS(&s_noiseStatsOutputBuffer));
          s_noiseStatsCapacity = required;

          D3D12_HEAP_PROPERTIES rdProps = {};
          rdProps.Type = D3D12_HEAP_TYPE_READBACK;
          outDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
          s_device->CreateCommittedResource(
              &rdProps, D3D12_HEAP_FLAG_NONE, &outDesc,
              D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
              IID_PPV_ARGS(&s_noiseStatsReadbackBuffer));
        }

        // u2 - output buffer has variable element count
        D3D12_UNORDERED_ACCESS_VIEW_DESC bufDesc = {};
        bufDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        bufDesc.Buffer.FirstElement = 0;
        bufDesc.Buffer.NumElements = max(1u, s_noiseStatsCapacity);
        bufDesc.Buffer.StructureByteStride = sizeof(float);
        bufDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
        D3D12_CPU_DESCRIPTOR_HANDLE u2 = cpuStart;
        u2.ptr += 2 * inc;
        s_device->CreateUnorderedAccessView(s_noiseStatsOutputBuffer.Get(),
                                            nullptr, &bufDesc, u2);

        dxrList->SetComputeRootConstantBufferView(
            0, s_noiseStatsCB->GetGPUVirtualAddress());
        dxrList->SetComputeRootDescriptorTable(1, gpuStart);

        // Dispatch thread groups sized 16x16; each thread samples stride pixels
        const UINT groupSizeX = 16;
        const UINT groupSizeY = 16;
        UINT dispatchX =
            (s_outputWidth + stride * groupSizeX - 1) / (stride * groupSizeX);
        UINT dispatchY =
            (s_outputHeight + stride * groupSizeY - 1) / (stride * groupSizeY);
        dxrList->Dispatch(dispatchX, dispatchY, 1);

        // 4. Copy to Readback (for NEXT frame to read)
        TransitionResource(dxrList.Get(), s_noiseStatsOutputBuffer.Get(),
                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                           D3D12_RESOURCE_STATE_COPY_SOURCE);
        dxrList->CopyResource(s_noiseStatsReadbackBuffer.Get(),
                              s_noiseStatsOutputBuffer.Get());
        TransitionResource(dxrList.Get(), s_noiseStatsOutputBuffer.Get(),
                           D3D12_RESOURCE_STATE_COPY_SOURCE,
                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        s_noiseStatsDispatchCount++;

        // End noise calculation timer
        if (s_queryHeap) {
          dxrList->EndQuery(s_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                            8); // Noise end
        }
      }
    }
  }

  // For RR, once we reach stop conditions, keep the last RR output instead of
  // re-evaluating with stale inputs and changing jitter.
  const bool skipDlssEvalAtStop = rrActive && reachedEndCondition;
  if (!debugViewActive && !skipDlssEvalAtStop && s_streamline &&
      s_streamline->IsInitialized() && s_streamline->IsDeviceSet() &&
      s_streamline->IsEnabled() &&
      s_streamline->GetMode() != StreamlineManager::Mode::Off &&
      s_dlssOutputUAV && s_depthUAV && s_mvecUAV) {

    TransitionResource(dxrList.Get(), s_outputUAV.Get(),
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    TransitionResource(dxrList.Get(), s_depthUAV.Get(),
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    TransitionResource(dxrList.Get(), s_mvecUAV.Get(),
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    if (s_albedoUAV) {
      TransitionResource(dxrList.Get(), s_albedoUAV.Get(),
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                         D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    if (s_normalRoughnessUAV) {
      TransitionResource(dxrList.Get(), s_normalRoughnessUAV.Get(),
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                         D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    if (s_specularAlbedoUAV) {
      TransitionResource(dxrList.Get(), s_specularAlbedoUAV.Get(),
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                         D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    if (s_specHitDistanceUAV) {
      TransitionResource(dxrList.Get(), s_specHitDistanceUAV.Get(),
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                         D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    if (s_specularMotionVectorsUAV) {
      TransitionResource(dxrList.Get(), s_specularMotionVectorsUAV.Get(),
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                         D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    const bool resetHistory = s_streamlineResetHistory;
    s_streamlineResetHistory = false;

    if (s_streamline->Evaluate(
            dxrList.Get(), s_outputUAV.Get(),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            s_dlssOutputUAV.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            s_outputWidth, s_outputHeight, outW, outH, s_depthUAV.Get(),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, s_mvecUAV.Get(),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            s_normalRoughnessUAV.Get(),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, s_albedoUAV.Get(),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            s_specularAlbedoUAV.Get(),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            s_specHitDistanceUAV.Get(),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            s_specularMotionVectorsUAV.Get(),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, resetHistory,
            jitterX, jitterY)) {
      usedDlss = true;
      postColor = s_dlssOutputUAV.Get();
    }

    // Restore states for potentially other passes (OIDN or next frame)
    // Streamline outputs are UAV. Inputs need to be reset if we want to use
    // them again as UAVs. Note: OIDN needs them as SRVs (Read) usually, but we
    // have a dedicated transition block below. For now, let's reset to UAV to
    // be safe and consistent, OR rely on the central transitions. The original
    // code reset them here.
    TransitionResource(dxrList.Get(), s_outputUAV.Get(),
                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    TransitionResource(dxrList.Get(), s_depthUAV.Get(),
                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    TransitionResource(dxrList.Get(), s_mvecUAV.Get(),
                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (s_albedoUAV) {
      TransitionResource(dxrList.Get(), s_albedoUAV.Get(),
                         D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    if (s_normalRoughnessUAV) {
      TransitionResource(dxrList.Get(), s_normalRoughnessUAV.Get(),
                         D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    if (s_specularAlbedoUAV) {
      TransitionResource(dxrList.Get(), s_specularAlbedoUAV.Get(),
                         D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    if (s_specHitDistanceUAV) {
      TransitionResource(dxrList.Get(), s_specHitDistanceUAV.Get(),
                         D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    if (s_specularMotionVectorsUAV) {
      TransitionResource(dxrList.Get(), s_specularMotionVectorsUAV.Get(),
                         D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
  }

  // RR stop path: if we reached end conditions and Streamline did not run this
  // frame, keep presenting the last DLSS output instead of falling back to raw
  // s_outputUAV (which appears noisy).
  if (rrActive && reachedEndCondition && !usedDlss && s_dlssOutputUAV) {
    postColor = s_dlssOutputUAV.Get();
  }

  // If DLSS wasn't used, allow the final denoiser to operate on the
  // linear HDR output as a post-process cleanup step.
  // IMPORTANT: When DLSS is active, color is output-resolution while our AOVs
  // (albedo/normal) are render-resolution. Running a final denoiser in that
  // configuration produces invalid results, so keep it gated to non-DLSS.
  bool shouldRunFinalDenoiser = doDenoise && !usedDlss;

  if (shouldRunFinalDenoiser && s_oidnOutputUAV && denoiserInput) {
    const D3D12_RESOURCE_DESC inDesc = denoiserInput->GetDesc();
    const D3D12_RESOURCE_DESC outDesc = s_oidnOutputUAV->GetDesc();
    if (inDesc.Width != outDesc.Width || inDesc.Height != outDesc.Height) {
      fprintf(stderr,
              "DxrRenderer: final denoiser skipped (input %ux%u, output "
              "%ux%u).\n",
              (unsigned)inDesc.Width, (unsigned)inDesc.Height,
              (unsigned)outDesc.Width, (unsigned)outDesc.Height);
      shouldRunFinalDenoiser = false;
    }
  }

  if (shouldRunFinalDenoiser && s_oidnOutputUAV && denoiserInput) {
    // Start denoising timer
    if (s_queryHeap) {
      dxrList->EndQuery(s_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                        5); // Denoise start
    }

    const bool useOptix =
        s_denoiserMode == DxrRenderer::DenoiserMode::OptiX;
    fprintf(stderr, "DxrRenderer: MaxSPP reached. Auto-triggering %s "
                    "denoise.\n",
            useOptix ? "OptiX" : "OIDN");
    PrepareSelectedFinalDenoiserResources();

    // Ensure input is in COMMON state for interop.
    // denoiserInput is the resolved DXR accumulation color (s_outputUAV).
    TransitionResource(dxrList.Get(), denoiserInput,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS, // Assumed state
                       D3D12_RESOURCE_STATE_COMMON);

    if (s_albedoUAV) {
      TransitionResource(dxrList.Get(), s_albedoUAV.Get(),
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                         D3D12_RESOURCE_STATE_COMMON);
    }
    if (s_normalRoughnessUAV) {
      TransitionResource(dxrList.Get(), s_normalRoughnessUAV.Get(),
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                         D3D12_RESOURCE_STATE_COMMON);
    }

    // s_oidnOutputUAV is the shared final-denoiser HDR target; transition to
    // COMMON for external interop writes.
    TransitionResource(dxrList.Get(), s_oidnOutputUAV.Get(),
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                       D3D12_RESOURCE_STATE_COMMON);

    // FLUSH & SYNC for external denoisers:
    // We must execute the command list to ensure resources are in COMMON state
    // before CUDA/OIDN tries to access them.
    if (cmdAlloc && s_commandQueue && s_fence) {
      // Flush the transitions to COMMON so OIDN can safely access the
      // resources.
      ThrowIfFailed(dxrList->Close());
      ID3D12CommandList *lists[] = {dxrList.Get()};
      s_commandQueue->ExecuteCommandLists(1, lists);

      UINT64 fenceVal = s_fenceValues[*s_frameIndexPtr];
      s_commandQueue->Signal(s_fence, fenceVal);
      s_fenceValues[*s_frameIndexPtr]++;

      if (s_fence->GetCompletedValue() < fenceVal) {
        s_fence->SetEventOnCompletion(fenceVal, s_fenceEvent);
        WaitForSingleObject(s_fenceEvent, INFINITE);
      }

      // Start a fresh command list for any fallback copy + state restores.
      ThrowIfFailed(cmdAlloc->Reset());
      ThrowIfFailed(dxrList->Reset(cmdAlloc, nullptr));
      ID3D12DescriptorHeap *dxrHeaps[] = {s_srvHeap.Get()};
      dxrList->SetDescriptorHeaps(1, dxrHeaps);

      bool ran = false;
      if (useOptix) {
        ran = s_optixDenoiser.RunDenoise(
            s_commandQueue, denoiserInput, s_albedoUAV.Get(),
            s_normalRoughnessUAV.Get(), s_oidnOutputUAV.Get());
      } else {
        // OIDN manages its own internal copy-execute-sync cycle to handle
        // tiled <-> linear layout conversion for D3D12 interop.
        ran = s_oidnDenoiser.RunDenoise(
            dxrList.Get(), s_commandQueue, denoiserInput, s_albedoUAV.Get(),
            s_normalRoughnessUAV.Get(), s_oidnOutputUAV.Get(), false);
      }

      // Restore Resource States from COMMON to what the engine expects.
      TransitionResource(dxrList.Get(), denoiserInput,
                         D3D12_RESOURCE_STATE_COMMON,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      if (s_albedoUAV) {
        TransitionResource(dxrList.Get(), s_albedoUAV.Get(),
                           D3D12_RESOURCE_STATE_COMMON,
                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      }
      if (s_normalRoughnessUAV) {
        TransitionResource(dxrList.Get(), s_normalRoughnessUAV.Get(),
                           D3D12_RESOURCE_STATE_COMMON,
                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      }
      TransitionResource(dxrList.Get(), s_oidnOutputUAV.Get(),
                         D3D12_RESOURCE_STATE_COMMON,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

      s_hasDenoised = ran;
      if (ran) {
        usedFinalDenoiser = true;
        postColor = s_oidnOutputUAV.Get();
        fprintf(stderr, "DxrRenderer: %s denoise completed successfully.\n",
                useOptix ? "OptiX" : "OIDN");
      } else {
        fprintf(stderr, "DxrRenderer: %s denoise did not run (unsupported "
                        "config or failure).\n",
                useOptix ? "OptiX" : "OIDN");
        if (useOptix) {
          // Avoid export deadlock when OptiX is selected on a non-OptiX build
          // or unsupported adapter; preserve the converged raw image in the
          // final-denoiser output slot so frozen frames keep showing it.
          TransitionResource(dxrList.Get(), denoiserInput,
                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                             D3D12_RESOURCE_STATE_COPY_SOURCE);
          TransitionResource(dxrList.Get(), s_oidnOutputUAV.Get(),
                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                             D3D12_RESOURCE_STATE_COPY_DEST);
          dxrList->CopyResource(s_oidnOutputUAV.Get(), denoiserInput);
          TransitionResource(dxrList.Get(), denoiserInput,
                             D3D12_RESOURCE_STATE_COPY_SOURCE,
                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
          TransitionResource(dxrList.Get(), s_oidnOutputUAV.Get(),
                             D3D12_RESOURCE_STATE_COPY_DEST,
                             D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
          s_hasDenoised = true;
          usedFinalDenoiser = true;
          postColor = s_oidnOutputUAV.Get();
        }
      }
    }

    // End denoising timer
    if (s_queryHeap) {
      dxrList->EndQuery(s_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                        6); // Denoise end
    }

    // postColor is in UAV state so the Tonemap block can transition it to SRV.
  }

  bool postColorInSrv = false;

  // --- Average Luminance Calculation ---
  {
    EnsureAvgLumPipeline();
    if (s_avgLumPSO && s_avgLumRootSig && s_avgLumCB && s_avgLumBuffer &&
        s_avgLumReadbackBuffer && s_avgLumHeap) {
        const bool useDisplayedPostColorForExposure =
          usedFinalDenoiser ||
          (reachedEndCondition && isFinalDenoiserMode && s_hasDenoised &&
           postColor == s_oidnOutputUAV.Get());
        ID3D12Resource *exposureSource = useDisplayedPostColorForExposure
                                             ? postColor
                                             : postColor;
      bool exposureSourceMatchesPostColor = (exposureSource == postColor);

      // 1. Read previous results
      float *data = nullptr;
      if (SUCCEEDED(s_avgLumReadbackBuffer->Map(0, nullptr, (void **)&data))) {
        const UINT stride = 8;
        const UINT gridW = (s_outputWidth + stride - 1) / stride;
        const UINT gridH = (s_outputHeight + stride - 1) / stride;
        const UINT total = gridW * gridH;
        double sumLogLum = 0.0;
        double sumLum = 0.0;
        UINT count = 0;
        // The buffer might be larger from a previous resolution
        // desc.Width contains total * sizeof(float) * 2
        const UINT maxFloats =
            (UINT)(s_avgLumReadbackBuffer->GetDesc().Width / sizeof(float));
        // We read pairs, so limit the loop to half the floats
        const UINT limit = (std::min)(total, maxFloats / 2);

        for (UINT i = 0; i < limit; ++i) {
          float logVal = data[i * 2 + 0];
          float lumVal = data[i * 2 + 1];

          if (std::isfinite(logVal) && std::isfinite(lumVal)) {
            sumLogLum += logVal;
            sumLum += lumVal;
            ++count;
          }
        }

        float avgLog = (count > 0) ? expf((float)(sumLogLum / count)) : 0.0f;
        float avgLin = (count > 0) ? (float)(sumLum / count) : 0.0f;

        // Use a weighted blend to prevent scenes with bright lights from
        // exploding. If Arithmetic mean is much higher than Geometric, it means
        // high variance (bright lights). Bias towards Arithmetic mean in that
        // case to reduce exposure but not completely define it by the sun. A
        // common trick is to use a high percentile, or blend. For now, let's
        // use a conservative approach: Use Geometric Mean as base, but blend in
        // Arithmetic Mean if the difference is huge. Also clamp the minimum
        // luminance to avoid divergence in dark scenes.
        float targetLum = avgLog;
        if (avgLin > avgLog * 10.0f) {
          // Significant variance (fireflies or sun). Pull the average up
          // towards linear to reduce exposure.
          targetLum = avgLog * 0.2f + avgLin * 0.8f;
        } else {
          targetLum = avgLog;
        }

        s_avgLuminanceCdM2 = (std::max)(targetLum, 1e-4f);

        if (s_avgLuminanceCdM2 > 1e-6f) {
          // EV100 = log2(L / 0.125) = log2(L * 8)
          s_lastEV100 = log2f(s_avgLuminanceCdM2 / 0.125f);
        } else {
          s_lastEV100 = -10.0f;
        }
        s_avgLumReadbackBuffer->Unmap(0, nullptr);
      }

      // 2. Grow buffers if needed
      // Buffer stores pairs of floats: {logLuminance, luminance}
      const UINT stride = 8;
      const UINT gridW = (s_outputWidth + stride - 1) / stride;
      const UINT gridH = (s_outputHeight + stride - 1) / stride;
      const UINT total = gridW * gridH;
      // Capacity check needs to account for 2 floats per element
      if (total > s_avgLumCapacity) {
        s_avgLumCapacity = total;
        D3D12_RESOURCE_DESC desc = s_avgLumBuffer->GetDesc();
        desc.Width = total * sizeof(float) * 2; // Store {logLum, lum}
        D3D12_HEAP_PROPERTIES defHeap = {};
        defHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
        s_device->CreateCommittedResource(&defHeap, D3D12_HEAP_FLAG_NONE, &desc,
                                          D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                          nullptr,
                                          IID_PPV_ARGS(&s_avgLumBuffer));

        desc.Flags = D3D12_RESOURCE_FLAG_NONE;
        D3D12_HEAP_PROPERTIES rdHeap = {};
        rdHeap.Type = D3D12_HEAP_TYPE_READBACK;
        s_device->CreateCommittedResource(
            &rdHeap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&s_avgLumReadbackBuffer));
      }

      // 3. Dispatch
      struct {
        uint32_t w, h;
        float padding[2];
      } nsc = {s_outputWidth, s_outputHeight, {0.0f, 0.0f}};
      void *mapPtr = nullptr;
      if (SUCCEEDED(s_avgLumCB->Map(0, nullptr, &mapPtr))) {
        memcpy(mapPtr, &nsc, sizeof(nsc));
        s_avgLumCB->Unmap(0, nullptr);
      }

      const UINT descSize = s_device->GetDescriptorHandleIncrementSize(
          D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
      D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle =
          s_avgLumHeap->GetCPUDescriptorHandleForHeapStart();

      D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
      srv.Format = exposureSource->GetDesc().Format;
      srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      srv.Texture2D.MipLevels = 1;
      s_device->CreateShaderResourceView(exposureSource, &srv, cpuHandle);

      D3D12_CPU_DESCRIPTOR_HANDLE uavCpu = cpuHandle;
      uavCpu.ptr += descSize;
      D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
      uav.Format = DXGI_FORMAT_UNKNOWN;
      uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
      uav.Buffer.NumElements = total;
      uav.Buffer.StructureByteStride = sizeof(float) * 2; // {logLum, lum}
      s_device->CreateUnorderedAccessView(s_avgLumBuffer.Get(), nullptr, &uav,
                                          uavCpu);

      // Barrier to SRV for current shader
      TransitionResource(dxrList.Get(), exposureSource,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                         D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
      if (exposureSourceMatchesPostColor) {
        postColorInSrv = true;
      }

      ID3D12DescriptorHeap *avgHeaps[] = {s_avgLumHeap.Get()};
      dxrList->SetDescriptorHeaps(1, avgHeaps);
      dxrList->SetPipelineState(s_avgLumPSO.Get());
      dxrList->SetComputeRootSignature(s_avgLumRootSig.Get());
      dxrList->SetComputeRootConstantBufferView(
          0, s_avgLumCB->GetGPUVirtualAddress());

      D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle =
          s_avgLumHeap->GetGPUDescriptorHandleForHeapStart();
      dxrList->SetComputeRootDescriptorTable(1, gpuHandle);
      gpuHandle.ptr += descSize;
      dxrList->SetComputeRootDescriptorTable(2, gpuHandle);

      dxrList->Dispatch((gridW + 15) / 16, (gridH + 15) / 16, 1);

      // Copy results back
      TransitionResource(dxrList.Get(), s_avgLumBuffer.Get(),
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                         D3D12_RESOURCE_STATE_COPY_SOURCE);
      dxrList->CopyResource(s_avgLumReadbackBuffer.Get(), s_avgLumBuffer.Get());
      TransitionResource(dxrList.Get(), s_avgLumBuffer.Get(),
                         D3D12_RESOURCE_STATE_COPY_SOURCE,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      if (!exposureSourceMatchesPostColor) {
        TransitionResource(dxrList.Get(), exposureSource,
                           D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      }
    }
  }

  // Tonemap linear HDR to swapchain format, then copy.
  EnsureTonemapPipeline();

  if (s_tonemapPSO && s_tonemapRootSig && s_tonemapHeap && s_tonemapCB &&
      s_tonemapOutputUAV) {
    // Update constants
    TonemapConstants tc{};
    tc.outWidth = outW;
    tc.outHeight = outH;

    // Auto-exposure logic
    float targetExposure = 1.0f;
    if (s_autoExposure) {
      // Don't apply auto-exposure until we have enough samples to get a
      // reasonable average luminance. Use displayed SPP as the trigger.
      const UINT currentSpp = GetDisplayedSampleCount();
      const UINT kAutoExposureMinSpp = 10; // start auto-exposure after this

      if (currentSpp >= kAutoExposureMinSpp) {
        if (s_avgLuminanceCdM2 > 1e-5f) {
          targetExposure =
              (0.18f / s_avgLuminanceCdM2) * s_exposureCompensation;
        }
        targetExposure = std::clamp(targetExposure, 1e-20f, 1e10f);

        // Simple temporal smoothing (Exponential Moving Average)
        // smoothingFactor: lower is smoother, higher is faster
        const float smoothingFactor = 0.05f;
        s_smoothedExposure +=
            (targetExposure - s_smoothedExposure) * smoothingFactor;

        // Sync to global camera data ONLY if auto-exposure is on and active
        g_cameraData.intensity = s_smoothedExposure;
        targetExposure = s_smoothedExposure;
      } else {
        // Before the auto-exposure threshold, preserve manual exposure so
        // the image doesn't jump. Initialize the smoothed value from the
        // current camera intensity so the transition at the threshold is
        // smooth.
        s_smoothedExposure = g_cameraData.intensity;
        targetExposure = g_cameraData.intensity;
      }
    } else {
      // Manual mode
      if (s_physicalCameraExposure) {
        // Exposure scale from camera EV100.
        // Calibration constant 1.2 is commonly used for scene-referred HDR.
        const float ev100 = GetPhysicalCameraEV100();
        targetExposure =
            (1.0f / (1.2f * powf(2.0f, ev100))) * s_exposureCompensation;
        targetExposure = (std::max)(targetExposure, 1e-20f);
        g_cameraData.intensity = targetExposure;
      } else {
        targetExposure = g_cameraData.intensity;
      }

      // Keep smoothed state aligned when switching between modes.
      s_smoothedExposure = targetExposure;
    }

    tc.exposure = targetExposure;
    const auto &rs = RasterRenderer::GetRenderSettings();
    tc.vignette = rs.tonemapVignette;
    tc.saturation = rs.tonemapSaturation;
    tc.contrast = rs.tonemapContrast;

    // DXR path: Secondary ray traced AO is computed in the wavefront path via
    // ComputePrimaryRayTracedAo and encoded in the primary color output.
    // Disable tonemap post-process AO entirely in this path, to avoid double
    // AO application.
    tc.aoIntensity = 0.0f;
    tc.aoRadiusMeters = 0.0f;
    tc.aoMode = static_cast<uint32_t>(s_tonemapAoMode);
    const bool useTonemapAo = false;

    void *p = nullptr;
    D3D12_RANGE readRange = {0, 0};
    if (SUCCEEDED(s_tonemapCB->Map(0, &readRange, &p))) {
      memcpy(p, &tc, sizeof(tc));
      s_tonemapCB->Unmap(0, nullptr);
    }

    if (useTonemapAo) {
      TransitionResource(dxrList.Get(), s_linearDepthUAV.Get(),
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                         D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
      TransitionResource(dxrList.Get(), s_normalRoughnessUAV.Get(),
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                         D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    // Create SRVs (HDR, depth, normal) and the UAV output.
    const UINT descInc = s_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE cpuStart =
        s_tonemapHeap->GetCPUDescriptorHandleForHeapStart();

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = postColor->GetDesc().Format;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    s_device->CreateShaderResourceView(postColor, &srv, cpuStart);

    D3D12_CPU_DESCRIPTOR_HANDLE depthCpu = cpuStart;
    depthCpu.ptr += descInc;
    if (useTonemapAo) {
      D3D12_SHADER_RESOURCE_VIEW_DESC depthSrv{};
      depthSrv.Format = DXGI_FORMAT_R32_FLOAT;
      depthSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      depthSrv.Shader4ComponentMapping =
          D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      depthSrv.Texture2D.MipLevels = 1;
      s_device->CreateShaderResourceView(s_linearDepthUAV.Get(), &depthSrv,
                                         depthCpu);
    } else {
      D3D12_SHADER_RESOURCE_VIEW_DESC nullDepthSrv{};
      nullDepthSrv.Format = DXGI_FORMAT_R32_FLOAT;
      nullDepthSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      nullDepthSrv.Shader4ComponentMapping =
          D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      nullDepthSrv.Texture2D.MipLevels = 1;
      s_device->CreateShaderResourceView(nullptr, &nullDepthSrv, depthCpu);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE normalCpu = depthCpu;
    normalCpu.ptr += descInc;
    if (useTonemapAo) {
      D3D12_SHADER_RESOURCE_VIEW_DESC normalSrv{};
      normalSrv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
      normalSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      normalSrv.Shader4ComponentMapping =
          D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      normalSrv.Texture2D.MipLevels = 1;
      s_device->CreateShaderResourceView(s_normalRoughnessUAV.Get(), &normalSrv,
                                         normalCpu);
    } else {
      D3D12_SHADER_RESOURCE_VIEW_DESC nullNormalSrv{};
      nullNormalSrv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
      nullNormalSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      nullNormalSrv.Shader4ComponentMapping =
          D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      nullNormalSrv.Texture2D.MipLevels = 1;
      s_device->CreateShaderResourceView(nullptr, &nullNormalSrv, normalCpu);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE uavCpu = normalCpu;
    uavCpu.ptr += descInc;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uav.Texture2D.MipSlice = 0;
    uav.Texture2D.PlaneSlice = 0;
    s_device->CreateUnorderedAccessView(s_tonemapOutputUAV.Get(), nullptr, &uav,
                                        uavCpu);

    // Barriers
    if (!postColorInSrv) {
      TransitionResource(dxrList.Get(), postColor,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                         D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    ID3D12DescriptorHeap *tmHeaps[] = {s_tonemapHeap.Get()};
    dxrList->SetDescriptorHeaps(1, tmHeaps);
    dxrList->SetPipelineState(s_tonemapPSO.Get());
    dxrList->SetComputeRootSignature(s_tonemapRootSig.Get());
    dxrList->SetComputeRootConstantBufferView(
        0, s_tonemapCB->GetGPUVirtualAddress());

    D3D12_GPU_DESCRIPTOR_HANDLE gpuStart =
        s_tonemapHeap->GetGPUDescriptorHandleForHeapStart();
    dxrList->SetComputeRootDescriptorTable(1, gpuStart);
    D3D12_GPU_DESCRIPTOR_HANDLE gpuUav = gpuStart;
    gpuUav.ptr += 3 * descInc;
    dxrList->SetComputeRootDescriptorTable(2, gpuUav);

    const UINT gx = (outW + 7) / 8;
    const UINT gy = (outH + 7) / 8;
    dxrList->Dispatch(gx, gy, 1);

    // Copy tonemapped output to the render target
    CopyPresentedTexture(s_tonemapOutputUAV.Get(),
               D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    TransitionResource(dxrList.Get(), postColor,
                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (useTonemapAo) {
      TransitionResource(dxrList.Get(), s_linearDepthUAV.Get(),
                         D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      TransitionResource(dxrList.Get(), s_normalRoughnessUAV.Get(),
                         D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    if (usedFinalDenoiser) {
      // If we just ran a one-shot or continuous final denoiser pass, mark the
      // frame as tonemapped so that if maxSPP is reached, we don't keep
      // re-tonemapping the same results.
      // Additionally, for one-shot denoise, this helps keep the result on
      // screen.
      s_hasTonemappedFrame = true;
      s_finalDisplayState = FinalDisplayState::DisplayingFinal;
    }
  } else {
    // Tonemap resources are mandatory for SDR swapchain output.
    // Do not copy HDR postColor directly into the UNORM backbuffer since that
    // causes severe clipping/blown highlights.
    if (postColor && postColorInSrv) {
      TransitionResource(dxrList.Get(), postColor,
                         D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    return ReturnFail(14, "Tonemap pipeline unavailable; aborted DXR frame to "
                          "avoid HDR->UNORM clipping");
  }

  // FREEZE LOGIC AFTER TONEMAPPING:
  // If we reached max SPP (or converged) and froze, copy the newly tonemapped
  // result to present it.
  if (shouldFreezeAfterTonemap) {
    ID3D12Resource *freezeSrc = nullptr;
    if (s_hasDenoised && s_tonemapOutputUAV) {
      freezeSrc = s_tonemapOutputUAV.Get();
    } else if (s_tonemapOutputUAV) {
      freezeSrc = s_tonemapOutputUAV.Get();
    } else {
      // If tonemap output isn't available (e.g., swapchain is HDR), fall back
      // to copying the main output directly if formats match.
      const DXGI_FORMAT dstFmt = renderTarget->GetDesc().Format;
      if (s_outputUAV && s_outputUAV->GetDesc().Format == dstFmt) {
        freezeSrc = s_outputUAV.Get();
      }
    }

    if (freezeSrc) {
      CopyPresentedTexture(freezeSrc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      s_hasTonemappedFrame = true;
      s_finalDisplayState = FinalDisplayState::DisplayingFinal;
    }
  }

  EndFrameProfiling(dxrList.Get());

  // Bind RTV for subsequent ImGui draws
  commandListBase->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
  return true;
}

void SubmitAsyncRestirWork() {
  if (!s_asyncRestirPending) {
    return;
  }

  if (!s_asyncRestirAvailable || !s_commandQueue || !s_asyncComputeQueue ||
      !s_asyncDirectFence || !s_asyncComputeFence || !s_asyncComputeAllocator ||
      !s_asyncComputeList || !s_asyncRestirCameraCB || !s_srvHeap) {
    s_asyncRestirPending = false;
    return;
  }

  // Ensure allocator/list are no longer in-flight from the previous async
  // submit.
  const UINT64 previousAsyncFence =
      (s_asyncComputeFenceValue > 1) ? (s_asyncComputeFenceValue - 1) : 0;
  if (previousAsyncFence > 0 &&
      s_asyncComputeFence->GetCompletedValue() < previousAsyncFence) {
    if (s_asyncComputeFenceEvent) {
      HRESULT hrWait = s_asyncComputeFence->SetEventOnCompletion(
          previousAsyncFence, s_asyncComputeFenceEvent);
      if (FAILED(hrWait)) {
        DisableAsyncRestir(
            "Failed to wait for previous async ReSTIR pass; falling back to "
            "direct-queue ReSTIR.");
        return;
      }
      if (WaitForSingleObject(s_asyncComputeFenceEvent, 5000) == WAIT_TIMEOUT) {
        DisableAsyncRestir(
            "Timeout waiting for previous async ReSTIR pass; falling back to "
            "direct-queue ReSTIR.");
        return;
      }
    } else {
      DisableAsyncRestir(
          "Async ReSTIR fence event missing; falling back to direct-queue "
          "ReSTIR.");
      return;
    }
  }

  HRESULT hr = s_asyncComputeAllocator->Reset();
  if (FAILED(hr)) {
    DisableAsyncRestir(
        "Async ReSTIR allocator reset failed; falling back to direct-queue "
        "ReSTIR.");
    return;
  }
  hr = s_asyncComputeList->Reset(s_asyncComputeAllocator.Get(), nullptr);
  if (FAILED(hr)) {
    DisableAsyncRestir(
        "Async ReSTIR command list reset failed; falling back to direct-queue "
        "ReSTIR.");
    return;
  }

  DispatchRestirSpatialPasses(s_asyncComputeList.Get(),
                              s_asyncRestirCameraCB.Get());

  hr = s_asyncComputeList->Close();
  if (FAILED(hr)) {
    DisableAsyncRestir(
        "Async ReSTIR command list close failed; falling back to direct-queue "
        "ReSTIR.");
    return;
  }

  // Run compute only after this frame's direct queue work has completed.
  const UINT64 directFenceValue = s_asyncDirectFenceValue++;
  hr = s_commandQueue->Signal(s_asyncDirectFence.Get(), directFenceValue);
  if (FAILED(hr)) {
    DisableAsyncRestir(
        "Async ReSTIR direct-queue signal failed; falling back to direct-queue "
        "ReSTIR.");
    return;
  }
  hr = s_asyncComputeQueue->Wait(s_asyncDirectFence.Get(), directFenceValue);
  if (FAILED(hr)) {
    DisableAsyncRestir(
        "Async ReSTIR compute-queue wait failed; falling back to direct-queue "
        "ReSTIR.");
    return;
  }

  ID3D12CommandList *lists[] = {s_asyncComputeList.Get()};
  s_asyncComputeQueue->ExecuteCommandLists(1, lists);

  const UINT64 computeFenceValue = s_asyncComputeFenceValue++;
  hr = s_asyncComputeQueue->Signal(s_asyncComputeFence.Get(), computeFenceValue);
  if (FAILED(hr)) {
    DisableAsyncRestir(
        "Async ReSTIR compute-queue signal failed; falling back to direct-queue "
        "ReSTIR.");
    return;
  }
  s_asyncComputePendingFenceWait = computeFenceValue;

  s_asyncRestirPending = false;
}

bool CopyTonemappedFrameToResource(ID3D12GraphicsCommandList *commandList,
                                   ID3D12Resource *target,
                                   D3D12_RESOURCE_STATES *targetState) {
  if (!commandList || !target || !targetState || !s_tonemapOutputUAV) {
    return false;
  }

  const D3D12_RESOURCE_DESC srcDesc = s_tonemapOutputUAV->GetDesc();
  const D3D12_RESOURCE_DESC dstDesc = target->GetDesc();
  if (srcDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
      dstDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
      srcDesc.Format != dstDesc.Format || srcDesc.Width != dstDesc.Width ||
      srcDesc.Height != dstDesc.Height) {
    return false;
  }

  const D3D12_RESOURCE_STATES restoreTargetState = *targetState;
  TransitionResource(commandList, s_tonemapOutputUAV.Get(),
                     D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                     D3D12_RESOURCE_STATE_COPY_SOURCE);
  TransitionResource(commandList, target, *targetState,
                     D3D12_RESOURCE_STATE_COPY_DEST);
  *targetState = D3D12_RESOURCE_STATE_COPY_DEST;

  commandList->CopyResource(target, s_tonemapOutputUAV.Get());

  TransitionResource(commandList, target, *targetState, restoreTargetState);
  *targetState = restoreTargetState;
  TransitionResource(commandList, s_tonemapOutputUAV.Get(),
                     D3D12_RESOURCE_STATE_COPY_SOURCE,
                     D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  return true;
}

bool ExportTonemappedFrameToPng(const std::wstring &filePath) {
  if (filePath.empty() || !s_device || !s_commandQueue || !s_fence ||
      !s_fenceValues || !s_frameIndexPtr || !s_fenceEvent ||
      !s_tonemapOutputUAV) {
    fprintf(stderr,
            "DxrRenderer: ExportTonemappedFrameToPng precondition failed.\n");
    return false;
  }

  ID3D12Resource *source = s_tonemapOutputUAV.Get();
  const D3D12_RESOURCE_DESC srcDesc = source->GetDesc();
  if (srcDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
      srcDesc.Format != DXGI_FORMAT_R10G10B10A2_UNORM) {
    fprintf(stderr,
            "DxrRenderer: ExportTonemappedFrameToPng unsupported source "
            "resource format.\n");
    return false;
  }

  const UINT width = static_cast<UINT>(srcDesc.Width);
  const UINT height = srcDesc.Height;
  if (width == 0 || height == 0) {
    fprintf(stderr, "DxrRenderer: ExportTonemappedFrameToPng invalid size.\n");
    return false;
  }
  fprintf(stderr, "DxrRenderer: ExportTonemappedFrameToPng writing %ux%u\n",
          width, height);

  ComPtr<ID3D12CommandAllocator> cmdAlloc;
  if (FAILED(s_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              IID_PPV_ARGS(&cmdAlloc)))) {
    fprintf(stderr, "DxrRenderer: ExportTonemappedFrameToPng failed to create "
                    "command allocator.\n");
    return false;
  }

  ComPtr<ID3D12GraphicsCommandList> cmdList;
  if (FAILED(s_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         cmdAlloc.Get(), nullptr,
                                         IID_PPV_ARGS(&cmdList)))) {
    fprintf(stderr, "DxrRenderer: ExportTonemappedFrameToPng failed to create "
                    "command list.\n");
    return false;
  }

  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
  UINT numRows = 0;
  UINT64 rowSizeInBytes = 0;
  UINT64 totalBytes = 0;
  s_device->GetCopyableFootprints(&srcDesc, 0, 1, 0, &footprint, &numRows,
                                  &rowSizeInBytes, &totalBytes);
  if (totalBytes == 0 || numRows == 0) {
    fprintf(stderr, "DxrRenderer: ExportTonemappedFrameToPng invalid "
                    "copyable footprint.\n");
    return false;
  }

  D3D12_HEAP_PROPERTIES readbackHeap = {};
  readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;

  D3D12_RESOURCE_DESC readbackDesc = {};
  readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  readbackDesc.Width = totalBytes;
  readbackDesc.Height = 1;
  readbackDesc.DepthOrArraySize = 1;
  readbackDesc.MipLevels = 1;
  readbackDesc.SampleDesc.Count = 1;
  readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

  ComPtr<ID3D12Resource> readback;
  if (FAILED(s_device->CreateCommittedResource(
          &readbackHeap, D3D12_HEAP_FLAG_NONE, &readbackDesc,
          D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)))) {
    fprintf(stderr, "DxrRenderer: ExportTonemappedFrameToPng failed to create "
                    "readback buffer.\n");
    return false;
  }

  TransitionResource(cmdList.Get(), source,
                     D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                     D3D12_RESOURCE_STATE_COPY_SOURCE);

  D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
  srcLoc.pResource = source;
  srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  srcLoc.SubresourceIndex = 0;

  D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
  dstLoc.pResource = readback.Get();
  dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  dstLoc.PlacedFootprint = footprint;

  cmdList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

  TransitionResource(cmdList.Get(), source, D3D12_RESOURCE_STATE_COPY_SOURCE,
                     D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

  if (FAILED(cmdList->Close())) {
    fprintf(stderr, "DxrRenderer: ExportTonemappedFrameToPng failed to close "
                    "command list.\n");
    return false;
  }

  ID3D12CommandList *lists[] = {cmdList.Get()};
  s_commandQueue->ExecuteCommandLists(1, lists);

  const UINT fi = *s_frameIndexPtr;
  const UINT64 fenceValue = s_fenceValues[fi] + 1000;
  if (FAILED(s_commandQueue->Signal(s_fence, fenceValue))) {
    fprintf(stderr, "DxrRenderer: ExportTonemappedFrameToPng failed to signal "
                    "fence.\n");
    return false;
  }
  s_fenceValues[fi] = fenceValue + 1;

  if (s_fence->GetCompletedValue() < fenceValue) {
    if (FAILED(s_fence->SetEventOnCompletion(fenceValue, s_fenceEvent))) {
      fprintf(stderr, "DxrRenderer: ExportTonemappedFrameToPng "
                      "SetEventOnCompletion failed.\n");
      return false;
    }
    if (WaitForSingleObject(s_fenceEvent, 5000) == WAIT_TIMEOUT) {
      fprintf(stderr, "DxrRenderer: ExportTonemappedFrameToPng wait timed "
                      "out.\n");
      return false;
    }
  }

  uint8_t *mapped = nullptr;
  if (FAILED(readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped))) ||
      !mapped) {
    fprintf(
        stderr,
        "DxrRenderer: ExportTonemappedFrameToPng failed to map readback.\n");
    return false;
  }

  std::vector<uint8_t> rgba(width * height * 4);
  const UINT srcPitch = footprint.Footprint.RowPitch;
  for (UINT y = 0; y < height; ++y) {
    const uint8_t *srcRow = mapped + footprint.Offset + y * srcPitch;
    uint8_t *dstRow = rgba.data() + y * (width * 4);
    const auto *srcPixels = reinterpret_cast<const uint32_t *>(srcRow);
    for (UINT x = 0; x < width; ++x) {
      const uint32_t packed = srcPixels[x];
      const uint32_t r10 = packed & 0x3FFu;
      const uint32_t g10 = (packed >> 10) & 0x3FFu;
      const uint32_t b10 = (packed >> 20) & 0x3FFu;
      const uint32_t a2 = (packed >> 30) & 0x3u;
      dstRow[x * 4 + 0] = static_cast<uint8_t>((r10 * 255u + 511u) / 1023u);
      dstRow[x * 4 + 1] = static_cast<uint8_t>((g10 * 255u + 511u) / 1023u);
      dstRow[x * 4 + 2] = static_cast<uint8_t>((b10 * 255u + 511u) / 1023u);
      dstRow[x * 4 + 3] = static_cast<uint8_t>((a2 * 255u + 1u) / 3u);
    }
  }

  readback->Unmap(0, nullptr);

  return SaveRgba8ToPngWic(filePath, width, height, rgba.data(), width * 4);
}

static float HalfToFloat(uint16_t h) {
  const uint32_t sign = (uint32_t)(h & 0x8000u) << 16;
  uint32_t exp = (h >> 10) & 0x1Fu;
  uint32_t mant = h & 0x03FFu;

  if (exp == 0) {
    if (mant == 0) {
      const uint32_t bits = sign;
      float out = 0.0f;
      memcpy(&out, &bits, sizeof(out));
      return out;
    }

    exp = 1;
    while ((mant & 0x0400u) == 0) {
      mant <<= 1;
      exp--;
    }
    mant &= 0x03FFu;
    const uint32_t bits = sign | ((exp + 112u) << 23) | (mant << 13);
    float out = 0.0f;
    memcpy(&out, &bits, sizeof(out));
    return out;
  }

  if (exp == 31) {
    const uint32_t bits = sign | 0x7F800000u | (mant << 13);
    float out = 0.0f;
    memcpy(&out, &bits, sizeof(out));
    return out;
  }

  const uint32_t bits = sign | ((exp + 112u) << 23) | (mant << 13);
  float out = 0.0f;
  memcpy(&out, &bits, sizeof(out));
  return out;
}

static uint8_t Float01ToByte(float v) {
  v = (std::clamp)(v, 0.0f, 1.0f);
  return (uint8_t)(v * 255.0f + 0.5f);
}

static uint8_t LinearToSrgbByte(float v) {
  v = (std::clamp)(v, 0.0f, 1.0f);
  v = powf(v, 1.0f / 2.2f);
  return Float01ToByte(v);
}

static std::wstring FloatTag(float v) {
  if (!std::isfinite(v))
    return L"inf";
  std::wostringstream oss;
  oss << std::fixed << std::setprecision(3) << v;
  std::wstring s = oss.str();
  for (wchar_t &ch : s) {
    if (ch == L'-')
      ch = L'm';
    else if (ch == L'.')
      ch = L'p';
  }
  return s;
}

static bool ReadbackTexture2D(ID3D12Resource *source,
                              D3D12_RESOURCE_STATES assumedState,
                              std::vector<uint8_t> &raw,
                              D3D12_PLACED_SUBRESOURCE_FOOTPRINT &footprint,
                              UINT &width, UINT &height) {
  if (!source || !s_device || !s_commandQueue || !s_fence || !s_fenceValues ||
      !s_frameIndexPtr || !s_fenceEvent) {
    return false;
  }

  const D3D12_RESOURCE_DESC srcDesc = source->GetDesc();
  if (srcDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
      srcDesc.DepthOrArraySize != 1 || srcDesc.MipLevels != 1) {
    return false;
  }

  width = (UINT)srcDesc.Width;
  height = srcDesc.Height;
  if (width == 0 || height == 0) {
    return false;
  }

  UINT numRows = 0;
  UINT64 rowSizeInBytes = 0;
  UINT64 totalBytes = 0;
  s_device->GetCopyableFootprints(&srcDesc, 0, 1, 0, &footprint, &numRows,
                                  &rowSizeInBytes, &totalBytes);
  if (numRows == 0 || totalBytes == 0) {
    return false;
  }

  D3D12_HEAP_PROPERTIES readbackHeap = {};
  readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;

  D3D12_RESOURCE_DESC readbackDesc = {};
  readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  readbackDesc.Width = totalBytes;
  readbackDesc.Height = 1;
  readbackDesc.DepthOrArraySize = 1;
  readbackDesc.MipLevels = 1;
  readbackDesc.SampleDesc.Count = 1;
  readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

  ComPtr<ID3D12Resource> readback;
  if (FAILED(s_device->CreateCommittedResource(
          &readbackHeap, D3D12_HEAP_FLAG_NONE, &readbackDesc,
          D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)))) {
    return false;
  }

  ComPtr<ID3D12CommandAllocator> cmdAlloc;
  if (FAILED(s_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              IID_PPV_ARGS(&cmdAlloc)))) {
    return false;
  }

  ComPtr<ID3D12GraphicsCommandList> cmdList;
  if (FAILED(s_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         cmdAlloc.Get(), nullptr,
                                         IID_PPV_ARGS(&cmdList)))) {
    return false;
  }

  TransitionResource(cmdList.Get(), source, assumedState,
                     D3D12_RESOURCE_STATE_COPY_SOURCE);

  D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
  srcLoc.pResource = source;
  srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  srcLoc.SubresourceIndex = 0;

  D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
  dstLoc.pResource = readback.Get();
  dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  dstLoc.PlacedFootprint = footprint;

  cmdList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

  TransitionResource(cmdList.Get(), source, D3D12_RESOURCE_STATE_COPY_SOURCE,
                     assumedState);

  if (FAILED(cmdList->Close())) {
    return false;
  }

  ID3D12CommandList *lists[] = {cmdList.Get()};
  s_commandQueue->ExecuteCommandLists(1, lists);

  const UINT fi = *s_frameIndexPtr;
  const UINT64 fenceValue = s_fenceValues[fi] + 1000;
  if (FAILED(s_commandQueue->Signal(s_fence, fenceValue))) {
    return false;
  }
  s_fenceValues[fi] = fenceValue + 1;

  if (s_fence->GetCompletedValue() < fenceValue) {
    if (FAILED(s_fence->SetEventOnCompletion(fenceValue, s_fenceEvent))) {
      return false;
    }
    if (WaitForSingleObject(s_fenceEvent, 5000) == WAIT_TIMEOUT) {
      return false;
    }
  }

  uint8_t *mapped = nullptr;
  if (FAILED(readback->Map(0, nullptr, (void **)&mapped)) || !mapped) {
    return false;
  }

  raw.resize((size_t)totalBytes);
  memcpy(raw.data(), mapped, (size_t)totalBytes);
  readback->Unmap(0, nullptr);
  return true;
}

static bool SaveGrayPngFromFloats(const std::wstring &filePath, UINT width,
                                  UINT height, const std::vector<float> &values,
                                  float minValue, float maxValue,
                                  bool useLogScale) {
  if (values.size() != (size_t)width * (size_t)height)
    return false;

  std::vector<uint8_t> rgba((size_t)width * (size_t)height * 4u, 255u);
  const float safeMin = std::isfinite(minValue) ? minValue : 0.0f;
  const float safeMax = std::isfinite(maxValue) ? maxValue : safeMin;
  const float denom = (safeMax > safeMin) ? (safeMax - safeMin) : 1.0f;
  const float logMin =
      useLogScale ? logf(((std::max)(safeMin, 0.0f)) + 1.0f) : 0.0f;
  const float logMax =
      useLogScale ? logf(((std::max)(safeMax, 0.0f)) + 1.0f) : 1.0f;
  const float logDenom = (logMax > logMin) ? (logMax - logMin) : 1.0f;

  for (size_t i = 0; i < values.size(); ++i) {
    float v = values[i];
    float n = 0.0f;
    if (std::isfinite(v)) {
      if (useLogScale) {
        const float lv = logf(((std::max)(v, 0.0f)) + 1.0f);
        n = (lv - logMin) / logDenom;
      } else {
        n = (v - safeMin) / denom;
      }
    }
    const uint8_t c = Float01ToByte(n);
    rgba[i * 4 + 0] = c;
    rgba[i * 4 + 1] = c;
    rgba[i * 4 + 2] = c;
  }

  return SaveRgba8ToPngWic(filePath, width, height, rgba.data(), width * 4u);
}

static bool ExportHalfScalarTexture(const std::wstring &directoryPath,
                                    const std::wstring &baseName,
                                    ID3D12Resource *resource,
                                    bool useLogScale) {
  if (!resource)
    return false;

  std::vector<uint8_t> raw;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
  UINT width = 0, height = 0;
  if (!ReadbackTexture2D(resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, raw,
                         footprint, width, height)) {
    return false;
  }

  std::vector<float> values((size_t)width * (size_t)height, 0.0f);
  float minV = (std::numeric_limits<float>::max)();
  float maxV = 0.0f;
  const DXGI_FORMAT format = resource->GetDesc().Format;
  const bool isFloat32 = (format == DXGI_FORMAT_R32_FLOAT);
  for (UINT y = 0; y < height; ++y) {
    const uint8_t *srcRow = raw.data() + footprint.Offset +
                            (size_t)y * footprint.Footprint.RowPitch;
    for (UINT x = 0; x < width; ++x) {
      const float v = isFloat32 ? reinterpret_cast<const float *>(srcRow)[x]
                                : HalfToFloat(reinterpret_cast<const uint16_t *>(srcRow)[x]);
      values[(size_t)y * width + x] = v;
      if (std::isfinite(v)) {
        minV = (std::min)(minV, v);
        maxV = (std::max)(maxV, v);
      }
    }
  }

  if (minV == (std::numeric_limits<float>::max)())
    minV = 0.0f;
  fprintf(stderr, "DxrRenderer: %ls range [%.6f, %.6f]\n", baseName.c_str(),
          minV, maxV);

  const std::wstring path =
      directoryPath + L"/" + baseName + L"_" + FloatTag(minV) + L"_" +
      FloatTag(maxV) + L".png";
  return SaveGrayPngFromFloats(path, width, height, values, minV, maxV,
                               useLogScale);
}

static bool ExportHalf2Texture(const std::wstring &directoryPath,
                               const std::wstring &baseName,
                               ID3D12Resource *resource,
                               bool useLogScale) {
  if (!resource)
    return false;

  std::vector<uint8_t> raw;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
  UINT width = 0, height = 0;
  if (!ReadbackTexture2D(resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, raw,
                         footprint, width, height)) {
    return false;
  }

  std::vector<float> xVals((size_t)width * (size_t)height, 0.0f);
  std::vector<float> yVals((size_t)width * (size_t)height, 0.0f);
  float minX = (std::numeric_limits<float>::max)();
  float maxX = 0.0f;
  float minY = (std::numeric_limits<float>::max)();
  float maxY = 0.0f;
  const DXGI_FORMAT format = resource->GetDesc().Format;
  const bool isFloat32 = (format == DXGI_FORMAT_R32G32_FLOAT);
  for (UINT y = 0; y < height; ++y) {
    const uint8_t *srcRow = raw.data() + footprint.Offset +
                            (size_t)y * footprint.Footprint.RowPitch;
    for (UINT x = 0; x < width; ++x) {
      const size_t idx = (size_t)y * width + x;
      float vx, vy;
      if (isFloat32) {
        const float *src = reinterpret_cast<const float *>(srcRow) + x * 2;
        vx = src[0];
        vy = src[1];
      } else {
        const uint16_t *src = reinterpret_cast<const uint16_t *>(srcRow) + x * 2;
        vx = HalfToFloat(src[0]);
        vy = HalfToFloat(src[1]);
      }
      xVals[idx] = vx;
      yVals[idx] = vy;
      if (std::isfinite(vx)) {
        minX = (std::min)(minX, vx);
        maxX = (std::max)(maxX, vx);
      }
      if (std::isfinite(vy)) {
        minY = (std::min)(minY, vy);
        maxY = (std::max)(maxY, vy);
      }
    }
  }

  if (minX == (std::numeric_limits<float>::max)())
    minX = 0.0f;
  if (minY == (std::numeric_limits<float>::max)())
    minY = 0.0f;
  fprintf(stderr,
          "DxrRenderer: %ls x range [%.6f, %.6f], y range [%.6f, %.6f]\n",
          baseName.c_str(), minX, maxX, minY, maxY);

  const std::wstring xPath =
      directoryPath + L"/" + baseName + L"_x_" + FloatTag(minX) + L"_" +
      FloatTag(maxX) + L".png";
  const std::wstring yPath =
      directoryPath + L"/" + baseName + L"_y_" + FloatTag(minY) + L"_" +
      FloatTag(maxY) + L".png";
  const bool xOk =
      SaveGrayPngFromFloats(xPath, width, height, xVals, minX, maxX, useLogScale);
  const bool yOk =
      SaveGrayPngFromFloats(yPath, width, height, yVals, minY, maxY, useLogScale);
  return xOk && yOk;
}


// Profiling functions
static std::chrono::high_resolution_clock::time_point s_cpuFrameStartTime;
static float s_cpuWorkTimeMs = 0.0f;

void BeginFrameProfiling(ID3D12GraphicsCommandList *commandList) {
  s_cpuFrameStartTime = std::chrono::high_resolution_clock::now();
  if (s_queryHeap) {
    // Record all timestamps at the start. DXR mode will overwrite specific
    // ones. This prevents stale/undefined data from appearing in the UI for
    // raster mode.
    for (int i = 0; i < 10; ++i) {
      commandList->EndQuery(s_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, i);
    }
  }
}

void EndFrameProfiling(ID3D12GraphicsCommandList *commandList) {
  auto cpuEnd = std::chrono::high_resolution_clock::now();
  s_cpuWorkTimeMs =
      std::chrono::duration<float, std::milli>(cpuEnd - s_cpuFrameStartTime)
          .count();
  static UINT s_wavefrontStatsReadbackFrame = 0;
  const bool readbackShaderCounters = ShaderCountersEnabled();
  const bool readbackWavefrontStats =
      g_verboseRenderLogs || ((s_wavefrontStatsReadbackFrame++ & 7u) == 0u);

  if (s_queryHeap) {
    commandList->EndQuery(s_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                          9); // Frame end
    // Copy shader counters (GPU -> readback) so CPU can inspect them next map
    if (readbackShaderCounters && s_shaderCountersBuffer &&
        s_shaderCountersReadbackBuffer) {
      TransitionResource(commandList, s_shaderCountersBuffer.Get(),
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                         D3D12_RESOURCE_STATE_COPY_SOURCE);
      commandList->CopyResource(s_shaderCountersReadbackBuffer.Get(),
                                s_shaderCountersBuffer.Get());
      TransitionResource(commandList, s_shaderCountersBuffer.Get(),
                         D3D12_RESOURCE_STATE_COPY_SOURCE,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    if (readbackWavefrontStats && s_wavefrontStatsBuffer &&
        s_wavefrontStatsReadbackBuffer) {
      TransitionResource(commandList, s_wavefrontStatsBuffer.Get(),
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                         D3D12_RESOURCE_STATE_COPY_SOURCE);
      commandList->CopyResource(s_wavefrontStatsReadbackBuffer.Get(),
                                s_wavefrontStatsBuffer.Get());
      TransitionResource(commandList, s_wavefrontStatsBuffer.Get(),
                         D3D12_RESOURCE_STATE_COPY_SOURCE,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    commandList->ResolveQueryData(s_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                                  0, 10, s_queryReadbackBuffer.Get(), 0);
  }

  // Calculate CPU frame time and FPS
  auto now = std::chrono::high_resolution_clock::now();
  if (s_lastFrameTime.time_since_epoch().count() != 0) {
    auto frameDuration = now - s_lastFrameTime;
    s_frameTimeMs =
        std::chrono::duration<float, std::milli>(frameDuration).count();
    s_fps = 1000.0f / s_frameTimeMs;

    // Calculate SPP/s
    UINT currentFrameCount = GetDisplayedSampleCount();
    if (currentFrameCount > s_lastFrameCount) {
      float sppIncrease = (float)(currentFrameCount - s_lastFrameCount);
      s_sppPerSec = sppIncrease / (s_frameTimeMs / 1000.0f);
    } else {
      s_sppPerSec = 0.0f;
    }
    s_lastFrameCount = currentFrameCount;
  }
  s_lastFrameTime = now;

  // Read GPU timestamps and calculate times (will be available next frame)
  if (s_queryReadbackBuffer) {
    UINT64 *data = nullptr;
    if (SUCCEEDED(s_queryReadbackBuffer->Map(0, nullptr, (void **)&data))) {
      UINT64 gpuFrequency = 0;
      s_commandQueue->GetTimestampFrequency(&gpuFrequency);
      double timestampToMs = 1000.0 / gpuFrequency;

      // ReSTIR time: restir_end - restir_start
      if (data[2] > data[1]) {
        s_gpuTimes[0] = (float)((data[2] - data[1]) * timestampToMs);
      }

      // DispatchRays time: dispatch_end - dispatch_start
      if (data[4] > data[3]) {
        s_gpuTimes[1] = (float)((data[4] - data[3]) * timestampToMs);
      }

      // Denoising time: denoise_end - denoise_start
      if (data[6] > data[5]) {
        s_gpuTimes[2] = (float)((data[6] - data[5]) * timestampToMs);
      }

      // Noise calculation time: noise_end - noise_start
      if (data[8] > data[7]) {
        s_gpuTimes[3] = (float)((data[8] - data[7]) * timestampToMs);
      }

      // Compute full GPU frame time using frame start (0) and frame end (9)
      if (data[9] > data[0]) {
        s_gpuFrameTimeMs = (float)((data[9] - data[0]) * timestampToMs);
      }

      s_queryReadbackBuffer->Unmap(0, nullptr);
    }
  }

  // Read shader counters readback (from GPU) and log a short summary
  if (readbackShaderCounters && s_shaderCountersReadbackBuffer) {
    UINT *c = nullptr;
    if (SUCCEEDED(
            s_shaderCountersReadbackBuffer->Map(0, nullptr, (void **)&c))) {
      for (UINT i = 0; i < 16; ++i)
        s_lastShaderCounters[i] = c[i];
      s_shaderCountersReadbackBuffer->Unmap(0, nullptr);

      if (g_verboseRenderLogs) {
        fprintf(stderr,
                "ShaderCounters: TR=%u SH=%u SPEC=%u TEX=%u VTX=%u RESR=%u "
                "RESW=%u\n",
                s_lastShaderCounters[0], s_lastShaderCounters[1],
                s_lastShaderCounters[2], s_lastShaderCounters[5],
                s_lastShaderCounters[4], s_lastShaderCounters[6],
                s_lastShaderCounters[7]);
      }
    }
  }

  if (readbackWavefrontStats && s_wavefrontStatsReadbackBuffer) {
    UINT *stats = nullptr;
    if (SUCCEEDED(
            s_wavefrontStatsReadbackBuffer->Map(0, nullptr, (void **)&stats))) {
      memcpy(s_lastWavefrontStats, stats, sizeof(s_lastWavefrontStats));
      s_lastWavefrontPrimaryRecordCount = s_lastWavefrontStats[2];
      s_lastWavefrontPrimaryHitCount = s_lastWavefrontStats[6];
      s_lastWavefrontPrimaryMissCount = s_lastWavefrontStats[7];
      s_lastWavefrontResolveRecordCount = s_lastWavefrontStats[8];
      s_lastWavefrontResolveSurfaceCount = s_lastWavefrontStats[9];
      s_lastWavefrontResolveDiffuseCount = s_lastWavefrontStats[10];
      s_lastWavefrontResolveSpecularCount = s_lastWavefrontStats[11];
      s_lastWavefrontResolveTransmissionCount = s_lastWavefrontStats[12];
      s_lastWavefrontResolveSkyCount = s_lastWavefrontStats[13];
      s_lastWavefrontContinuationOverflowCount = s_lastWavefrontStats[21];
      s_lastWavefrontShadowOverflowCount = s_lastWavefrontStats[22];
      s_lastWavefrontMaterialBinOverflowCount = s_lastWavefrontStats[49];
      s_lastWavefrontSecondaryPathCount = s_lastWavefrontStats[16];
      s_lastWavefrontSecondaryDiffuseCount = s_lastWavefrontStats[17];
      s_lastWavefrontSecondarySpecularCount = s_lastWavefrontStats[18];
      s_lastWavefrontSecondaryTransmissionCount = s_lastWavefrontStats[19];
      s_lastWavefrontShadowTaskCount = s_lastWavefrontStats[20];
      s_lastWavefrontSecondaryVisibilityRecordCount = s_lastWavefrontStats[23];
      s_lastWavefrontSecondaryVisibilityHitCount = s_lastWavefrontStats[24];
      s_lastWavefrontSecondaryVisibilityMissCount = s_lastWavefrontStats[25];
      s_lastWavefrontSecondaryResolveRecordCount = s_lastWavefrontStats[26];
      s_lastWavefrontSecondaryResolveSurfaceCount = s_lastWavefrontStats[27];
      s_lastWavefrontSecondaryResolveSkyCount = s_lastWavefrontStats[28];
      s_lastWavefrontShadowVisibilityTaskCount = s_lastWavefrontStats[29];
      s_lastWavefrontShadowVisibleCount = s_lastWavefrontStats[30];
      s_lastWavefrontShadowOccludedCount = s_lastWavefrontStats[31];
      s_wavefrontStatsReadbackBuffer->Unmap(0, nullptr);
    }
  }
}

float GetFrameTimeMs() { return s_frameTimeMs; }
float GetCPUWorkTimeMs() { return s_cpuWorkTimeMs; }
float GetFPS() { return s_fps; }
float GetSPPPerSec() { return s_sppPerSec; }
void GetGPUTimes(float &restirTime, float &dispatchTime, float &denoiseTime,
                 float &noiseTime) {
  restirTime = s_gpuTimes[0];
  dispatchTime = s_gpuTimes[1];
  denoiseTime = s_gpuTimes[2];
  noiseTime = s_gpuTimes[3];
}

float GetGPUFrameTimeMs() { return s_gpuFrameTimeMs; }

// Expose shader counters (filled from last GPU readback)
void GetShaderCounters(UINT *outCounters, UINT maxCount) {
  if (!outCounters || maxCount == 0)
    return;
  UINT toCopy = (maxCount < _countof(s_lastShaderCounters))
                    ? maxCount
                    : _countof(s_lastShaderCounters);
  for (UINT i = 0; i < toCopy; ++i)
    outCounters[i] = s_lastShaderCounters[i];
  for (UINT i = toCopy; i < maxCount; ++i)
    outCounters[i] = 0u;
}

} // namespace DxrRenderer
