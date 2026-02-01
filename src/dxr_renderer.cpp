#include "dxr_renderer.h"
#include "camera.h"
#include "d3d12_helpers.h"
#include "dxc_wrapper.h"
#include "dxr_accumulation.h"
#include "dxr_helpers.h"
#include "ibl_manager.h"
#include "scene.h"
#include "streamline_manager.h"
#include <cstdio>
#include <vector>
#include <unordered_map>
#include <wrl.h>
#include <sstream>
#include <cstdarg>

// Expose global debug flag (set by WinMain parsing)
extern bool g_debugLog;


using Microsoft::WRL::ComPtr;

// Access global CBV/SRV descriptor heap so DXR can bind scene textures
extern DescriptorHeapAllocator g_cbvSrvAllocator;
// Globals from main/scene for descriptor bookkeeping
extern D3D12_GPU_DESCRIPTOR_HANDLE g_texturesGpuStart;
extern UINT g_textureDescriptorCount;
extern Microsoft::WRL::ComPtr<ID3D12Device> g_device;

// Module-local state
static ID3D12Device *s_device = nullptr;
static ID3D12CommandQueue *s_commandQueue = nullptr;

static StreamlineManager *s_streamline = nullptr;
static bool s_streamlineResetHistory = true;
// RR jitter scale default: lower than 1.0 to reduce silhouette/screen-edge shimmer.
static float s_rrJitterScale = 0.5f;
// When DLSS-RR is active we don't use the accumulation buffer; track a still-frame
// SPP count separately so maxSPP can still freeze rendering.
static UINT s_rrStillFrameSpp = 0;
static bool s_hasTonemappedFrame = false;
// Exposed for UI/debug (WinMain). Keep external linkage.
unsigned int s_jitterFrameIndex = 0;
float s_lastJitterX = 0.0f;
float s_lastJitterY = 0.0f;

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
static D3D12_GPU_DESCRIPTOR_HANDLE s_reservoirGpuHandle[2];
static D3D12_GPU_DESCRIPTOR_HANDLE s_gi_reservoirGpuHandle[2];
static D3D12_GPU_DESCRIPTOR_HANDLE s_iblGpuHandle;

// Descriptor counts (tweak to support large models)
static const UINT DXR_HEAP_TEX_COUNT = 2048;        // max textures
static const UINT DXR_HEAP_VB_COUNT = 4096;         // vertex buffer SRVs
static const UINT DXR_HEAP_IB_COUNT = 4096;         // index buffer SRVs
static const UINT DXR_HEAP_TEX_OFFSET = 0;
static const UINT DXR_HEAP_VB_OFFSET = DXR_HEAP_TEX_OFFSET + DXR_HEAP_TEX_COUNT;
static const UINT DXR_HEAP_IB_OFFSET = DXR_HEAP_VB_OFFSET + DXR_HEAP_VB_COUNT;
static const UINT DXR_HEAP_UAV_OFFSET = DXR_HEAP_IB_OFFSET + DXR_HEAP_IB_COUNT;
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
static const UINT DXR_HEAP_NORMAL_ROUGHNESS_UAV_OFFSET = DXR_HEAP_UAV_OFFSET + 13;
static const UINT DXR_HEAP_DLSS_OUT_UAV_OFFSET = DXR_HEAP_UAV_OFFSET + 14;
static const UINT DXR_HEAP_IBL_OFFSET = DXR_HEAP_UAV_OFFSET + 15;
static const UINT DXR_HEAP_SPEC_ALBEDO_OFFSET = DXR_HEAP_UAV_OFFSET + 16;
static const UINT DXR_HEAP_SPEC_HITDIST_OFFSET = DXR_HEAP_UAV_OFFSET + 17;
static const UINT DXR_HEAP_SPEC_MVEC_OFFSET = DXR_HEAP_UAV_OFFSET + 18;
static const UINT DXR_HEAP_TOTAL_COUNT = DXR_HEAP_TEX_COUNT + DXR_HEAP_VB_COUNT + DXR_HEAP_IB_COUNT + 19;

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
static ComPtr<ID3D12Resource> s_tonemapOutputUAV;
static ComPtr<ID3D12Resource> s_specularAlbedoUAV;
static ComPtr<ID3D12Resource> s_specHitDistanceUAV;
static ComPtr<ID3D12Resource> s_specularMotionVectorsUAV;
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

static ComPtr<ID3D12Resource> s_lightBuffer;
static UINT s_lightCount = 0;
static std::vector<GpuLight> s_lastLightsCpu;
static ComPtr<ID3D12Resource> s_reservoirBuffers[2];
static ComPtr<ID3D12Resource> s_gi_reservoirBuffers[6];

namespace DxrRenderer {

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
  ThrowIfFailed(s_device->CreateDescriptorHeap(&heapDesc,
                                               IID_PPV_ARGS(&s_tonemapHeap)));

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

  if (g_verboseRenderLogs) {
    fprintf(stderr,
            "DxrRenderer: Creating Ray Tracing Pipeline (size=%u x %u)...\n",
            s_outputWidth, s_outputHeight);
  }

  // Create a large shader-visible heap for all DXR resources early,
  // so that BuildAccelerationStructures doesn't crash if shader compile fails.
  if (!s_srvHeap) {
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
    s_reservoirGpuHandle[0].ptr =
        gpuStart.ptr + (UINT64)DXR_HEAP_RESERVOIR_0_OFFSET * descSize;
    s_reservoirGpuHandle[1].ptr =
        gpuStart.ptr + (UINT64)DXR_HEAP_RESERVOIR_1_OFFSET * descSize;
    s_gi_reservoirGpuHandle[0].ptr =
        gpuStart.ptr + (UINT64)DXR_HEAP_GI_RESERVOIR_0_OFFSET_A * descSize;
    s_gi_reservoirGpuHandle[1].ptr =
        gpuStart.ptr + (UINT64)DXR_HEAP_GI_RESERVOIR_1_OFFSET_A * descSize;
    s_iblGpuHandle.ptr = gpuStart.ptr + (UINT64)DXR_HEAP_IBL_OFFSET * descSize;
  }

  // Compile shader
  ComPtr<IDxcBlob> shaderBlob;
  try {
    std::vector<std::wstring> compileDefines;
    if (::g_dxrDebugUV)
      compileDefines.push_back(L"RAYGEN_DEBUG=1");
    if (::g_dxrHitDebug)
      compileDefines.push_back(L"HIT_DEBUG=1");
    shaderBlob = s_dxcHelper.Compile(L"shaders/raytracing.hlsl", L"",
                                     L"lib_6_3", compileDefines);
  } catch (const std::exception &e) {
    fprintf(stderr, "DxrRenderer: Shader Compilation Failed: %s\n", e.what());
    return;
  }
  if (!shaderBlob) {
    fprintf(stderr, "DxrRenderer: shader blob null\n");
    return;
  }

  // Create global root signature
  D3D12_ROOT_PARAMETER params[10] = {}; // Increased for Lights
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  params[0].Descriptor.ShaderRegister = 0;
  params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  D3D12_DESCRIPTOR_RANGE uavRange = {};
  uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  uavRange.NumDescriptors = 20; // Increased from 16 to 20 to support u16, u17+
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

  // Environment Map Descriptor Table (t0, space1)
  static D3D12_DESCRIPTOR_RANGE envRange = {};
  envRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  envRange.NumDescriptors = 1;
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

  // Lights SB (t5000)
  params[9].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  params[9].Descriptor.ShaderRegister = 5000;
  params[9].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
  rootDesc.NumParameters = 10;
  rootDesc.pParameters = params;
  rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

  static D3D12_STATIC_SAMPLER_DESC staticSampler = {};
  staticSampler.Filter = D3D12_FILTER_ANISOTROPIC;
  staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  staticSampler.MipLODBias = 0;
  staticSampler.MaxAnisotropy = 16;
  staticSampler.ShaderRegister = 0;
  staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  rootDesc.NumStaticSamplers = 1;
  rootDesc.pStaticSamplers = &staticSampler;

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
  static D3D12_EXPORT_DESC exports[3] = {};
  static D3D12_HIT_GROUP_DESC hitGroupDesc = {};
  static D3D12_RAYTRACING_SHADER_CONFIG shaderConfig = {};
  static D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig = {};
  static D3D12_GLOBAL_ROOT_SIGNATURE globalRootSigDesc = {};

  libDesc.DXILLibrary.pShaderBytecode = shaderBlob->GetBufferPointer();
  libDesc.DXILLibrary.BytecodeLength = shaderBlob->GetBufferSize();
  exports[0].Name = L"RayGen";
  exports[1].Name = L"Miss";
  exports[2].Name = L"ClosestHit";
  libDesc.NumExports = 3;
  libDesc.pExports = exports;
  D3D12_STATE_SUBOBJECT libSub = {};
  libSub.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
  libSub.pDesc = &libDesc;

  hitGroupDesc.HitGroupExport = L"HitGroup";
  hitGroupDesc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
  hitGroupDesc.ClosestHitShaderImport = L"ClosestHit";
  D3D12_STATE_SUBOBJECT hitSub = {};
  hitSub.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
  hitSub.pDesc = &hitGroupDesc;

  fprintf(stderr, "DxrRenderer: MaxPayloadSizeInBytes=%u\n", 128);
  shaderConfig.MaxPayloadSizeInBytes = 128;
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
  s_dlssOutputUAV.Reset();
  s_tonemapOutputUAV.Reset();
  ThrowIfFailed(s_device->CreateCommittedResource(
      &heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
      IID_PPV_ARGS(&s_outputUAV)));
  if (s_outputUAV)
    s_outputUAV->SetName(L"RT Output Texture");

  D3D12_RESOURCE_DESC outDesc = texDesc;
  outDesc.Width = outW;
  outDesc.Height = outH;

  auto CreateUavTexture = [&](ComPtr<ID3D12Resource> &out,
                              const D3D12_RESOURCE_DESC &baseDesc,
                              DXGI_FORMAT format, const wchar_t *name) {
    D3D12_RESOURCE_DESC desc = baseDesc;
    desc.Format = format;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ThrowIfFailed(s_device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&out)));
    if (out)
      out->SetName(name);
  };

  // DLSS/Streamline inputs for DXR
  CreateUavTexture(s_depthUAV, texDesc, DXGI_FORMAT_R32_FLOAT, L"RT Depth");
  CreateUavTexture(s_mvecUAV, texDesc, DXGI_FORMAT_R16G16_FLOAT,
                   L"RT Motion Vectors");
  CreateUavTexture(s_albedoUAV, texDesc, DXGI_FORMAT_R16G16B16A16_FLOAT,
                   L"RT Albedo");
  CreateUavTexture(s_specularAlbedoUAV, texDesc, DXGI_FORMAT_R16G16B16A16_FLOAT,
                   L"RT Specular Albedo");
  CreateUavTexture(s_specHitDistanceUAV, texDesc, DXGI_FORMAT_R32_FLOAT,
                   L"RT Specular HitDistance");
  CreateUavTexture(s_specularMotionVectorsUAV, texDesc,
                   DXGI_FORMAT_R16G16_FLOAT, L"RT Specular MotionVectors");
  CreateUavTexture(s_normalRoughnessUAV, texDesc,
                   DXGI_FORMAT_R16G16B16A16_FLOAT, L"RT NormalRoughness");

  // DLSS output is output-size in linear HDR (pre-tonemap).
  CreateUavTexture(s_dlssOutputUAV, outDesc, DXGI_FORMAT_R16G16B16A16_FLOAT,
                   L"RT DLSS Output (HDR)");

  // Tonemap output is swapchain-format (output-size) for easy CopyResource.
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

  // Prepare tonemap pipeline resources.
  EnsureTonemapPipeline();

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

  if (g_verboseRenderLogs) {
    fprintf(stderr, "DxrRenderer: Ray Tracing Pipeline ready\n");
  }
}

void BuildAccelerationStructures(
    const std::vector<Asset::GpuMesh> &meshes,
    const std::vector<Scene::Instance> &instances) {
  if (g_debugLog) {
    std::ostringstream _oss; _oss << "DxrRenderer::BuildAccelerationStructures: ENTRY meshes=" << meshes.size() << " instances=" << instances.size() << "\n";
    fprintf(stderr, "%s", _oss.str().c_str()); fflush(stderr);
  }
  if (!g_rayTracingSupported || !s_dxrDevice)
    return;
  try {
    if (g_debugLog) {
      std::ostringstream _oss2; _oss2 << "DxrRenderer::BuildAccelerationStructures: after check - commandQueue=" << s_commandQueue << " fence=" << s_fence << " s_srvHeap=" << s_srvHeap.Get() << "\n";
      fprintf(stderr, "%s", _oss2.str().c_str()); fflush(stderr);
    }
    if (meshes.empty() || instances.empty()) {
      if (g_verboseRenderLogs)
        fprintf(stderr, "DxrRenderer: Empty scene - clearing TLAS\n");
      s_tlas.result = nullptr;
      s_allBLAS.clear();
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
    fprintf(stderr, "DxrRenderer::BuildAccelerationStructures: srvHeap OK. validating meshes...\n"); fflush(stderr);
    for (size_t i = 0; i < meshes.size(); ++i) {
      const auto &m = meshes[i];
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
        fprintf(stderr, "DxrRenderer: Descriptor heap overflow for mesh %zu (vbIndex=%zu ibIndex=%zu total=%u)\n", i, vbIndex, ibIndex, DXR_HEAP_TOTAL_COUNT);
        fflush(stderr);
        return;
      }

      D3D12_CPU_DESCRIPTOR_HANDLE vbCpu = s_srvHeap->GetCPUDescriptorHandleForHeapStart();
      vbCpu.ptr += (SIZE_T)vbIndex * descSize;
      D3D12_CPU_DESCRIPTOR_HANDLE ibCpu = s_srvHeap->GetCPUDescriptorHandleForHeapStart();
      ibCpu.ptr += (SIZE_T)ibIndex * descSize;

      if (g_debugLog) {
        fprintf(stderr, "DxrRenderer: Creating VB/IB SRV for mesh %llu (vb=%p ib=%p verts=%u idx=%u)\n", (unsigned long long)i, m.vertexBuffer.Get(), m.indexBuffer.Get(), m.vertexCount, m.indexCount);
        fflush(stderr);
      }

      // Extra defensive checks to avoid crashing the process when a malformed
      // mesh or descriptor calculation slips through earlier validation.
      if (!m.vertexBuffer.Get()) {
        fprintf(stderr, "DxrRenderer: Null vertex buffer for mesh %zu - aborting AS build\n", i); fflush(stderr);
        return;
      }
      if (!m.indexBuffer.Get()) {
        fprintf(stderr, "DxrRenderer: Null index buffer for mesh %zu - aborting AS build\n", i); fflush(stderr);
        return;
      }
      if (vbCpu.ptr == 0 || ibCpu.ptr == 0) {
        fprintf(stderr, "DxrRenderer: Computed empty CPU descriptor handle for mesh %zu (vbCpu=%llu ibCpu=%llu) - aborting\n", i, (unsigned long long)vbCpu.ptr, (unsigned long long)ibCpu.ptr); fflush(stderr);
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
      if (g_debugLog) { fprintf(stderr, "DxrRenderer: VB SRV created for mesh %zu\n", i); fflush(stderr); }

      D3D12_SHADER_RESOURCE_VIEW_DESC ibSrv = {};
      ibSrv.Format = DXGI_FORMAT_R32_UINT;
      ibSrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
      ibSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      ibSrv.Buffer.FirstElement = 0;
      ibSrv.Buffer.NumElements = m.indexCount;
      ibSrv.Buffer.StructureByteStride = 0; // Typed buffer
      ibSrv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
      s_device->CreateShaderResourceView(m.indexBuffer.Get(), &ibSrv, ibCpu);
      if (g_debugLog) { fprintf(stderr, "DxrRenderer: IB SRV created for mesh %zu\n", i); fflush(stderr); }
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
      WaitForSingleObject(s_fenceEvent, INFINITE);
    }

    fprintf(stderr, "DxrRenderer: Creating command allocator/list\n"); fflush(stderr);
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
    // Optimization: Only rebuild BLAS if the mesh resource has changed.
    // BLAS are defined in local space, so they don't need to rebuild when
    // models are transformed.
    static std::vector<ID3D12Resource *> s_cachedMeshBuffers;
    bool meshesChanged = (meshes.size() != s_cachedMeshBuffers.size());
    if (!meshesChanged) {
      for (size_t i = 0; i < meshes.size(); ++i) {
        if (meshes[i].vertexBuffer.Get() != s_cachedMeshBuffers[i]) {
          meshesChanged = true;
          break;
        }
      }
    }

    // Pipelining:
    // Create separate allocators for each batch so we can submit them without blocking/waiting on the CPU.
    // We only wait once at the very end.
    const size_t BLAS_BATCH_SIZE = 500;
    size_t batchCount = 0;

    std::vector<ComPtr<ID3D12CommandAllocator>> pendingAllocators;
    pendingAllocators.push_back(cmdAlloc); // The initial one

    if (meshesChanged || s_allBLAS.empty()) {
      s_allBLAS.clear();
      s_cachedMeshBuffers.clear();
      try {
        for (size_t i = 0; i < meshes.size(); ++i) {
          const auto &mesh = meshes[i];
          if (!mesh.vertexBuffer || !mesh.indexBuffer) continue;
          
          auto vbAddr = mesh.vertexBuffer->GetGPUVirtualAddress();
          auto ibAddr = mesh.indexBuffer->GetGPUVirtualAddress();
          
          auto bl = BuildBLAS(s_dxrDevice.Get(), cmdList.Get(), vbAddr,
                              mesh.vertexCount, sizeof(Asset::Vertex), ibAddr,
                              mesh.indexCount);
          if (bl.result && bl.scratch) {
              s_allBLAS.push_back({bl, (UINT64)i});
              s_cachedMeshBuffers.push_back(mesh.vertexBuffer.Get());
          }

          batchCount++;
          if (batchCount >= BLAS_BATCH_SIZE) {
              ThrowIfFailed(cmdList->Close());
              ID3D12CommandList* lists[] = { cmdList.Get() };
              s_commandQueue->ExecuteCommandLists(1, lists);
              
              // DO NOT WAIT. Create new allocator and continue recording.
              ComPtr<ID3D12CommandAllocator> nextAlloc;
              ThrowIfFailed(s_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&nextAlloc)));
              pendingAllocators.push_back(nextAlloc);
              
              // Reset same list with new allocator
              ThrowIfFailed(cmdList->Reset(nextAlloc.Get(), nullptr));
              batchCount = 0;
          }
        }
        
        // Final flush if any remaining (and ensure list is closed regardless)
        ThrowIfFailed(cmdList->Close());
        ID3D12CommandList* lists[] = { cmdList.Get() };
        s_commandQueue->ExecuteCommandLists(1, lists);

        // NOW we wait for everything to finish (Single Wait)
        const UINT64 fenceVal = s_fenceValues[*s_frameIndexPtr];
        s_commandQueue->Signal(s_fence, fenceVal);
        s_fenceValues[*s_frameIndexPtr]++;
        if (s_fence->GetCompletedValue() < fenceVal) {
            s_fence->SetEventOnCompletion(fenceVal, s_fenceEvent);
            WaitForSingleObject(s_fenceEvent, INFINITE);
        }
        
        // Safe to release all scratch buffers now
        for(size_t k=0; k < s_allBLAS.size(); ++k) {
            s_allBLAS[k].buffers.scratch.Reset();
        }
        
        // Restore a fresh allocator/list for TLAS build (reuse the last one created)
        pendingAllocators.clear(); 
        ThrowIfFailed(cmdAlloc->Reset()); // Reuse the original handle for scope
        ThrowIfFailed(cmdList->Reset(cmdAlloc.Get(), nullptr));

      } catch (...) {
        fprintf(stderr, "DxrRenderer: BLAS Build crashed\n");
        return;
      }
      printf("DxrRenderer: BLAS creation completed. Total BLAS count: %zu\n", s_allBLAS.size());
    }

    if (s_allBLAS.empty()) {
      fprintf(stderr, "DxrRenderer: No BLAS built - aborting TLAS build\n");
      return;
    }

    // TLAS
    
    // Optimization: Pre-compute map from VertexBuffer -> BLAS Index
    std::unordered_map<ID3D12Resource*, size_t> meshToBlasIndex;
    meshToBlasIndex.reserve(s_allBLAS.size());
    for (size_t k = 0; k < s_allBLAS.size(); ++k) {
        // s_allBLAS[k].meshId stores originalMeshIndex
        size_t origIdx = (size_t)s_allBLAS[k].meshId;
        if (origIdx < meshes.size() && meshes[origIdx].vertexBuffer) {
            meshToBlasIndex[meshes[origIdx].vertexBuffer.Get()] = k;
        }
    }

    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs;
    instanceDescs.reserve(instances.size());

    for (const auto &sceneInst : instances) {
      if (!sceneInst.mesh.vertexBuffer) continue;

      auto it = meshToBlasIndex.find(sceneInst.mesh.vertexBuffer.Get());
      if (it == meshToBlasIndex.end()) continue;

      size_t blasIndex = it->second;
      UINT originalMeshIdx = (UINT)s_allBLAS[blasIndex].meshId;

      D3D12_RAYTRACING_INSTANCE_DESC inst = {};
      // Convert Column-Major 4x4 to Row-Major 3x4
      // D3D12_RAYTRACING_INSTANCE_DESC expects 3x4 Row-Major
      const float *m = sceneInst.transform;
      inst.Transform[0][0] = m[0];
      inst.Transform[0][1] = m[4];
      inst.Transform[0][2] = m[8];
      inst.Transform[0][3] = m[12];
      inst.Transform[1][0] = m[1];
      inst.Transform[1][1] = m[5];
      inst.Transform[1][2] = m[9];
      inst.Transform[1][3] = m[13];
      inst.Transform[2][0] = m[2];
      inst.Transform[2][1] = m[6];
      inst.Transform[2][2] = m[10];
      inst.Transform[2][3] = m[14];

      inst.InstanceID = originalMeshIdx; // Use mesh index for shader binding
      inst.InstanceMask = 0xFF;
      inst.InstanceContributionToHitGroupIndex = 0;
      inst.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
      inst.AccelerationStructure =
          s_allBLAS[blasIndex].buffers.result->GetGPUVirtualAddress();
      instanceDescs.push_back(inst);
    }

    ComPtr<ID3D12Resource> instanceDescBuffer;
    AllocateUploadBuffer(s_device, instanceDescs.data(),
                         instanceDescs.size() *
                             sizeof(D3D12_RAYTRACING_INSTANCE_DESC),
                         &instanceDescBuffer, L"TLAS Instance Buffer");

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.Flags =
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    inputs.NumDescs = (UINT)instanceDescs.size();
    inputs.InstanceDescs = instanceDescBuffer->GetGPUVirtualAddress();

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
    s_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);
    s_tlas.scratchSizeInBytes =
        Align(info.ScratchDataSizeInBytes,
              D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
    s_tlas.resultSizeInBytes =
        Align(info.ResultDataMaxSizeInBytes,
              D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
    AllocateUAVBuffer(s_device, s_tlas.scratchSizeInBytes, &s_tlas.scratch,
                      D3D12_RESOURCE_STATE_COMMON, L"TLAS Scratch");
    AllocateUAVBuffer(s_device, s_tlas.resultSizeInBytes, &s_tlas.result,
                      D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                      L"TLAS Result");

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
    buildDesc.Inputs = inputs;
    buildDesc.DestAccelerationStructureData =
        s_tlas.result->GetGPUVirtualAddress();
    buildDesc.ScratchAccelerationStructureData =
        s_tlas.scratch->GetGPUVirtualAddress();
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
    if (s_fence->GetCompletedValue() < fence2) {
      s_fence->SetEventOnCompletion(fence2, s_fenceEvent);
      WaitForSingleObject(s_fenceEvent, INFINITE);
    }
    if (g_verboseRenderLogs) {
      fprintf(stderr, "DxrRenderer: Acceleration structures built\n");
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

void UpdateLights(const std::vector<GpuLight> &lights) {
  if (lights.empty()) {
    if (s_lightCount != 0) {
      s_lightCount = 0;
      s_lastLightsCpu.clear();
      ResetAccumulation();
    }
    return;
  }

  // Avoid resetting accumulation / Streamline history when lights didn't change.
  if (lights.size() == s_lastLightsCpu.size()) {
    const size_t byteSize = lights.size() * sizeof(GpuLight);
    if (byteSize > 0 &&
        memcmp(lights.data(), s_lastLightsCpu.data(), byteSize) == 0) {
      // Keep s_lightCount accurate (and allow the caller to still call UpdateLights every frame).
      s_lightCount = (UINT)lights.size();
      return;
    }
  }

  s_lightCount = (UINT)lights.size();
  UINT bufferSize = (UINT)(lights.size() * sizeof(GpuLight));

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
  s_hasTonemappedFrame = false;
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

UINT GetAccumulationFrameCount() { return s_accumulation.GetFrameCount(); }

UINT GetLightCount() { return s_lightCount; }

// RR jitter scale accessors
void SetRrJitterScale(float scale) {
  if (scale < 0.0f) scale = 0.0f;
  if (scale > 1.0f) scale = 1.0f;
  s_rrJitterScale = scale;
}

float GetRrJitterScale() { return s_rrJitterScale; }

bool IsReady() {
  return g_rayTracingSupported && s_rtStateObject != nullptr &&
         s_tlas.result != nullptr;
}

bool RenderFrame(ID3D12GraphicsCommandList *commandListBase, UINT frameIndex,
                 ID3D12Resource *renderTarget,
                 D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle,
                 ID3D12Resource *cameraCB, ID3D12Resource *materialCB,
                 D3D12_GPU_DESCRIPTOR_HANDLE texturesGpuStart,
                 UINT textureDescriptorCount,
                 const std::vector<Asset::GpuMesh> &meshes,
                 ID3D12Resource *meshDataSB) {
  if (!g_rayTracingSupported || !s_rtStateObject || !s_srvHeap)
    return false;
  if (!renderTarget)
    return false;

  // Handle empty scene or missing TLAS gracefully
  if (meshes.empty() || !s_tlas.result) {
    TransitionResource(commandListBase, renderTarget,
                       D3D12_RESOURCE_STATE_PRESENT,
                       D3D12_RESOURCE_STATE_RENDER_TARGET);
    FLOAT clearColor[] = {0.1f, 0.1f, 0.12f, 1.0f};
    commandListBase->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    commandListBase->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
    return true;
  }

  if (!s_outputUAV)
    return false;

  ComPtr<ID3D12GraphicsCommandList4> dxrList;
  if (FAILED(commandListBase->QueryInterface(IID_PPV_ARGS(&dxrList))))
    return false;

  const bool dlssActive =
      (s_streamline && s_streamline->IsInitialized() &&
       s_streamline->IsDeviceSet() && s_streamline->IsEnabled() &&
       s_streamline->GetMode() != StreamlineManager::Mode::Off);
  const bool rrActive =
      dlssActive &&
      (s_streamline->GetMode() == StreamlineManager::Mode::DLSS_RayReconstruction);

  // If we've hit maxSPP and the camera/settings haven't changed (meaning
  // ResetAccumulation hasn't been called), freeze rendering and keep presenting
  // the last tonemapped output. This works for both accumulation and DLSS-RR.
  const UINT maxSpp = (g_cameraData.maxSPP > 0.0f) ? (UINT)g_cameraData.maxSPP : 0u;
  const UINT currSpp = rrActive ? s_rrStillFrameSpp : s_accumulation.GetFrameCount();
  if (maxSpp > 0 && currSpp >= maxSpp) {
    ID3D12Resource *freezeSrc = nullptr;
    if (s_tonemapOutputUAV) {
      freezeSrc = s_tonemapOutputUAV.Get();
    } else {
      // If tonemap output isn't available (e.g., swapchain is HDR), fall back to
      // copying the main output directly if formats match.
      const DXGI_FORMAT dstFmt = renderTarget->GetDesc().Format;
      if (s_outputUAV && s_outputUAV->GetDesc().Format == dstFmt) {
        freezeSrc = s_outputUAV.Get();
      }
    }

    if (freezeSrc) {
      TransitionResource(dxrList.Get(), freezeSrc,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                         D3D12_RESOURCE_STATE_COPY_SOURCE);
    TransitionResource(dxrList.Get(), renderTarget, D3D12_RESOURCE_STATE_PRESENT,
                       D3D12_RESOURCE_STATE_COPY_DEST);
      dxrList->CopyResource(renderTarget, freezeSrc);

      s_hasTonemappedFrame = true;
    TransitionResource(dxrList.Get(), renderTarget,
                       D3D12_RESOURCE_STATE_COPY_DEST,
                       D3D12_RESOURCE_STATE_RENDER_TARGET);
      TransitionResource(dxrList.Get(), freezeSrc,
                         D3D12_RESOURCE_STATE_COPY_SOURCE,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    commandListBase->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
    return true;
    }
  }

  // Set pipeline and root signature
  dxrList->SetPipelineState1(s_rtStateObject.Get());
  dxrList->SetComputeRootSignature(s_rtGlobalRootSignature.Get());

  // Bind TLAS
  dxrList->SetComputeRootShaderResourceView(
      0, s_tlas.result->GetGPUVirtualAddress());

  // Copy global texture descriptors to our local DXR heap IF they've changed or
  // every frame (simple)
  if (g_cbvSrvAllocator.Heap() && textureDescriptorCount > 0) {
    UINT descSize = s_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE srcStart =
        g_cbvSrvAllocator.Heap()->GetCPUDescriptorHandleForHeapStart();
    // find offset of textures in global heap
    D3D12_GPU_DESCRIPTOR_HANDLE globalGpuStart =
        g_cbvSrvAllocator.Heap()->GetGPUDescriptorHandleForHeapStart();
    UINT texOffset =
        (UINT)((texturesGpuStart.ptr - globalGpuStart.ptr) / descSize);
    srcStart.ptr += (SIZE_T)texOffset * descSize;

    D3D12_CPU_DESCRIPTOR_HANDLE dstStart =
        s_srvHeap->GetCPUDescriptorHandleForHeapStart();
    dstStart.ptr += (SIZE_T)DXR_HEAP_TEX_OFFSET * descSize;

    // Only copy what fits
    UINT count =
        (textureDescriptorCount < 2048) ? textureDescriptorCount : 2048;
    s_device->CopyDescriptorsSimple(count, dstStart, srcStart,
                                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  }

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

  // Update frame count in camera CB if present
  if (cameraCB) {
    void *pData = nullptr;
    D3D12_RANGE readRange = {0, 0};
    if (SUCCEEDED(cameraCB->Map(0, &readRange, &pData))) {
      float *pfData = (float *)pData;
      // Index 7 = jitterX (_pad1)
      pfData[7] = jitterX;
      // Index 11 = jitterY (_pad2)
      pfData[11] = jitterY;

      // Index 17: globalFrameCount (monotonic, for RNG)
      pfData[17] = (float)s_jitterFrameIndex;
      pfData[18] = (float)s_lightCount; // lightCount

        // Index 23: accumulationCount.
        // DLSS-RR is a temporal denoiser; don't also accumulate history.
        pfData[23] = rrActive ? 0.0f : (float)s_accumulation.GetFrameCount();

        // Streamline flags used by shaders/raytracing/common.hlsli.
        // Index 43: dlssEnabled
        // Index 47: dlssRayReconstruction
        pfData[43] = dlssActive ? 1.0f : 0.0f;
        pfData[47] = rrActive ? 1.0f : 0.0f;

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

  if (IBLManager::Get().IsLoaded()) {
    // Copy IBL descriptor from global heap to DXR local heap
    D3D12_CPU_DESCRIPTOR_HANDLE dst =
        s_srvHeap->GetCPUDescriptorHandleForHeapStart();
    dst.ptr += (SIZE_T)DXR_HEAP_IBL_OFFSET *
               s_device->GetDescriptorHandleIncrementSize(
                   D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    s_device->CopyDescriptorsSimple(1, dst, IBLManager::Get().GetCPUHandle(),
                                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

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

  D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
  // RayGen record size must match the raygen slot size (may be 64-aligned)
  UINT64 s_rayGenEntrySize = Align(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES,
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

  dxrList->DispatchRays(&dispatchDesc);

  // Increment accumulation history only when actually used.
  if (!rrActive)
    s_accumulation.IncrementFrame();
  else
    s_rrStillFrameSpp++;

  // Optional Streamline / DLSS evaluation
  ID3D12Resource *postColor = s_outputUAV.Get();
  bool usedDlss = false;
  const D3D12_RESOURCE_DESC dstDesc = renderTarget->GetDesc();
  const uint32_t outW = (uint32_t)dstDesc.Width;
  const uint32_t outH = (uint32_t)dstDesc.Height;

  // If a shader debug view is active, do not run DLSS/DLSS-RR.
  // DLSS is temporal and will "process" the debug visualization itself,
  // which can look like shimmer even when the underlying buffer is stable.
  const bool debugViewActive = (g_cameraData.debugMode != 0.0f);

  if (!debugViewActive && s_streamline && s_streamline->IsInitialized() &&
      s_streamline->IsDeviceSet() && s_streamline->IsEnabled() &&
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

    // Back to UAV for next frame dispatch
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

  // Tonemap linear HDR to swapchain format, then copy.
  EnsureTonemapPipeline();

  if (s_tonemapPSO && s_tonemapRootSig && s_tonemapHeap && s_tonemapCB &&
      s_tonemapOutputUAV) {
    // Update constants
    TonemapConstants tc{};
    tc.outWidth = outW;
    tc.outHeight = outH;
    tc.exposure = 1.0f;

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
    s_device->CreateUnorderedAccessView(s_tonemapOutputUAV.Get(), nullptr,
                                        &uav, uavCpu);

    // Barriers
    TransitionResource(dxrList.Get(), postColor,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    ID3D12DescriptorHeap *tmHeaps[] = {s_tonemapHeap.Get()};
    dxrList->SetDescriptorHeaps(1, tmHeaps);
    dxrList->SetPipelineState(s_tonemapPSO.Get());
    dxrList->SetComputeRootSignature(s_tonemapRootSig.Get());
    dxrList->SetComputeRootConstantBufferView(0,
                                              s_tonemapCB->GetGPUVirtualAddress());

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
    TransitionResource(dxrList.Get(), renderTarget, D3D12_RESOURCE_STATE_PRESENT,
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
  } else {
    // Fallback: copy (may be invalid if formats don't match)
    TransitionResource(dxrList.Get(), renderTarget, D3D12_RESOURCE_STATE_PRESENT,
                       D3D12_RESOURCE_STATE_COPY_DEST);
    TransitionResource(dxrList.Get(), postColor, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                       D3D12_RESOURCE_STATE_COPY_SOURCE);
    dxrList->CopyResource(renderTarget, postColor);
    TransitionResource(dxrList.Get(), renderTarget,
                       D3D12_RESOURCE_STATE_COPY_DEST,
                       D3D12_RESOURCE_STATE_RENDER_TARGET);
    TransitionResource(dxrList.Get(), postColor, D3D12_RESOURCE_STATE_COPY_SOURCE,
                       D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    // Consider the frame presented even if tonemap is missing (debug/dev).
    s_hasTonemappedFrame = true;
  }

  // Bind RTV for subsequent ImGui draws
  commandListBase->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
  return true;
}

} // namespace DxrRenderer
