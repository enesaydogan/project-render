#include "dxr_renderer.h"
#include "d3d12_helpers.h"
#include "dxc_wrapper.h"
#include "dxr_accumulation.h"
#include "dxr_helpers.h"
#include "ibl_manager.h"
#include "scene.h"
#include "camera.h"
#include "streamline_manager.h"
#include <cstdio>
#include <vector>
#include <wrl.h>

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

static StreamlineManager* s_streamline = nullptr;
static bool s_streamlineResetHistory = true;
// Configurable DLSS-RR evaluation frequency (frames/Sample-Per-Pixel). Default 10 SPP.
static unsigned s_dlssEvalSpp = 10u;
static UINT s_jitterFrameIndex = 0;

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

// Offset constants for s_srvHeap
static const UINT DXR_HEAP_TEX_OFFSET = 0;
static const UINT DXR_HEAP_VB_OFFSET = 2048;
static const UINT DXR_HEAP_IB_OFFSET = 2048 + 1024;
static const UINT DXR_HEAP_UAV_OFFSET = 2048 + 1024 + 1024;
static const UINT DXR_HEAP_ACCUM_UAV_OFFSET = 2048 + 1024 + 1024 + 1;
static const UINT DXR_HEAP_RESERVOIR_0_OFFSET = 2048 + 1024 + 1024 + 2;
static const UINT DXR_HEAP_RESERVOIR_1_OFFSET = 2048 + 1024 + 1024 + 3;
static const UINT DXR_HEAP_GI_RESERVOIR_0_OFFSET_A = 2048 + 1024 + 1024 + 4;
static const UINT DXR_HEAP_GI_RESERVOIR_0_OFFSET_B = 2048 + 1024 + 1024 + 5;
static const UINT DXR_HEAP_GI_RESERVOIR_0_OFFSET_C = 2048 + 1024 + 1024 + 6;
static const UINT DXR_HEAP_GI_RESERVOIR_1_OFFSET_A = 2048 + 1024 + 1024 + 7;
static const UINT DXR_HEAP_GI_RESERVOIR_1_OFFSET_B = 2048 + 1024 + 1024 + 8;
static const UINT DXR_HEAP_GI_RESERVOIR_1_OFFSET_C = 2048 + 1024 + 1024 + 9;
// Extra UAVs (u10+) reserved for Streamline/DLSS inputs/outputs
static const UINT DXR_HEAP_DEPTH_UAV_OFFSET = 2048 + 1024 + 1024 + 10;
static const UINT DXR_HEAP_MVEC_UAV_OFFSET = 2048 + 1024 + 1024 + 11;
static const UINT DXR_HEAP_ALBEDO_UAV_OFFSET = 2048 + 1024 + 1024 + 12;
static const UINT DXR_HEAP_NORMAL_ROUGHNESS_UAV_OFFSET =
  2048 + 1024 + 1024 + 13;
static const UINT DXR_HEAP_DLSS_OUT_UAV_OFFSET = 2048 + 1024 + 1024 + 14;
static const UINT DXR_HEAP_IBL_OFFSET = 2048 + 1024 + 1024 + 15;
static const UINT DXR_HEAP_SPEC_ALBEDO_OFFSET = 2048 + 1024 + 1024 + 16;
static const UINT DXR_HEAP_SPEC_HITDIST_OFFSET = 2048 + 1024 + 1024 + 17;
static const UINT DXR_HEAP_TOTAL_COUNT = 2048 + 1024 + 1024 + 18;

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
static ComPtr<ID3D12Resource> s_specularAlbedoUAV;
static ComPtr<ID3D12Resource> s_specHitDistanceUAV;
static UINT s_outputUAVDescriptorSize = 0;
static D3D12_GPU_DESCRIPTOR_HANDLE s_outputUAVGpuHandle = {0};
static ComPtr<ID3D12DescriptorHeap>
    s_uavHeap; // fallback heap when global heap not available
static ComPtr<ID3D12DescriptorHeap>
    s_mergedHeap; // merged heap that contains scene SRVs then output UAV
                  // (preferred)

struct ShaderTableEntry {
  void *id;
};
static UINT s_shaderTableEntrySize = 0;
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
static ComPtr<ID3D12Resource> s_reservoirBuffers[2];
static ComPtr<ID3D12Resource> s_gi_reservoirBuffers[6];

namespace DxrRenderer {

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
  if (s_streamline && s_streamline->IsInitialized() && s_streamline->IsDeviceSet() &&
      s_streamline->IsEnabled() &&
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
  uavRange.NumDescriptors = 16;
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
  UINT s_rayGenEntrySize =
      Align(shaderIdentifierSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
  s_shaderTableEntrySize = Align(shaderIdentifierSize,
                                 D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
  // Total SBT size: raygen + miss + hit (each 1 entry for now)
  UINT shaderTableSize =
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
  texDesc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
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
  s_normalRoughnessUAV.Reset();
  s_dlssOutputUAV.Reset();
  ThrowIfFailed(s_device->CreateCommittedResource(
      &heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
      IID_PPV_ARGS(&s_outputUAV)));
  if (s_outputUAV)
    s_outputUAV->SetName(L"RT Output Texture");

  D3D12_RESOURCE_DESC outDesc = texDesc;
  outDesc.Width = outW;
  outDesc.Height = outH;

  auto CreateUavTexture = [&](ComPtr<ID3D12Resource>& out,
                              const D3D12_RESOURCE_DESC& baseDesc,
                              DXGI_FORMAT format,
                              const wchar_t* name) {
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
  CreateUavTexture(s_normalRoughnessUAV, texDesc, DXGI_FORMAT_R16G16B16A16_FLOAT,
                   L"RT NormalRoughness");

  // DLSS output is output-size (same format as swapchain for easy copy)
  CreateUavTexture(s_dlssOutputUAV, outDesc, DXGI_FORMAT_R10G10B10A2_UNORM,
                   L"RT DLSS Output");

  D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
  uavDesc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
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

    auto CreateUavAt = [&](ID3D12Resource* res, DXGI_FORMAT fmt,
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
    CreateUavAt(s_depthUAV.Get(), DXGI_FORMAT_R32_FLOAT, DXR_HEAP_DEPTH_UAV_OFFSET);
    CreateUavAt(s_mvecUAV.Get(), DXGI_FORMAT_R16G16_FLOAT, DXR_HEAP_MVEC_UAV_OFFSET);
    CreateUavAt(s_specularAlbedoUAV.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT,
          DXR_HEAP_SPEC_ALBEDO_OFFSET);
    CreateUavAt(s_specHitDistanceUAV.Get(), DXGI_FORMAT_R32_FLOAT,
          DXR_HEAP_SPEC_HITDIST_OFFSET);
    CreateUavAt(s_albedoUAV.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT,
          DXR_HEAP_ALBEDO_UAV_OFFSET);
    CreateUavAt(s_normalRoughnessUAV.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT,
          DXR_HEAP_NORMAL_ROUGHNESS_UAV_OFFSET);
    CreateUavAt(s_dlssOutputUAV.Get(), DXGI_FORMAT_R10G10B10A2_UNORM,
          DXR_HEAP_DLSS_OUT_UAV_OFFSET);

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

  // Create GI Reservoir UAVs (3 per frame for ping-ponging, 2 frames total = 6 textures)
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
    switch(i) {
        case 0: offset = DXR_HEAP_GI_RESERVOIR_0_OFFSET_A; break;
        case 1: offset = DXR_HEAP_GI_RESERVOIR_0_OFFSET_B; break;
        case 2: offset = DXR_HEAP_GI_RESERVOIR_0_OFFSET_C; break;
        case 3: offset = DXR_HEAP_GI_RESERVOIR_1_OFFSET_A; break;
        case 4: offset = DXR_HEAP_GI_RESERVOIR_1_OFFSET_B; break;
        case 5: offset = DXR_HEAP_GI_RESERVOIR_1_OFFSET_C; break;
    }

    resUavCpu.ptr += (SIZE_T)offset *
                     s_device->GetDescriptorHandleIncrementSize(
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
  if (!g_rayTracingSupported || !s_dxrDevice)
    return;
  try {
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
      D3D12_CPU_DESCRIPTOR_HANDLE vbCpu =
          s_srvHeap->GetCPUDescriptorHandleForHeapStart();
      vbCpu.ptr += (SIZE_T)(DXR_HEAP_VB_OFFSET + i) * descSize;
      D3D12_CPU_DESCRIPTOR_HANDLE ibCpu =
          s_srvHeap->GetCPUDescriptorHandleForHeapStart();
      ibCpu.ptr += (SIZE_T)(DXR_HEAP_IB_OFFSET + i) * descSize;

      D3D12_SHADER_RESOURCE_VIEW_DESC vbSrv = {};
      vbSrv.Format = DXGI_FORMAT_UNKNOWN;
      vbSrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
      vbSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      vbSrv.Buffer.FirstElement = 0;
      vbSrv.Buffer.NumElements = m.vertexCount;
      vbSrv.Buffer.StructureByteStride = sizeof(Asset::Vertex);
      vbSrv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
      s_device->CreateShaderResourceView(m.vertexBuffer.Get(), &vbSrv, vbCpu);

      D3D12_SHADER_RESOURCE_VIEW_DESC ibSrv = {};
      ibSrv.Format = DXGI_FORMAT_R32_UINT;
      ibSrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
      ibSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      ibSrv.Buffer.FirstElement = 0;
      ibSrv.Buffer.NumElements = m.indexCount;
      ibSrv.Buffer.StructureByteStride = 0; // Typed buffer
      ibSrv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
      s_device->CreateShaderResourceView(m.indexBuffer.Get(), &ibSrv, ibCpu);
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

    if (meshesChanged || s_allBLAS.empty()) {
      s_allBLAS.clear();
      s_cachedMeshBuffers.clear();
      try {
        for (size_t i = 0; i < meshes.size(); ++i) {
          const auto &mesh = meshes[i];
          // Protect against invalid GPU virtual addresses
          if (!mesh.vertexBuffer || !mesh.indexBuffer) {
            fprintf(stderr,
                    "DxrRenderer: Skipping mesh %zu because buffers are null\n",
                    i);
            continue;
          }
          auto vbAddr = mesh.vertexBuffer->GetGPUVirtualAddress();
          auto ibAddr = mesh.indexBuffer->GetGPUVirtualAddress();
          if (vbAddr == 0 || ibAddr == 0) {
            fprintf(stderr,
                    "DxrRenderer: Mesh %zu has invalid GPU addresses "
                    "(vb=0x%016llx ib=0x%016llx) - aborting\n",
                    i, (unsigned long long)vbAddr, (unsigned long long)ibAddr);
            return;
          }
          auto bl = BuildBLAS(s_dxrDevice.Get(), cmdList.Get(), vbAddr,
                              mesh.vertexCount, sizeof(Asset::Vertex), ibAddr,
                              mesh.indexCount);
          // Basic validation
          if (!bl.result || !bl.scratch) {
            fprintf(stderr,
                    "DxrRenderer: BuildBLAS produced invalid buffers for mesh "
                    "%zu\n",
                    i);
            return;
          }
          s_allBLAS.push_back({bl, (UINT64)i});
          s_cachedMeshBuffers.push_back(mesh.vertexBuffer.Get());
        }
      } catch (...) {
        fprintf(stderr, "DxrRenderer: BLAS Build crashed\n");
        return;
      }
    }

    if (s_allBLAS.empty()) {
      fprintf(stderr, "DxrRenderer: No BLAS built - aborting TLAS build\n");
      return;
    }

    // TLAS
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs;
    for (const auto &sceneInst : instances) {
      // Find which BLAS corresponds to this mesh
      size_t meshIdx = (size_t)-1;
      for (size_t i = 0; i < meshes.size(); ++i) {
        if (meshes[i].vertexBuffer == sceneInst.mesh.vertexBuffer) {
          meshIdx = i;
          break;
        }
      }
      if (meshIdx == (size_t)-1)
        continue;

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

      inst.InstanceID = (UINT)meshIdx; // Use mesh index for shader binding
      inst.InstanceMask = 0xFF;
      inst.InstanceContributionToHitGroupIndex = 0;
      inst.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
      inst.AccelerationStructure =
          s_allBLAS[meshIdx].buffers.result->GetGPUVirtualAddress();
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
    s_lightCount = 0;
    return;
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

  ResetAccumulation();
}

void ResetAccumulation() {
  s_accumulation.Reset();
  // Keep Streamline history reset separate from accumulation decisions.
  // Accumulation resets happen on real camera/settings changes; per-frame jitter
  // changes must not trigger this.
  s_streamlineResetHistory = true;
  if (g_verboseRenderLogs) {
    fprintf(stderr, "DxrRenderer: Accumulation Reset\n");
  }
}

void SetStreamlineManager(StreamlineManager* streamline) {
  s_streamline = streamline;
  s_streamlineResetHistory = true;
}

void ResetStreamlineHistory() { s_streamlineResetHistory = true; }

UINT GetAccumulationFrameCount() { return s_accumulation.GetFrameCount(); }

UINT GetLightCount() { return s_lightCount; }

// DLSS-RR SPP configuration accessors
void SetDlssEvalSpp(unsigned spp) { s_dlssEvalSpp = spp < 1 ? 1u : spp; }
unsigned GetDlssEvalSpp() { return s_dlssEvalSpp; }

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

  // Compute jitter for this frame (DLSS-RR compatible)
  // Note: DLSS expects jitter in range [-0.5, 0.5] pixel space
  s_jitterFrameIndex++;
  uint32_t frameIdx = s_jitterFrameIndex;
  float jitterX = Halton(frameIdx, 2) - 0.5f;
  float jitterY = Halton(frameIdx, 3) - 0.5f;

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
      // If DLSS-RR is on, force 0 (effectively disabling accumulation) so we feed raw frames.
      bool useRawForDlss = s_streamline && s_streamline->IsEnabled() && 
        (s_streamline->GetMode() == StreamlineManager::Mode::DLSS_RayReconstruction);
      
      pfData[23] = useRawForDlss ? 0.0f : (float)s_accumulation.GetFrameCount();
      
      // Index 43: dlssEnabled (mapped from _padPrev0).
      // Used by shader to decide whether to ToneMap (if disabled) or output Linear (if enabled).
      bool anyDlss = s_streamline && s_streamline->IsEnabled() && 
         (s_streamline->GetMode() != StreamlineManager::Mode::Off);
      pfData[43] = anyDlss ? 1.0f : 0.0f;

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

  // SPP cap: if a max SPP is set and we've already reached it, skip ray dispatch
  // and reuse the last output. This prevents the frame count from increasing past
  // maxSPP and stops additional GPU work.
  if (g_cameraData.maxSPP > 0.0f && s_accumulation.GetFrameCount() >= (UINT)g_cameraData.maxSPP) {
      // Copy the current output texture to the render target and return
      TransitionResource(dxrList.Get(), s_outputUAV.Get(),
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                         D3D12_RESOURCE_STATE_COPY_SOURCE);
      TransitionResource(dxrList.Get(), renderTarget, D3D12_RESOURCE_STATE_PRESENT,
                         D3D12_RESOURCE_STATE_COPY_DEST);

      D3D12_RESOURCE_DESC srcDesc = s_outputUAV->GetDesc();
      D3D12_RESOURCE_DESC dstDesc = renderTarget->GetDesc();
      if (srcDesc.Width != dstDesc.Width || srcDesc.Height != dstDesc.Height) {
          UINT copyW = (UINT)min(srcDesc.Width, dstDesc.Width);
          UINT copyH = (UINT)min(srcDesc.Height, dstDesc.Height);
          D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
          dstLoc.pResource = renderTarget;
          dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
          dstLoc.SubresourceIndex = 0;

          D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
          srcLoc.pResource = s_outputUAV.Get();
          srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
          srcLoc.SubresourceIndex = 0;

          D3D12_BOX srcBox = {};
          srcBox.left = 0; srcBox.top = 0; srcBox.front = 0;
          srcBox.right = copyW; srcBox.bottom = copyH; srcBox.back = 1;

          dxrList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, &srcBox);
      } else {
          dxrList->CopyResource(renderTarget, s_outputUAV.Get());
      }

      // Transition back
      TransitionResource(dxrList.Get(), renderTarget,
                         D3D12_RESOURCE_STATE_COPY_DEST,
                         D3D12_RESOURCE_STATE_RENDER_TARGET);
      TransitionResource(dxrList.Get(), s_outputUAV.Get(),
                         D3D12_RESOURCE_STATE_COPY_SOURCE,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

      // Bind RTV for subsequent ImGui draws
      commandListBase->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
      return true;
  }

  D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
  // RayGen record size must match the raygen slot size (may be 64-aligned)
  UINT s_rayGenEntrySize = Align(D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES,
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

  // Increment accumulation frame (DLSS must not affect this)
  s_accumulation.IncrementFrame();

  // Optional Streamline / DLSS evaluation
  ID3D12Resource* postColor = s_outputUAV.Get();
  bool usedDlss = false;
  const D3D12_RESOURCE_DESC dstDesc = renderTarget->GetDesc();
  const uint32_t outW = (uint32_t)dstDesc.Width;
  const uint32_t outH = (uint32_t)dstDesc.Height;

  if (s_streamline && s_streamline->IsInitialized() && s_streamline->IsDeviceSet() &&
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

    const bool resetHistory = s_streamlineResetHistory;
    s_streamlineResetHistory = false;

    // User Request: Eval DLSS-D every N spp. ✅
    // Evaluate DLSS-RR less frequently so the denoiser receives a higher-quality
    // frame (we reuse the previous DLSS output on intervening frames).
    bool allowEval = true;
    if (s_streamline->GetMode() == StreamlineManager::Mode::DLSS_RayReconstruction) {
        const unsigned curSpp = s_accumulation.GetFrameCount();
        const unsigned configSpp = DxrRenderer::GetDlssEvalSpp();
        // Evaluate on frame 0 and every configSpp frames thereafter
        if (curSpp != 0 && (curSpp % configSpp) != 0) {
            allowEval = false;
        }

        // Force evaluate if camera moved significantly since last DLSS eval to
        // avoid reproject / alignment jitter when reusing previous DLSS output.
        static float lastEvalCamPos[3] = { 0.0f, 0.0f, 0.0f };
        static bool lastEvalCamPosValid = false;
        if (!lastEvalCamPosValid) {
            lastEvalCamPos[0] = g_cameraData.pos[0];
            lastEvalCamPos[1] = g_cameraData.pos[1];
            lastEvalCamPos[2] = g_cameraData.pos[2];
            lastEvalCamPosValid = true;
        }
        // Consider both translation and rotation (mouse look) as camera motion.
        static float lastEvalCamForward[3] = { 0.0f, 0.0f, 0.0f };
        static float lastEvalCamUp[3] = { 0.0f, 0.0f, 0.0f };
        static bool lastEvalCamOrientValid = false;
        auto camMoved = [&]() {
            float dx = g_cameraData.pos[0] - lastEvalCamPos[0];
            float dy = g_cameraData.pos[1] - lastEvalCamPos[1];
            float dz = g_cameraData.pos[2] - lastEvalCamPos[2];
            const float posThresh = 1e-4f; // small translation threshold
            bool posMoved = (dx*dx + dy*dy + dz*dz) > posThresh * posThresh;

            bool orientMoved = false;
            if (!lastEvalCamOrientValid) {
                lastEvalCamForward[0] = g_cameraData.forward[0];
                lastEvalCamForward[1] = g_cameraData.forward[1];
                lastEvalCamForward[2] = g_cameraData.forward[2];
                lastEvalCamUp[0] = g_cameraData.up[0];
                lastEvalCamUp[1] = g_cameraData.up[1];
                lastEvalCamUp[2] = g_cameraData.up[2];
                lastEvalCamOrientValid = true;
            } else {
                float dotF = g_cameraData.forward[0] * lastEvalCamForward[0] +
                             g_cameraData.forward[1] * lastEvalCamForward[1] +
                             g_cameraData.forward[2] * lastEvalCamForward[2];
                float dotU = g_cameraData.up[0] * lastEvalCamUp[0] +
                             g_cameraData.up[1] * lastEvalCamUp[1] +
                             g_cameraData.up[2] * lastEvalCamUp[2];
                // Small angular threshold; if forward/up change noticeably, consider as rotation
                const float orientThresh = 1e-3f; // ~0.03 degrees
                if (fabsf(1.0f - dotF) > orientThresh || fabsf(1.0f - dotU) > orientThresh) {
                    orientMoved = true;
                }
            }

            return posMoved || orientMoved;
        }();
        if (camMoved) {
            allowEval = true;
            // update last evaluated cam pos now that we will evaluate
            lastEvalCamPos[0] = g_cameraData.pos[0];
            lastEvalCamPos[1] = g_cameraData.pos[1];
            lastEvalCamPos[2] = g_cameraData.pos[2];
            // update last evaluated orientation
            lastEvalCamForward[0] = g_cameraData.forward[0];
            lastEvalCamForward[1] = g_cameraData.forward[1];
            lastEvalCamForward[2] = g_cameraData.forward[2];
            lastEvalCamUp[0] = g_cameraData.up[0];
            lastEvalCamUp[1] = g_cameraData.up[1];
            lastEvalCamUp[2] = g_cameraData.up[2];
        }

        if (!allowEval) {
           // fprintf(stderr, "DLSS-RR: skipping evaluate at spp=%u (every %u spp)\n", curSpp, configSpp);
        }
    }

    if (allowEval && s_streamline->Evaluate(
            dxrList.Get(),
            s_outputUAV.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            s_dlssOutputUAV.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            s_outputWidth, s_outputHeight, outW, outH,
            s_depthUAV.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            s_mvecUAV.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            s_normalRoughnessUAV.Get(),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            s_albedoUAV.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            s_specularAlbedoUAV.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            s_specHitDistanceUAV.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            nullptr, D3D12_RESOURCE_STATE_COMMON,
            resetHistory, jitterX, jitterY)) {
      usedDlss = true;
      postColor = s_dlssOutputUAV.Get();
    } else if (!allowEval && s_streamline->GetMode() == StreamlineManager::Mode::DLSS_RayReconstruction && s_dlssOutputUAV) {
       // Reuse previous DLSS output
       postColor = s_dlssOutputUAV.Get();
       // Important: Ensure we treat it as if DLSS was used (e.g. valid resource)
       usedDlss = true; 
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
  }

  // Copy postColor to the render target
  TransitionResource(dxrList.Get(), postColor,
                     usedDlss ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
                              : D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                     D3D12_RESOURCE_STATE_COPY_SOURCE);
  TransitionResource(dxrList.Get(), renderTarget, D3D12_RESOURCE_STATE_PRESENT,
                     D3D12_RESOURCE_STATE_COPY_DEST);

  // Validate sizes to avoid CopyResource invalid-argument errors
  const D3D12_RESOURCE_DESC srcDesc = postColor->GetDesc();
  if (srcDesc.Width != dstDesc.Width || srcDesc.Height != dstDesc.Height) {
    UINT copyW = (UINT)min(srcDesc.Width, dstDesc.Width);
    UINT copyH = (UINT)min(srcDesc.Height, dstDesc.Height);

    D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
    dstLoc.pResource = renderTarget;
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLoc.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
    srcLoc.pResource = postColor;
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    srcLoc.SubresourceIndex = 0;

    D3D12_BOX srcBox = {};
    srcBox.left = 0;
    srcBox.top = 0;
    srcBox.front = 0;
    srcBox.right = copyW;
    srcBox.bottom = copyH;
    srcBox.back = 1;

    dxrList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, &srcBox);
  } else {
    dxrList->CopyResource(renderTarget, postColor);
  }

  // Transition back
  TransitionResource(dxrList.Get(), renderTarget,
                     D3D12_RESOURCE_STATE_COPY_DEST,
                     D3D12_RESOURCE_STATE_RENDER_TARGET);
  TransitionResource(dxrList.Get(), postColor, D3D12_RESOURCE_STATE_COPY_SOURCE,
                     D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

  // Bind RTV for subsequent ImGui draws
  commandListBase->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
  return true;
}

} // namespace DxrRenderer
