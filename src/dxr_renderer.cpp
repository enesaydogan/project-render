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
#include "nrd_denoiser.h"
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
// Denoiser mode & wrapper
static DxrRenderer::DenoiserMode s_denoiserMode =
    DxrRenderer::DenoiserMode::Off;
static OidnDenoiser s_oidnDenoiser;
static OidnDenoiser::Quality s_oidnQuality = OidnDenoiser::Quality::Balanced;
static float s_rrJitterScale = 0.5f;

// default off so that users see the raw sky intensity without
// automatic normalization.  The UI checkbox will toggle this at runtime.
static bool s_autoExposure = false;
static float s_exposureCompensation = 1.0f;
static float s_smoothedExposure = 0.02f; // persistent smoothed exposure
static bool s_physicalCameraExposure = true;
static float s_cameraIso = 100.0f;
static float s_cameraShutterSeconds = 1.0f / 30.0f;
static float s_cameraApertureFNumber = 2.8f;

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
// changes
extern bool g_dxrDebugUV;
extern bool g_dxrHitDebug;
extern bool g_dxrDumpD3D12Messages;
extern bool g_verboseRenderLogs;
static ID3D12Fence *s_fence = nullptr;
static UINT64 *s_fenceValues = nullptr;
static UINT *s_frameIndexPtr = nullptr;
static HANDLE s_fenceEvent = nullptr;

bool g_rayTracingSupported = false; // defined here

// DXR-specific state kept internal to this module
static ComPtr<ID3D12Device5> s_dxrDevice;
static DxrAccumulation s_accumulation;

// Descriptor heaps for DXR
static ComPtr<ID3D12DescriptorHeap>
    s_srvHeap; // Holds [Textures(2048), VBs(1024), IBs(1024), OutputUAV(1)]
static D3D12_GPU_DESCRIPTOR_HANDLE s_texTableGpu;
static D3D12_GPU_DESCRIPTOR_HANDLE s_vbTableGpu;
static D3D12_GPU_DESCRIPTOR_HANDLE s_ibTableGpu;
static D3D12_GPU_DESCRIPTOR_HANDLE s_outputUAVGpu;
static D3D12_GPU_DESCRIPTOR_HANDLE s_accumUAVGpu;
static D3D12_GPU_DESCRIPTOR_HANDLE s_varianceUAVGpu;
static D3D12_GPU_DESCRIPTOR_HANDLE s_reservoirGpuHandle[2];
static D3D12_GPU_DESCRIPTOR_HANDLE s_gi_reservoirGpuHandle[6];
static D3D12_GPU_DESCRIPTOR_HANDLE s_iblGpuHandle;
static D3D12_GPU_DESCRIPTOR_HANDLE s_shaderCountersGpuHandle;
static Microsoft::WRL::ComPtr<ID3D12Resource> s_shaderCountersBuffer;
static Microsoft::WRL::ComPtr<ID3D12Resource> s_shaderCountersReadbackBuffer;
static UINT s_lastShaderCounters[16] = {0};

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
static const UINT DXR_HEAP_UAV_COUNT = 33; // u0..u32
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
// u15 intentionally unused.
static const UINT DXR_HEAP_SPEC_ALBEDO_OFFSET = DXR_HEAP_UAV_OFFSET + 16;
static const UINT DXR_HEAP_SPEC_HITDIST_OFFSET = DXR_HEAP_UAV_OFFSET + 17;
static const UINT DXR_HEAP_SPEC_MVEC_OFFSET = DXR_HEAP_UAV_OFFSET + 18;
// u19..u20 currently unused.
static const UINT DXR_HEAP_OIDN_OUT_UAV_OFFSET = DXR_HEAP_UAV_OFFSET + 21;
static const UINT DXR_HEAP_VARIANCE_UAV_OFFSET = DXR_HEAP_UAV_OFFSET + 22;
// Extra debug UAV: shader counters (readback) at u24
static const UINT DXR_HEAP_SHADER_COUNTERS_OFFSET = DXR_HEAP_UAV_OFFSET + 24;

// NRD dedicated UAVs
static const UINT DXR_HEAP_NRD_DIFFUSE_RADIANCE_HITDIST_OFFSET = DXR_HEAP_UAV_OFFSET + 25;
static const UINT DXR_HEAP_NRD_SPEC_RADIANCE_HITDIST_OFFSET = DXR_HEAP_UAV_OFFSET + 26;
static const UINT DXR_HEAP_NRD_VIEWZ_OFFSET = DXR_HEAP_UAV_OFFSET + 27;
static const UINT DXR_HEAP_NRD_NORMAL_ROUGHNESS_OFFSET = DXR_HEAP_UAV_OFFSET + 28;
static const UINT DXR_HEAP_NRD_MV_OFFSET = DXR_HEAP_UAV_OFFSET + 29;
static const UINT DXR_HEAP_NRD_OUT_DIFFUSE_OFFSET = DXR_HEAP_UAV_OFFSET + 30;
static const UINT DXR_HEAP_NRD_OUT_SPECULAR_OFFSET = DXR_HEAP_UAV_OFFSET + 31;
static const UINT DXR_HEAP_NRD_EMISSION_OFFSET = DXR_HEAP_UAV_OFFSET + 32;

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

inline void TransitionResource(ID3D12GraphicsCommandList *cmdList,
                               ID3D12Resource *resource,
                               D3D12_RESOURCE_STATES before,
                               D3D12_RESOURCE_STATES after) {
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
static ComPtr<ID3D12Resource> s_dlssOutputUAV;
static ComPtr<ID3D12Resource> s_oidnOutputUAV;
static ComPtr<ID3D12Resource> s_tonemapOutputUAV;
static ComPtr<ID3D12Resource> s_specularAlbedoUAV;
static ComPtr<ID3D12Resource> s_specHitDistanceUAV;
static ComPtr<ID3D12Resource> s_specularMotionVectorsUAV;

// NRD resources
static ComPtr<ID3D12Resource> s_nrdDiffuseRadianceHitDistUAV;
static ComPtr<ID3D12Resource> s_nrdSpecRadianceHitDistUAV;
static ComPtr<ID3D12Resource> s_nrdViewZUAV;
static ComPtr<ID3D12Resource> s_nrdNormalRoughnessUAV;
static ComPtr<ID3D12Resource> s_nrdMvUAV;
static ComPtr<ID3D12Resource> s_nrdOutDiffuseUAV;
static ComPtr<ID3D12Resource> s_nrdOutSpecularUAV;
static ComPtr<ID3D12Resource> s_nrdEmissionUAV;

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
static UINT64 s_shaderTableEntrySize = 0;
static D3D12_GPU_VIRTUAL_ADDRESS s_rayGenShaderTable = 0;
static D3D12_GPU_VIRTUAL_ADDRESS s_missShaderTable = 0;
static D3D12_GPU_VIRTUAL_ADDRESS s_hitGroupShaderTable = 0;

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
    TextureStreamingPolicy::Balanced;
static TextureStreamingPolicy s_lastAppliedTextureStreamingPolicy =
    TextureStreamingPolicy::Balanced;
static bool s_textureStreamingAuto = true;
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

static bool s_noiseConvergedLatched = false;
static bool s_cloudDescriptorsDone = false;
static bool s_hasDenoised = false;
static int s_lastRenderFrameFailReason = -1;

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

struct TonemapConstants {
  uint32_t outWidth;
  uint32_t outHeight;
  float exposure;
  float _pad;
};

static void EnsureTonemapPipeline() {
  if (s_tonemapPSO && s_tonemapRootSig && s_tonemapCB && s_tonemapHeap)
    return;
  if (!s_device)
    return;

  // Root signature: b0 constants, t0 SRV, u0 UAV
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

  D3D12_ROOT_SIGNATURE_DESC rsDesc{};
  rsDesc.NumParameters = _countof(params);
  rsDesc.pParameters = params;
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
    cs = s_dxcHelper.Compile(L"shaders/tonemap_cs.hlsl", L"CSMain", L"cs_6_3",
                             defines);
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

  // Descriptor heap: SRV + UAV
  D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
  heapDesc.NumDescriptors = 2;
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

    if (s_lightBuffer) {
      list->SetComputeRootShaderResourceView(
          2, s_lightBuffer->GetGPUVirtualAddress());
    }

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

  NrdDenoiser::Get().Initialize(s_device, s_commandQueue);

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

void CreateRayTracingPipeline(UINT width, UINT height) {
  if (!g_rayTracingSupported || !s_dxrDevice)
    return;

  // Track requested output (swapchain) size.
  if (width > 0)
    s_presentWidth = width;
  if (height > 0)
    s_presentHeight = height;

  const UINT outW = s_presentWidth;
  const UINT outH = s_presentHeight;

  // Compute internal render size (DLSS wants us to render smaller and upscale).
  UINT renderW = outW;
  UINT renderH = outH;
  if (s_streamline && s_streamline->IsInitialized() &&
      s_streamline->IsDeviceSet() && s_streamline->IsEnabled() &&
      s_streamline->GetMode() != StreamlineManager::Mode::Off) {
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
  s_textureTableDirty = true;
  s_cloudDescriptorsDone = false;
  s_asyncRestirPending = false;
  s_asyncRestirCameraCB.Reset();

  if (g_verboseRenderLogs) {
    fprintf(stderr,
            "DxrRenderer: Creating Ray Tracing Pipeline (size=%u x %u)...\n",
            s_outputWidth, s_outputHeight);
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

  // Compile shader
  ComPtr<IDxcBlob> shaderBlob;
  try {
    std::vector<std::wstring> compileDefines;
#ifdef _DEBUG
    compileDefines.push_back(L"SHADER_ENABLE_DEBUG=1");
    if (::g_dxrDebugUV)
      compileDefines.push_back(L"RAYGEN_DEBUG=1");
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
  D3D12_ROOT_PARAMETER params[13] =
      {}; // Increased for Lights, material extras, and cloud resources
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  params[0].Descriptor.ShaderRegister = 0;
  params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_DESCRIPTOR_RANGE uavRange = {};
  uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  uavRange.NumDescriptors = DXR_HEAP_UAV_COUNT; // u0..u24
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
  rootDesc.NumParameters = 13;
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
  static D3D12_EXPORT_DESC exports[4] = {};
  static D3D12_HIT_GROUP_DESC hitGroupDesc = {};
  static D3D12_RAYTRACING_SHADER_CONFIG shaderConfig = {};
  static D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig = {};
  static D3D12_GLOBAL_ROOT_SIGNATURE globalRootSigDesc = {};

  libDesc.DXILLibrary.pShaderBytecode = shaderBlob->GetBufferPointer();
  libDesc.DXILLibrary.BytecodeLength = shaderBlob->GetBufferSize();
  exports[0].Name = L"RayGen";
  exports[1].Name = L"Miss";
  exports[2].Name = L"ClosestHit";
  exports[3].Name = L"AnyHit";
  libDesc.NumExports = 4;
  libDesc.pExports = exports;
  D3D12_STATE_SUBOBJECT libSub = {};
  libSub.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
  libSub.pDesc = &libDesc;

  hitGroupDesc.HitGroupExport = L"HitGroup";
  hitGroupDesc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
  hitGroupDesc.ClosestHitShaderImport = L"ClosestHit";
  hitGroupDesc.AnyHitShaderImport = L"AnyHit";
  D3D12_STATE_SUBOBJECT hitSub = {};
  hitSub.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
  hitSub.pDesc = &hitGroupDesc;

  fprintf(stderr, "DxrRenderer: MaxPayloadSizeInBytes=%u\n", 32);
  shaderConfig.MaxPayloadSizeInBytes = 32;
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
  void *rayGenId = properties->GetShaderIdentifier(L"RayGen");
  void *missId = properties->GetShaderIdentifier(L"Miss");
  void *hitGroupId = properties->GetShaderIdentifier(L"HitGroup");
  if (!rayGenId || !missId || !hitGroupId) {
    fprintf(stderr, "DxrRenderer: Shader IDs null\n");
    return;
  }
  UINT shaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
  // RayGen record must be aligned to 64 bytes (shader table alignment),
  // miss/hit records must be aligned to 32 bytes (shader record alignment).
  UINT64 s_rayGenEntrySize =
      Align(shaderIdentifierSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
  s_shaderTableEntrySize = Align(shaderIdentifierSize,
                                 D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
  // Total SBT size: raygen + miss + hit (each 1 entry for now)
  UINT64 shaderTableSize =
      s_rayGenEntrySize + s_shaderTableEntrySize + s_shaderTableEntrySize;
  AllocateUploadBuffer(s_device, nullptr, shaderTableSize, &s_sbtStorage,
                       L"Shader Table");
  UINT8 *pData = nullptr;
  s_sbtStorage->Map(0, nullptr, (void **)&pData);
  // Place RayGen at beginning (64-byte aligned slot)
  memcpy(pData, rayGenId, shaderIdentifierSize);
  // Place Miss right after raygen (start aligned to 64 bytes as raygen slot is
  // 64)
  memcpy(pData + s_rayGenEntrySize, missId, shaderIdentifierSize);
  // Place HitGroup after miss (aligned to 32 bytes)
  memcpy(pData + s_rayGenEntrySize + s_shaderTableEntrySize, hitGroupId,
         shaderIdentifierSize);
  s_sbtStorage->Unmap(0, nullptr);
  D3D12_GPU_VIRTUAL_ADDRESS baseAddr = s_sbtStorage->GetGPUVirtualAddress();
  s_rayGenShaderTable = baseAddr;
  s_missShaderTable = baseAddr + s_rayGenEntrySize;
  s_hitGroupShaderTable = s_missShaderTable + s_shaderTableEntrySize;

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
  s_specularAlbedoUAV.Reset();
  s_specHitDistanceUAV.Reset();
  s_specularMotionVectorsUAV.Reset();
  s_normalRoughnessUAV.Reset();
  s_nrdDiffuseRadianceHitDistUAV.Reset();
  s_nrdSpecRadianceHitDistUAV.Reset();
  s_nrdViewZUAV.Reset();
  s_nrdNormalRoughnessUAV.Reset();
  s_nrdMvUAV.Reset();
  s_nrdOutDiffuseUAV.Reset();
  s_nrdOutSpecularUAV.Reset();
  s_nrdEmissionUAV.Reset();
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

  // DLSS/Streamline inputs for DXR
  CreateUavTexture(s_depthUAV, texDesc, DXGI_FORMAT_R32_FLOAT, L"RT Depth");
  CreateUavTexture(s_mvecUAV, texDesc, DXGI_FORMAT_R16G16_FLOAT,
                   L"RT Motion Vectors");
  CreateUavTexture(s_albedoUAV, texDesc, DXGI_FORMAT_R16G16B16A16_FLOAT,
                   L"RT Albedo", true); // Shared for OIDN
  CreateUavTexture(s_specularAlbedoUAV, texDesc, DXGI_FORMAT_R16G16B16A16_FLOAT,
                   L"RT Specular Albedo");
  CreateUavTexture(s_specHitDistanceUAV, texDesc, DXGI_FORMAT_R32_FLOAT,
                   L"RT Specular HitDistance");
  CreateUavTexture(s_specularMotionVectorsUAV, texDesc,
                   DXGI_FORMAT_R16G16_FLOAT, L"RT Specular MotionVectors");
  CreateUavTexture(s_normalRoughnessUAV, texDesc,
                   DXGI_FORMAT_R16G16B16A16_FLOAT, L"RT NormalRoughness",
                   true); // Shared for OIDN

  // NRD required resources (all are full window bounds, mostly FP16 or FP32)
  CreateUavTexture(s_nrdDiffuseRadianceHitDistUAV, texDesc, DXGI_FORMAT_R16G16B16A16_FLOAT, L"NRD DiffRadiance HitDist");
  CreateUavTexture(s_nrdSpecRadianceHitDistUAV, texDesc, DXGI_FORMAT_R16G16B16A16_FLOAT, L"NRD SpecRadiance HitDist");
  CreateUavTexture(s_nrdViewZUAV, texDesc, DXGI_FORMAT_R32_FLOAT, L"NRD ViewZ");
  CreateUavTexture(s_nrdNormalRoughnessUAV, texDesc, DXGI_FORMAT_R16G16B16A16_FLOAT, L"NRD NormalRoughness");
  CreateUavTexture(s_nrdMvUAV, texDesc, DXGI_FORMAT_R16G16_FLOAT, L"NRD MotionVectors");
  CreateUavTexture(s_nrdOutDiffuseUAV, texDesc, DXGI_FORMAT_R16G16B16A16_FLOAT, L"NRD Out Diffuse");
  CreateUavTexture(s_nrdOutSpecularUAV, texDesc, DXGI_FORMAT_R16G16B16A16_FLOAT, L"NRD Out Specular");
  CreateUavTexture(s_nrdEmissionUAV, texDesc, DXGI_FORMAT_R16G16B16A16_FLOAT, L"NRD Emission");

  // DLSS output is output-size in linear HDR (pre-tonemap).
  CreateUavTexture(s_dlssOutputUAV, outDesc, DXGI_FORMAT_R16G16B16A16_FLOAT,
                   L"RT DLSS Output (HDR)");

  // OIDN output (HDR) - same format as DLSS output for tonemapping.
  CreateUavTexture(s_oidnOutputUAV, outDesc, DXGI_FORMAT_R16G16B16A16_FLOAT,
                   L"RT OIDN Output (HDR)", true); // Shared for OIDN

  // Tonemap output is swapchain-format (output-size) for easy CopyResource.
  // Not shared (only used by internal CS and Copy)
  CreateUavTexture(s_tonemapOutputUAV, outDesc, DXGI_FORMAT_R10G10B10A2_UNORM,
                   L"RT Tonemap Output");

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
  CreateUavAt(s_oidnOutputUAV.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT,
              DXR_HEAP_OIDN_OUT_UAV_OFFSET);
  CreateUavAt(s_nrdDiffuseRadianceHitDistUAV.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT,
              DXR_HEAP_NRD_DIFFUSE_RADIANCE_HITDIST_OFFSET);
  CreateUavAt(s_nrdSpecRadianceHitDistUAV.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT,
              DXR_HEAP_NRD_SPEC_RADIANCE_HITDIST_OFFSET);
  CreateUavAt(s_nrdViewZUAV.Get(), DXGI_FORMAT_R32_FLOAT,
              DXR_HEAP_NRD_VIEWZ_OFFSET);
  CreateUavAt(s_nrdNormalRoughnessUAV.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT,
              DXR_HEAP_NRD_NORMAL_ROUGHNESS_OFFSET);
  CreateUavAt(s_nrdMvUAV.Get(), DXGI_FORMAT_R16G16_FLOAT,
              DXR_HEAP_NRD_MV_OFFSET);
  CreateUavAt(s_nrdOutDiffuseUAV.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT,
              DXR_HEAP_NRD_OUT_DIFFUSE_OFFSET);
  CreateUavAt(s_nrdOutSpecularUAV.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT,
              DXR_HEAP_NRD_OUT_SPECULAR_OFFSET);
  CreateUavAt(s_nrdEmissionUAV.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT,
              DXR_HEAP_NRD_EMISSION_OFFSET);

  // Prepare tonemap pipeline resources.
  EnsureTonemapPipeline();

  // Initialize NRD wrapper
  NrdDenoiser::Get().Recreate(s_outputWidth, s_outputHeight);

  // Create Accumulation UAV
  s_accumulation.Resize(s_outputWidth, s_outputHeight);
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
  const float maxRefr =
      (std::max)(m.refractionColor[0],
                 (std::max)(m.refractionColor[1], m.refractionColor[2]));
  const bool glassLike = (maxRefr > 0.01f) || (m.thinWalled > 0.5f);
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
}

void RequestAccelerationStructureRebuild() { s_forceAsRebuild = true; }

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
    const size_t BLAS_BATCH_SIZE = 500;
    const bool enableBlasCompaction = meshes.size() <= 1500;
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
                  enableBlasCompaction);
          if (bl.result && bl.scratch) {
            s_allBLAS.push_back({bl, (UINT64)i});
            s_cachedMeshBuffersForBlas.push_back(mesh.vertexBuffer.Get());
            s_cachedMeshOpaqueForBlas.push_back(meshOpaqueStates[i]);
          }

          batchCount++;
          if (batchCount >= BLAS_BATCH_SIZE) {
            fprintf(stderr,
                    "DxrRenderer: Submitting BLAS batch (mesh %zu/%zu)...\n", i,
                    meshes.size());
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
      const UINT grassRequested = GrassManager::GetInstanceCount();
      if (grassRequested > 0) {
        const Asset::GpuMesh *patchMesh = GrassManager::GetPatchMesh();
        UINT64 patchBlasAddr = 0;
        UINT patchMeshIndex = 0;
        if (patchMesh && patchMesh->vertexBuffer) {
          auto patchIt = meshToBlasIndex.find(patchMesh->vertexBuffer.Get());
          if (patchIt != meshToBlasIndex.end()) {
            const size_t patchBlasIndex = patchIt->second;
            if (patchBlasIndex < s_allBLAS.size() &&
                s_allBLAS[patchBlasIndex].buffers.result) {
              patchBlasAddr =
                  s_allBLAS[patchBlasIndex].buffers.result->GetGPUVirtualAddress();
              patchMeshIndex = (UINT)s_allBLAS[patchBlasIndex].meshId;
            }
          }
        }

        if (patchBlasAddr != 0) {
          const auto &blades = GrassManager::GetBlades();
          instanceDescs.reserve(instanceDescs.size() + blades.size());
          instanceMeshOrder.reserve(instanceMeshOrder.size() + blades.size());
          for (const FGrassBlade &b : blades) {
            const float s = sinf(b.yawRadians);
            const float c = cosf(b.yawRadians);
            const float sc = (std::max)(b.scale, 1e-3f);

            D3D12_RAYTRACING_INSTANCE_DESC inst = {};
            inst.Transform[0][0] = c * sc;
            inst.Transform[0][1] = 0.0f;
            inst.Transform[0][2] = -s * sc;
            inst.Transform[0][3] = b.position.x;
            inst.Transform[1][0] = 0.0f;
            inst.Transform[1][1] = sc;
            inst.Transform[1][2] = 0.0f;
            inst.Transform[1][3] = b.position.y;
            inst.Transform[2][0] = s * sc;
            inst.Transform[2][1] = 0.0f;
            inst.Transform[2][2] = c * sc;
            inst.Transform[2][3] = b.position.z;
            inst.InstanceID = patchMeshIndex;
            inst.InstanceMask = 0xFF;
            inst.InstanceContributionToHitGroupIndex = 0;
            inst.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE;
            inst.AccelerationStructure = patchBlasAddr;
            instanceDescs.push_back(inst);
            instanceMeshOrder.push_back(patchMesh);
          }
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

void UpdateLights(const std::vector<Light> &lights) {
  WaitForAsyncRestirIdleForLightUpdates();

  if (lights.empty()) {
    if (s_lightCount != 0) {
      s_lightCount = 0;
      s_lastLightsCpu.clear();
      ResetAccumulation();
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

  ResetAccumulation();
}

void ResetAccumulation() {
  s_accumulation.Reset();
  s_rrStillFrameSpp = 0;
  s_lastNoiseLevel = 0.0f; // Reset noise level so rendering restarts
  s_hasNoiseEstimate = false;
  s_noiseStatsDispatchCount = 0;
  s_noiseConvergedLatched = false;
  s_hasTonemappedFrame = false;
  s_hasDenoised = false; // Reset auto-denoiser state
  // Keep Streamline history reset separate from accumulation decisions.
  // Accumulation resets happen on real camera/settings changes; per-frame
  // jitter changes must not trigger this.
  s_streamlineResetHistory = true;
  if (g_verboseRenderLogs) {
    fprintf(stderr, "DxrRenderer: Accumulation Reset\n");
  }
}

void SetStreamlineManager(StreamlineManager *streamline) {
  s_streamline = streamline;
  s_streamlineResetHistory = true;
}

void ResetStreamlineHistory() {
  // Resetting DLSS history should resume sampling even if we previously froze.
  s_rrStillFrameSpp = 0;
  s_hasTonemappedFrame = false;
  s_streamlineResetHistory = true;
}

void SetDenoiserMode(DenoiserMode m) {
  if (s_denoiserMode == m)
    return;
  s_denoiserMode = m;
  
  g_cameraData.nrdEnabled = (m == DenoiserMode::NRD_RELAX) ? 1.0f : 0.0f;
  UpdateCameraCB();

  if (s_denoiserMode != DenoiserMode::Off) {
    // Try to initialize OIDN wrapper; if device isn't ready, initialization
    // will be attempted on first RunDenoise call.
    s_oidnDenoiser.Initialize(s_device);
  } else {
    s_oidnDenoiser.Shutdown();
  }
  // Reset accumulation as denoiser mode change may affect post-process outputs
  DxrRenderer::ResetAccumulation();
}

DenoiserMode GetDenoiserMode() { return s_denoiserMode; }

void SetOidnQuality(OidnDenoiser::Quality q) {
  s_oidnQuality = q;
  s_oidnDenoiser.SetQuality(q);
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
  return g_rayTracingSupported && s_rtStateObject != nullptr &&
         s_tlas.result != nullptr;
}

bool RenderFrame(ID3D12GraphicsCommandList *commandListBase,
                 ID3D12CommandAllocator *cmdAlloc, UINT frameIndex,
                 ID3D12Resource *renderTarget,
                 D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
                 ID3D12Resource *cameraCB, ID3D12Resource *materialCB,
                 D3D12_GPU_DESCRIPTOR_HANDLE texturesGpuStart,
                 UINT textureDescriptorCount,
                 const std::vector<const Asset::GpuMesh *> &meshes,
                 ID3D12Resource *meshDataSB, ID3D12Resource *materialExtraSB) {
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
  if (!renderTarget) {
    return ReturnFail(2, "renderTarget is null");
  }

  // Handle empty scene or missing TLAS gracefully
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

  // Material edits only require AS rebuild when opaque-vs-nonopaque state
  // changes (affects BLAS geometry flags / AnyHit path).
  if (s_forceAsRebuild) {
    BuildAccelerationStructures(meshes, Scene::GetInstances());
    s_forceAsRebuild = false;
    if (!s_tlas.result) {
      return ReturnFail(16, "TLAS missing after forced rebuild");
    }
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

  bool usedOidn = false;

  // If we've hit maxSPP and the camera/settings haven't changed (meaning
  // ResetAccumulation hasn't been called), freeze rendering and keep presenting
  // the last tonemapped output. This works for both accumulation and DLSS-RR.
  const UINT maxSpp =
      (g_cameraData.maxSPP > 0.0f) ? (UINT)g_cameraData.maxSPP : 0u;
  const UINT currSpp =
      rrActive ? s_rrStillFrameSpp : s_accumulation.GetFrameCount();
  const bool debugViewActive = (g_cameraData.debugMode != 0.0f) ||
                               (g_cameraData.debugVisualizationMode != 0.0f);

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
  bool isOidnMode =
      (s_denoiserMode != DxrRenderer::DenoiserMode::Off && !dlssActive);
  bool reachedEndCondition = ((maxSpp > 0 && currSpp >= maxSpp) || isConverged);

  bool canAutoDenoise = isOidnMode && reachedEndCondition && !s_hasDenoised;
  bool doDenoise = canAutoDenoise;

  // Flag to freeze after tonemapping instead of early return
  bool shouldFreezeAfterTonemap = reachedEndCondition && !doDenoise;

  // Bake clouds before DXR state binding. BakeSky uses compute
  // PSO/root-signature and descriptor heaps, so it must not run mid-way through
  // DXR root bindings.
  if (g_cloudManager.NeedsBake()) {
    g_cloudManager.BakeSky(commandListBase, cameraCB);
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

      // 4. Create Baked Sky SRV at DXR_HEAP_CLOUD_BAKED_TEX_OFFSET
      D3D12_CPU_DESCRIPTOR_HANDLE srvCpuBaked =
          s_srvHeap->GetCPUDescriptorHandleForHeapStart();
      srvCpuBaked.ptr += (SIZE_T)DXR_HEAP_CLOUD_BAKED_TEX_OFFSET *
                         s_device->GetDescriptorHandleIncrementSize(
                             D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

      if (g_cloudManager.GetBakedSkyTexture()) {
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

      s_cloudDescriptorsDone = true;
    }

    // 4. Bind Resources
    // Slot 10: Root CBV
    dxrList->SetComputeRootConstantBufferView(
        10, g_cloudManager.GetConstantBufferAddr());

    // Slot 11: SRV Table
    D3D12_GPU_DESCRIPTOR_HANDLE cloudSRV =
        s_srvHeap->GetGPUDescriptorHandleForHeapStart();
    cloudSRV.ptr += (UINT64)DXR_HEAP_CLOUD_TEX_OFFSET *
                    s_device->GetDescriptorHandleIncrementSize(
                        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    dxrList->SetComputeRootDescriptorTable(11, cloudSRV);
  }

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
  if (s_lightBuffer) {
    dxrList->SetComputeRootShaderResourceView(
        9, s_lightBuffer->GetGPUVirtualAddress());
  }

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
    if (s_shaderCountersBuffer) {
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
  }

  D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
  // RayGen record size must match the raygen slot size (may be 64-aligned)
  UINT64 s_rayGenEntrySize =
      Align(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES,
            D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
  dispatchDesc.RayGenerationShaderRecord.StartAddress = s_rayGenShaderTable;
  dispatchDesc.RayGenerationShaderRecord.SizeInBytes = s_rayGenEntrySize;
  // Miss/Hit tables: one entry each for now
  dispatchDesc.MissShaderTable.StartAddress = s_missShaderTable;
  dispatchDesc.MissShaderTable.SizeInBytes = s_shaderTableEntrySize * 1;
  dispatchDesc.MissShaderTable.StrideInBytes = s_shaderTableEntrySize;
  dispatchDesc.HitGroupTable.StartAddress = s_hitGroupShaderTable;
  dispatchDesc.HitGroupTable.SizeInBytes = s_shaderTableEntrySize * 1;
  dispatchDesc.HitGroupTable.StrideInBytes = s_shaderTableEntrySize;

  // Dispatch size should match created output texture size
  dispatchDesc.Width = s_outputWidth;
  dispatchDesc.Height = s_outputHeight;
  dispatchDesc.Depth = 1;

  // Only Dispatch Rays if we are NOT in a pure denoise pass.
  // If we are denoising an already-completed frame (e.g. at MaxSPP),
  // we do not want to add more samples or modify the accumulation buffer.

  bool didDispatchRays = false;
  if (!doDenoise && !reachedEndCondition) {
    // Start DispatchRays timer
    if (s_queryHeap) {
      dxrList->EndQuery(s_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                        3); // Dispatch start
    }

    // Ensure shader counters are reset per-frame (debug instrumentation)
    if (s_shaderCountersBuffer) {
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

    dxrList->DispatchRays(&dispatchDesc);
    didDispatchRays = true;

    // End DispatchRays timer
    if (s_queryHeap) {
      dxrList->EndQuery(s_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                        4); // Dispatch end
    }

    // Increment accumulation history only when actually used and not at end
    // condition.
    if (!rrActive && !reachedEndCondition)
      s_accumulation.IncrementFrame();
    else if (rrActive && !reachedEndCondition)
      s_rrStillFrameSpp++;
  } else {
    // We are skipping dispatch to run OIDN on the existing buffer or because
    // we reached an end condition (noise/maxSPP).
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

  // Phase 4: decoupled ReSTIR DI temporal/spatial reuse in a dedicated compute
  // pass. RayGen now writes initial candidates, and this pass performs reuse.
  if (didDispatchRays) {
    if (cameraCB && s_asyncRestirAvailable && hasAsyncCameraSnapshot &&
        UploadAsyncRestirCamera(asyncCameraSnapshot)) {
      // Submit on async queue after this frame's direct list executes.
      s_asyncRestirPending = true;
    } else if (cameraCB) {
      DispatchRestirSpatialPasses(dxrList.Get(), cameraCB);
    }
  }

  // Optional Streamline / DLSS evaluation
  ID3D12Resource *postColor = s_outputUAV.Get();

  // Evaluate NRD
  if (!debugViewActive && s_denoiserMode == DenoiserMode::NRD_RELAX && s_nrdOutDiffuseUAV) {
    if (s_queryHeap) {
      dxrList->EndQuery(s_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                        5); // Start Denoise
    }

    const bool resetHistory = s_streamlineResetHistory || (currSpp == 0) || !s_accumulation.IsAccumulating();

    NrdDenoiser::Get().Denoise(
        dxrList.Get(),
        s_nrdDiffuseRadianceHitDistUAV.Get(),
        s_nrdSpecRadianceHitDistUAV.Get(),
        s_nrdViewZUAV.Get(),
        s_nrdNormalRoughnessUAV.Get(),
        s_nrdMvUAV.Get(),
        s_nrdOutDiffuseUAV.Get(),
        s_nrdOutSpecularUAV.Get(),
        g_cameraData,
        jitterX, jitterY,
        resetHistory
    );

    postColor = s_nrdOutDiffuseUAV.Get();

    if (s_queryHeap) {
      dxrList->EndQuery(s_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                        6); // End Denoise
    }
  }

  // After one-shot OIDN at end conditions, keep showing the denoised HDR buffer
  // instead of falling back to the raw output on following frames.
  if (reachedEndCondition && isOidnMode && s_hasDenoised && s_oidnOutputUAV) {
    postColor = s_oidnOutputUAV.Get();
  }
  bool usedDlss = false;
  const D3D12_RESOURCE_DESC dstDesc = renderTarget->GetDesc();
  const uint32_t outW = (uint32_t)dstDesc.Width;
  const uint32_t outH = (uint32_t)dstDesc.Height;

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

  // If DLSS wasn't used, allow OIDN (or other denoiser) to operate on the
  // linear HDR output as a post-process cleanup step.
  // IMPORTANT: When DLSS is active, color is output-resolution while our AOVs
  // (albedo/normal) are render-resolution. Running OIDN in that configuration
  // produces invalid results. Keep OIDN gated to the non-DLSS path.
  bool shouldRunOidn = doDenoise && !usedDlss;

  if (shouldRunOidn && s_oidnOutputUAV && postColor) {
    // Start denoising timer
    if (s_queryHeap) {
      dxrList->EndQuery(s_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                        5); // Denoise start
    }

    fprintf(stderr,
            "DxrRenderer: MaxSPP reached. Auto-triggering OIDN denoise.\n");
    s_oidnDenoiser.Initialize(s_device);

    // Ensure input is in COMMON state for interop
    // postColor is currently in UAV state (either s_outputUAV or
    // s_dlssOutputUAV)
    TransitionResource(dxrList.Get(), postColor,
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

    // s_oidnOutputUAV was created/kept as UAV; transition to COMMON for OIDN
    // write
    TransitionResource(dxrList.Get(), s_oidnOutputUAV.Get(),
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                       D3D12_RESOURCE_STATE_COMMON);

    // FLUSH & SYNC for OIDN:
    // We must execute the command list to ensure resources are in COMMON state
    // before OIDN tries to access them.
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

      // Run OIDN.
      // We pass the command queue so the denoiser can manage its own internal
      // copy-execute-sync cycle to handle Tiled <-> Linear layout conversion
      // for D3D12 interop.
      bool ran = s_oidnDenoiser.RunDenoise(
          dxrList.Get(), s_commandQueue, postColor, s_albedoUAV.Get(),
          s_normalRoughnessUAV.Get(), s_oidnOutputUAV.Get(), false);

      // Restore Resource States from COMMON to what the engine expects.
      TransitionResource(dxrList.Get(), postColor, D3D12_RESOURCE_STATE_COMMON,
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
        usedOidn = true;
        postColor = s_oidnOutputUAV.Get();
        fprintf(stderr, "DxrRenderer: OIDN denoise completed successfully.\n");
      } else {
        fprintf(stderr, "DxrRenderer: OIDN denoise did not run (unsupported "
                        "config or failure).\n");
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
      srv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
      srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      srv.Texture2D.MipLevels = 1;
      s_device->CreateShaderResourceView(postColor, &srv, cpuHandle);

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
      TransitionResource(dxrList.Get(), postColor,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                         D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
      postColorInSrv = true;

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

    void *p = nullptr;
    D3D12_RANGE readRange = {0, 0};
    if (SUCCEEDED(s_tonemapCB->Map(0, &readRange, &p))) {
      memcpy(p, &tc, sizeof(tc));
      s_tonemapCB->Unmap(0, nullptr);
    }

    // Create SRV (slot 0) and UAV (slot 1)
    const UINT descInc = s_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE cpuStart =
        s_tonemapHeap->GetCPUDescriptorHandleForHeapStart();

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    s_device->CreateShaderResourceView(postColor, &srv, cpuStart);

    D3D12_CPU_DESCRIPTOR_HANDLE uavCpu = cpuStart;
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
    gpuUav.ptr += descInc;
    dxrList->SetComputeRootDescriptorTable(2, gpuUav);

    const UINT gx = (outW + 7) / 8;
    const UINT gy = (outH + 7) / 8;
    dxrList->Dispatch(gx, gy, 1);

    // Copy tonemapped output to the render target
    TransitionResource(dxrList.Get(), s_tonemapOutputUAV.Get(),
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                       D3D12_RESOURCE_STATE_COPY_SOURCE);
    TransitionResource(dxrList.Get(), renderTarget,
                       D3D12_RESOURCE_STATE_PRESENT,
                       D3D12_RESOURCE_STATE_COPY_DEST);
    dxrList->CopyResource(renderTarget, s_tonemapOutputUAV.Get());

    // Transition back
    TransitionResource(dxrList.Get(), renderTarget,
                       D3D12_RESOURCE_STATE_COPY_DEST,
                       D3D12_RESOURCE_STATE_RENDER_TARGET);
    TransitionResource(dxrList.Get(), s_tonemapOutputUAV.Get(),
                       D3D12_RESOURCE_STATE_COPY_SOURCE,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    TransitionResource(dxrList.Get(), postColor,
                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    if (usedOidn) {
      // If we just ran a one-shot or continuous OIDN pass, we can mark the
      // frame as tonemapped so that if maxSPP is reached, we don't keep
      // re-tonemapping the same results.
      // Additionally, for one-shot denoise, this helps keep the result on
      // screen.
      s_hasTonemappedFrame = true;
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
      TransitionResource(dxrList.Get(), freezeSrc,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                         D3D12_RESOURCE_STATE_COPY_SOURCE);
      TransitionResource(dxrList.Get(), renderTarget,
                         D3D12_RESOURCE_STATE_PRESENT,
                         D3D12_RESOURCE_STATE_COPY_DEST);
      dxrList->CopyResource(renderTarget, freezeSrc);

      s_hasTonemappedFrame = true;
      TransitionResource(dxrList.Get(), renderTarget,
                         D3D12_RESOURCE_STATE_COPY_DEST,
                         D3D12_RESOURCE_STATE_RENDER_TARGET);
      TransitionResource(dxrList.Get(), freezeSrc,
                         D3D12_RESOURCE_STATE_COPY_SOURCE,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
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

static bool ExportNrdRadianceHitDistTexture(const std::wstring &directoryPath,
                                            const std::wstring &baseName,
                                            ID3D12Resource *resource) {
  if (!resource)
    return false;

  std::vector<uint8_t> raw;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
  UINT width = 0, height = 0;
  if (!ReadbackTexture2D(resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, raw,
                         footprint, width, height)) {
    return false;
  }

  std::vector<uint8_t> rgba((size_t)width * (size_t)height * 4u, 255u);
  std::vector<float> alpha((size_t)width * (size_t)height, 0.0f);
  float lumMin = (std::numeric_limits<float>::max)();
  float lumMax = 0.0f;
  float alphaMin = (std::numeric_limits<float>::max)();
  float alphaMax = 0.0f;

  for (UINT y = 0; y < height; ++y) {
    const uint16_t *srcRow = (const uint16_t *)(raw.data() + footprint.Offset +
                                                (size_t)y * footprint.Footprint.RowPitch);
    for (UINT x = 0; x < width; ++x) {
      const size_t idx = (size_t)y * width + x;
      const float r = HalfToFloat(srcRow[x * 4 + 0]);
      const float g = HalfToFloat(srcRow[x * 4 + 1]);
      const float b = HalfToFloat(srcRow[x * 4 + 2]);
      const float a = HalfToFloat(srcRow[x * 4 + 3]);
      const float lum = (std::max)(0.0f, 0.2126f * r + 0.7152f * g + 0.0722f * b);
      lumMin = (std::min)(lumMin, lum);
      lumMax = (std::max)(lumMax, lum);
      if (std::isfinite(a) && a > 0.0f) {
        alphaMin = (std::min)(alphaMin, a);
        alphaMax = (std::max)(alphaMax, a);
      }
      alpha[idx] = a;

      rgba[idx * 4 + 0] = LinearToSrgbByte(r / (1.0f + (std::max)(0.0f, r)));
      rgba[idx * 4 + 1] = LinearToSrgbByte(g / (1.0f + (std::max)(0.0f, g)));
      rgba[idx * 4 + 2] = LinearToSrgbByte(b / (1.0f + (std::max)(0.0f, b)));
    }
  }

  if (lumMin == (std::numeric_limits<float>::max)())
    lumMin = 0.0f;
  if (alphaMin == (std::numeric_limits<float>::max)())
    alphaMin = 0.0f;

  fprintf(stderr,
          "DxrRenderer: %ls RGB luminance range [%.6f, %.6f], alpha range "
          "[%.6f, %.6f]\n",
          baseName.c_str(), lumMin, lumMax, alphaMin, alphaMax);

  const std::wstring rgbPath =
      directoryPath + L"/" + baseName + L"_rgb_lum_" + FloatTag(lumMin) + L"_" +
      FloatTag(lumMax) + L".png";
  const std::wstring aPath =
      directoryPath + L"/" + baseName + L"_a_" + FloatTag(alphaMin) + L"_" +
      FloatTag(alphaMax) + L".png";

  const bool rgbOk =
      SaveRgba8ToPngWic(rgbPath, width, height, rgba.data(), width * 4u);
  const bool aOk = SaveGrayPngFromFloats(aPath, width, height, alpha, alphaMin,
                                         alphaMax, true);
  return rgbOk && aOk;
}

static bool ExportNrdNormalRoughnessTexture(const std::wstring &directoryPath,
                                            const std::wstring &baseName,
                                            ID3D12Resource *resource) {
  if (!resource)
    return false;

  std::vector<uint8_t> raw;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
  UINT width = 0, height = 0;
  if (!ReadbackTexture2D(resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, raw,
                         footprint, width, height)) {
    return false;
  }

  std::vector<uint8_t> normalRgba((size_t)width * (size_t)height * 4u, 255u);
  std::vector<float> roughness((size_t)width * (size_t)height, 0.0f);
  float roughMin = (std::numeric_limits<float>::max)();
  float roughMax = 0.0f;

  for (UINT y = 0; y < height; ++y) {
    const uint16_t *srcRow = (const uint16_t *)(raw.data() + footprint.Offset +
                                                (size_t)y * footprint.Footprint.RowPitch);
    for (UINT x = 0; x < width; ++x) {
      const size_t idx = (size_t)y * width + x;
      const float nx = HalfToFloat(srcRow[x * 4 + 0]);
      const float ny = HalfToFloat(srcRow[x * 4 + 1]);
      const float nz = HalfToFloat(srcRow[x * 4 + 2]);
      const float r = HalfToFloat(srcRow[x * 4 + 3]);
      normalRgba[idx * 4 + 0] = Float01ToByte(nx * 0.5f + 0.5f);
      normalRgba[idx * 4 + 1] = Float01ToByte(ny * 0.5f + 0.5f);
      normalRgba[idx * 4 + 2] = Float01ToByte(nz * 0.5f + 0.5f);
      roughness[idx] = r;
      if (std::isfinite(r)) {
        roughMin = (std::min)(roughMin, r);
        roughMax = (std::max)(roughMax, r);
      }
    }
  }

  if (roughMin == (std::numeric_limits<float>::max)())
    roughMin = 0.0f;
  fprintf(stderr, "DxrRenderer: %ls roughness range [%.6f, %.6f]\n",
          baseName.c_str(), roughMin, roughMax);

  const std::wstring normalPath =
      directoryPath + L"/" + baseName + L"_normal.png";
  const std::wstring roughPath =
      directoryPath + L"/" + baseName + L"_roughness_" + FloatTag(roughMin) +
      L"_" + FloatTag(roughMax) + L".png";

  const bool normalOk = SaveRgba8ToPngWic(normalPath, width, height,
                                          normalRgba.data(), width * 4u);
  const bool roughOk = SaveGrayPngFromFloats(roughPath, width, height,
                                             roughness, roughMin, roughMax,
                                             false);
  return normalOk && roughOk;
}

static bool ExportNrdViewZTexture(const std::wstring &directoryPath,
                                  const std::wstring &baseName,
                                  ID3D12Resource *resource) {
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
  for (UINT y = 0; y < height; ++y) {
    const float *srcRow =
        (const float *)(raw.data() + footprint.Offset +
                        (size_t)y * footprint.Footprint.RowPitch);
    for (UINT x = 0; x < width; ++x) {
      const float v = srcRow[x];
      values[(size_t)y * width + x] = v;
      if (std::isfinite(v) && v > 0.0f) {
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
  return SaveGrayPngFromFloats(path, width, height, values, minV, maxV, true);
}

static bool ExportNrdMotionTexture(const std::wstring &directoryPath,
                                   const std::wstring &baseName,
                                   ID3D12Resource *resource) {
  if (!resource)
    return false;

  std::vector<uint8_t> raw;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
  UINT width = 0, height = 0;
  if (!ReadbackTexture2D(resource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, raw,
                         footprint, width, height)) {
    return false;
  }

  std::vector<uint8_t> rgba((size_t)width * (size_t)height * 4u, 255u);
  float maxAbs = 0.0f;
  for (UINT y = 0; y < height; ++y) {
    const uint16_t *srcRow = (const uint16_t *)(raw.data() + footprint.Offset +
                                                (size_t)y * footprint.Footprint.RowPitch);
    for (UINT x = 0; x < width; ++x) {
      const float mx = HalfToFloat(srcRow[x * 2 + 0]);
      const float my = HalfToFloat(srcRow[x * 2 + 1]);
      if (std::isfinite(mx))
        maxAbs = (std::max)(maxAbs, fabsf(mx));
      if (std::isfinite(my))
        maxAbs = (std::max)(maxAbs, fabsf(my));
    }
  }
  maxAbs = (std::max)(maxAbs, 1e-6f);

  for (UINT y = 0; y < height; ++y) {
    const uint16_t *srcRow = (const uint16_t *)(raw.data() + footprint.Offset +
                                                (size_t)y * footprint.Footprint.RowPitch);
    for (UINT x = 0; x < width; ++x) {
      const size_t idx = (size_t)y * width + x;
      const float mx = HalfToFloat(srcRow[x * 2 + 0]);
      const float my = HalfToFloat(srcRow[x * 2 + 1]);
      const float nx = std::isfinite(mx) ? (0.5f + 0.5f * mx / maxAbs) : 0.5f;
      const float ny = std::isfinite(my) ? (0.5f + 0.5f * my / maxAbs) : 0.5f;
      const float mag =
          std::isfinite(mx) && std::isfinite(my)
              ? (sqrtf(mx * mx + my * my) / maxAbs)
              : 0.0f;
      rgba[idx * 4 + 0] = Float01ToByte(nx);
      rgba[idx * 4 + 1] = Float01ToByte(ny);
      rgba[idx * 4 + 2] = Float01ToByte(mag);
    }
  }

  fprintf(stderr, "DxrRenderer: %ls maxAbs %.6f\n", baseName.c_str(), maxAbs);

  const std::wstring path =
      directoryPath + L"/" + baseName + L"_maxabs_" + FloatTag(maxAbs) +
      L".png";
  return SaveRgba8ToPngWic(path, width, height, rgba.data(), width * 4u);
}

bool ExportNrdDebugBuffersToPng(const std::wstring &directoryPath) {
  if (directoryPath.empty() || !s_device) {
    fprintf(stderr,
            "DxrRenderer: ExportNrdDebugBuffersToPng invalid arguments.\n");
    return false;
  }
  if (!s_nrdDiffuseRadianceHitDistUAV || !s_nrdSpecRadianceHitDistUAV ||
      !s_nrdViewZUAV || !s_nrdNormalRoughnessUAV || !s_nrdMvUAV ||
      !s_nrdOutDiffuseUAV || !s_nrdOutSpecularUAV) {
    fprintf(stderr,
            "DxrRenderer: ExportNrdDebugBuffersToPng missing NRD resources.\n");
    return false;
  }

  std::error_code ec;
  std::filesystem::create_directories(directoryPath, ec);
  if (ec) {
    fprintf(stderr, "DxrRenderer: failed to create export dir.\n");
    return false;
  }

  bool ok = true;
  ok &= ExportNrdRadianceHitDistTexture(directoryPath, L"nrd_in_diffuse",
                                        s_nrdDiffuseRadianceHitDistUAV.Get());
  ok &= ExportNrdRadianceHitDistTexture(directoryPath, L"nrd_in_specular",
                                        s_nrdSpecRadianceHitDistUAV.Get());
  ok &= ExportNrdViewZTexture(directoryPath, L"nrd_in_viewz",
                              s_nrdViewZUAV.Get());
  ok &= ExportNrdNormalRoughnessTexture(directoryPath, L"nrd_in_normal_roughness",
                                        s_nrdNormalRoughnessUAV.Get());
  ok &= ExportNrdMotionTexture(directoryPath, L"nrd_in_mv", s_nrdMvUAV.Get());
  ok &= ExportNrdRadianceHitDistTexture(directoryPath, L"nrd_out_diffuse",
                                        s_nrdOutDiffuseUAV.Get());
  ok &= ExportNrdRadianceHitDistTexture(directoryPath, L"nrd_out_specular",
                                        s_nrdOutSpecularUAV.Get());

  fprintf(stderr, "DxrRenderer: NRD debug export %s to %ls\n",
          ok ? "completed" : "failed", directoryPath.c_str());
  return ok;
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

  if (s_queryHeap) {
    commandList->EndQuery(s_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                          9); // Frame end
    // Copy shader counters (GPU -> readback) so CPU can inspect them next map
    if (s_shaderCountersBuffer && s_shaderCountersReadbackBuffer) {
      TransitionResource(commandList, s_shaderCountersBuffer.Get(),
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                         D3D12_RESOURCE_STATE_COPY_SOURCE);
      commandList->CopyResource(s_shaderCountersReadbackBuffer.Get(),
                                s_shaderCountersBuffer.Get());
      TransitionResource(commandList, s_shaderCountersBuffer.Get(),
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
  if (s_shaderCountersReadbackBuffer) {
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
