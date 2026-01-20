#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <windows.h>
#include <wrl.h>

#ifdef _DEBUG
#include <d3d12sdklayers.h>
#endif
#include "assets/asset_loader.h"
#include "d3d12_helpers.h"
#include "dxc_wrapper.h"
#include "dxr_helpers.h"
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"
#include <codecvt>
#include <commctrl.h>
#include <commdlg.h>
#include <filesystem>
#include <locale>
#include <stdio.h>
#include <string>
#include <vector>

// Forward-declare ImGui Win32 WndProc handler (imgui_impl_win32.h documents
// this should be declared by user)
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd,
                                                             UINT msg,
                                                             WPARAM wParam,
                                                             LPARAM lParam);

namespace fs = std::filesystem;

using Microsoft::WRL::ComPtr;

static const UINT FrameCount = 2;

static ComPtr<ID3D12Device> g_device;
static ComPtr<ID3D12CommandQueue> g_commandQueue;
static ComPtr<IDXGISwapChain3> g_swapChain;

static ComPtr<ID3D12Resource> g_renderTargets[FrameCount];
static ComPtr<ID3D12DescriptorHeap> g_rtvHeap;
static UINT g_rtvDescriptorSize = 0;

static ComPtr<ID3D12DescriptorHeap> g_imguiHeap;

static DescriptorHeapAllocator g_cbvSrvAllocator;
static FrameResource g_frameResources[FrameCount];
static ComPtr<ID3D12GraphicsCommandList> g_commandList;
static UINT g_frameIndex = 0;
static HWND g_hwnd = nullptr;

// Loaded meshes from Asset loader
static std::vector<Asset::GpuMesh> g_loadedMeshes;
static std::vector<Asset::Material> g_loadedMaterials;
static std::vector<Asset::Texture> g_loadedTextures;
static D3D12_GPU_DESCRIPTOR_HANDLE g_texturesGpuStart = {0};
static ComPtr<ID3D12Resource>
    g_materialBuffer; // Persistent material constant buffer
static UINT g_textureDescriptorCount = 0;
static ComPtr<ID3D12Resource> g_materialConstantBuffer;
static bool g_showAssetsWindow =
    true; // Controls visibility of the Assets panel (can be closed/reopened)
static bool g_forceUncollapse =
    false; // When true, next Assets window will be forced open and focused
static std::string g_lastAssetStatus; // Human-readable status for the Assets UI
static std::string
    g_selectedAssetPath; // Path chosen by Open dialog (not yet imported)

static std::string WStringToUtf8(const std::wstring &ws) {
  if (ws.empty())
    return {};
  int size_needed = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(),
                                        NULL, 0, NULL, NULL);
  std::string s(size_needed, 0);
  WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), &s[0],
                      size_needed, NULL, NULL);
  return s;
}

static ComPtr<ID3D12Fence> g_fence;
static UINT64 g_fenceValues[FrameCount] = {};
static HANDLE g_fenceEvent = nullptr;

// Simple pipeline objects
static ComPtr<ID3D12RootSignature> g_rootSignature;
static ComPtr<ID3D12PipelineState> g_pipelineState;
static ComPtr<ID3D12PipelineState> g_meshPipelineState;
static ComPtr<ID3D12Resource> g_vertexBuffer;
static D3D12_VERTEX_BUFFER_VIEW g_vertexBufferView = {};
static ComPtr<ID3D12Resource> g_constantBuffer;
static float g_offsetX = 0.2f;

// --- DXR Globals ---
static bool g_rayTracingSupported = false;
static ComPtr<ID3D12Device5> g_dxrDevice;
struct MeshBLAS {
  AccelerationStructureBuffers buffers;
  UINT64 meshId; // index in g_loadedMeshes
};
static std::vector<MeshBLAS> g_allBLAS;
static AccelerationStructureBuffers g_tlas;

// --- DXR Pipeline Globals ---
static DxcHelper g_dxcHelper;
static ComPtr<ID3D12StateObject> g_rtStateObject;
static ComPtr<ID3D12Resource> g_sbtStorage;
static ComPtr<ID3D12RootSignature> g_rtGlobalRootSignature;
static ComPtr<ID3D12Resource> g_outputUAV;
static UINT g_outputUAVDescriptorSize = 0;
static D3D12_GPU_DESCRIPTOR_HANDLE g_outputUAVGpuHandle;
static ComPtr<ID3D12DescriptorHeap> g_uavHeap;

// Shader Table Helpers
struct ShaderTableEntry {
  void *id;
  // local root arguments (none for now)
};
static UINT g_shaderTableEntrySize = 0;
// Pointers into g_sbtStorage
static D3D12_GPU_VIRTUAL_ADDRESS g_rayGenShaderTable;
static D3D12_GPU_VIRTUAL_ADDRESS g_missShaderTable;
static D3D12_GPU_VIRTUAL_ADDRESS g_hitGroupShaderTable;

// Raw Helper to Add Subobject
struct SubobjectWrapper {
  D3D12_STATE_SUBOBJECT subobject;
  // definition storage
  D3D12_DXIL_LIBRARY_DESC dxilLibDesc;
  D3D12_EXPORT_DESC rayGenExport;
  D3D12_EXPORT_DESC missExport;
  D3D12_EXPORT_DESC hitExport;
  D3D12_HIT_GROUP_DESC hitGroupDesc;
  D3D12_RAYTRACING_SHADER_CONFIG shaderConfig;
  D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig;
  D3D12_GLOBAL_ROOT_SIGNATURE globalRootSig;
};

void CreateRayTracingPipeline() {
  if (!g_rayTracingSupported || !g_dxrDevice)
    return;

  // 1. Compile Shader
  OutputDebugStringA("Compiling Ray Tracing Shaders...\n");
  ComPtr<IDxcBlob> shaderBlob;
  try {
    shaderBlob =
        g_dxcHelper.Compile(L"shaders/raytracing.hlsl", L"", L"lib_6_3", {});
  } catch (const std::exception &e) {
    OutputDebugStringA("Shader Compilation Failed: ");
    OutputDebugStringA(e.what());
    OutputDebugStringA("\n");
    return;
  }

  // 2. Create Global Root Signature
  // t0: TLAS
  // u0: Usage Output (ensure u0 in shader matches)
  {
    D3D12_ROOT_PARAMETER params[2] = {};
    // t0
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].Descriptor.RegisterSpace = 0;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    // u0
    D3D12_DESCRIPTOR_RANGE uavRange = {};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 1;
    uavRange.BaseShaderRegister = 0;
    uavRange.RegisterSpace = 0;
    uavRange.OffsetInDescriptorsFromTableStart = 0;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &uavRange;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = 2;
    rootDesc.pParameters = params;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    ThrowIfFailed(D3D12SerializeRootSignature(
        &rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
    ThrowIfFailed(g_device->CreateRootSignature(
        0, signature->GetBufferPointer(), signature->GetBufferSize(),
        IID_PPV_ARGS(&g_rtGlobalRootSignature)));
  }

  // 3. Create State Object
  // We need to construct D3D12_STATE_OBJECT_DESC manually without d3dx12
  std::vector<D3D12_STATE_SUBOBJECT> subobjects;

  // Storage to keep descs alive
  static D3D12_DXIL_LIBRARY_DESC libDesc = {};
  static D3D12_EXPORT_DESC exports[3] = {};
  static D3D12_HIT_GROUP_DESC hitGroupDesc = {};
  static D3D12_RAYTRACING_SHADER_CONFIG shaderConfig = {};
  static D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig = {};
  static D3D12_GLOBAL_ROOT_SIGNATURE globalRootSigDesc = {};

  // DXIL Library
  libDesc.DXILLibrary.pShaderBytecode = shaderBlob->GetBufferPointer();
  libDesc.DXILLibrary.BytecodeLength = shaderBlob->GetBufferSize();
  exports[0].Name = L"RayGen";
  exports[0].Flags = D3D12_EXPORT_FLAG_NONE;
  exports[1].Name = L"Miss";
  exports[1].Flags = D3D12_EXPORT_FLAG_NONE;
  exports[2].Name = L"ClosestHit";
  exports[2].Flags = D3D12_EXPORT_FLAG_NONE;
  libDesc.NumExports = 3;
  libDesc.pExports = exports;

  D3D12_STATE_SUBOBJECT libSub = {};
  libSub.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
  libSub.pDesc = &libDesc;
  subobjects.push_back(libSub);

  // Hit Group
  hitGroupDesc.HitGroupExport = L"HitGroup";
  hitGroupDesc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
  hitGroupDesc.ClosestHitShaderImport = L"ClosestHit";
  // AnyHit/Intersection null

  D3D12_STATE_SUBOBJECT hitSub = {};
  hitSub.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
  hitSub.pDesc = &hitGroupDesc;
  subobjects.push_back(hitSub);

  // Shader Config
  shaderConfig.MaxPayloadSizeInBytes = 4 * sizeof(float);   // float4 color
  shaderConfig.MaxAttributeSizeInBytes = 2 * sizeof(float); // barycentrics
  D3D12_STATE_SUBOBJECT shaderConfigSub = {};
  shaderConfigSub.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
  shaderConfigSub.pDesc = &shaderConfig;
  subobjects.push_back(shaderConfigSub);

  // Pipeline Config
  pipelineConfig.MaxTraceRecursionDepth = 1;
  D3D12_STATE_SUBOBJECT pipeConfigSub = {};
  pipeConfigSub.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
  pipeConfigSub.pDesc = &pipelineConfig;
  subobjects.push_back(pipeConfigSub);

  // Global Root Signature
  globalRootSigDesc.pGlobalRootSignature = g_rtGlobalRootSignature.Get();
  D3D12_STATE_SUBOBJECT rootSigSub = {};
  rootSigSub.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
  rootSigSub.pDesc = &globalRootSigDesc;
  subobjects.push_back(rootSigSub);

  D3D12_STATE_OBJECT_DESC stateObjDesc = {};
  stateObjDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
  stateObjDesc.NumSubobjects = (UINT)subobjects.size();
  stateObjDesc.pSubobjects = subobjects.data();

  ThrowIfFailed(g_dxrDevice->CreateStateObject(&stateObjDesc,
                                               IID_PPV_ARGS(&g_rtStateObject)));
  OutputDebugStringA("Ray Tracing Pipeline Created!\n");

  // 4. Create Shader Table
  // Get Properties
  ComPtr<ID3D12StateObjectProperties> properties;
  ThrowIfFailed(g_rtStateObject.As(&properties));

  void *rayGenId = properties->GetShaderIdentifier(L"RayGen");
  void *missId = properties->GetShaderIdentifier(L"Miss");
  void *hitGroupId = properties->GetShaderIdentifier(L"HitGroup");

  UINT shaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
  g_shaderTableEntrySize = Align(shaderIdentifierSize,
                                 D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
  UINT shaderTableSize =
      g_shaderTableEntrySize * 3; // 1 RayGen, 1 Miss, 1 HitGroup

  AllocateUploadBuffer(g_device.Get(), nullptr, shaderTableSize, &g_sbtStorage,
                       L"Shader Table");

  // Map and Write
  UINT8 *pData;
  g_sbtStorage->Map(0, nullptr, (void **)&pData);

  memcpy(pData, rayGenId, shaderIdentifierSize); // RayGen at 0
  memcpy(pData + g_shaderTableEntrySize, missId,
         shaderIdentifierSize); // Miss at 1
  memcpy(pData + g_shaderTableEntrySize * 2, hitGroupId,
         shaderIdentifierSize); // Hit at 2

  g_sbtStorage->Unmap(0, nullptr);

  g_rayGenShaderTable = g_sbtStorage->GetGPUVirtualAddress();
  g_missShaderTable = g_rayGenShaderTable + g_shaderTableEntrySize;
  g_hitGroupShaderTable = g_missShaderTable + g_shaderTableEntrySize;

  // 5. Create Output UAV
  D3D12_DESCRIPTOR_HEAP_DESC uavHeapDesc = {};
  uavHeapDesc.NumDescriptors = 1;
  uavHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  uavHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  g_device->CreateDescriptorHeap(&uavHeapDesc, IID_PPV_ARGS(&g_uavHeap));

  AllocateUAVBuffer(g_device.Get(), 1280 * 720 * 4 * 4, &g_outputUAV,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS, L"RT Output");
  // Recreate it as Texture2D for easier management?
  // AllocateUAVBuffer makes a buffer. RayGen writes to RWTexture2D.
  // We should use a Texture2D Resource.

  {
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 1280;
    desc.Height = 720;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    g_outputUAV.Reset();
    ThrowIfFailed(g_device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
        IID_PPV_ARGS(&g_outputUAV)));

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0;
    uavDesc.Texture2D.PlaneSlice = 0;

    g_device->CreateUnorderedAccessView(
        g_outputUAV.Get(), nullptr, &uavDesc,
        g_uavHeap->GetCPUDescriptorHandleForHeapStart());
    g_outputUAVGpuHandle = g_uavHeap->GetGPUDescriptorHandleForHeapStart();
  }
}

// Helper to build AS for all loaded meshes
// This is a naive implementation that rebuilds everything from scratch
void BuildAccelerationStructures() {
  if (!g_rayTracingSupported || !g_dxrDevice)
    return;

  if (g_loadedMeshes.empty())
    return;

  // Wait for GPU to finish previous work to avoid hazards (optimization:
  // implement proper sync)
  {
    const UINT64 fence = g_fenceValues[g_frameIndex];
    g_commandQueue->Signal(g_fence.Get(), fence);
    g_fenceValues[g_frameIndex]++;

    if (g_fence->GetCompletedValue() < fence) {
      g_fence->SetEventOnCompletion(fence, g_fenceEvent);
      WaitForSingleObject(g_fenceEvent, INFINITE);
    }
  }

  // Create a transient command list for building AS
  ComPtr<ID3D12CommandAllocator> cmdAlloc;
  ComPtr<ID3D12GraphicsCommandList4> cmdList;

  ThrowIfFailed(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                 IID_PPV_ARGS(&cmdAlloc)));
  ThrowIfFailed(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                            cmdAlloc.Get(), nullptr,
                                            IID_PPV_ARGS(&cmdList)));

  // 1. Build BLAS for each mesh
  g_allBLAS.clear();
  for (size_t i = 0; i < g_loadedMeshes.size(); ++i) {
    const auto &mesh = g_loadedMeshes[i];

    // Asset::Vertex is layout {pos, normal, tangent, uv}. Position is at offset
    // 0. Stride is sizeof(Asset::Vertex).
    auto bl =
        BuildBLAS(g_dxrDevice.Get(), cmdList.Get(),
                  mesh.vertexBuffer->GetGPUVirtualAddress(), mesh.vertexCount,
                  sizeof(Asset::Vertex),
                  mesh.indexBuffer->GetGPUVirtualAddress(), mesh.indexCount);
    g_allBLAS.push_back({bl, (UINT64)i});
  }

  // 2. Build TLAS
  // Create instances for each loaded mesh (assume identity transform for now or
  // use node system later) For now we map 1:1 mesh : instance.
  std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs;
  for (size_t i = 0; i < g_allBLAS.size(); ++i) {
    D3D12_RAYTRACING_INSTANCE_DESC inst = {};
    // Identity matrix
    inst.Transform[0][0] = inst.Transform[1][1] = inst.Transform[2][2] = 1.0f;
    inst.InstanceID = (UINT)i;
    inst.InstanceMask = 0xFF;
    inst.InstanceContributionToHitGroupIndex = 0;
    inst.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
    inst.AccelerationStructure =
        g_allBLAS[i].buffers.result->GetGPUVirtualAddress();
    instanceDescs.push_back(inst);
  }

  // Allocate instance buffer
  ComPtr<ID3D12Resource> instanceDescBuffer;
  AllocateUploadBuffer(g_device.Get(), instanceDescs.data(),
                       instanceDescs.size() *
                           sizeof(D3D12_RAYTRACING_INSTANCE_DESC),
                       &instanceDescBuffer, L"TLAS Instance Buffer");

  // DXR requires the GPU virtual address of the instance descs
  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
  inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
  inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  inputs.Flags =
      D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
  inputs.NumDescs = (UINT)instanceDescs.size();
  inputs.InstanceDescs = instanceDescBuffer->GetGPUVirtualAddress();

  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
  g_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

  // Reuse/Create Global TLAS buffers
  g_tlas.scratchSizeInBytes =
      Align(info.ScratchDataSizeInBytes,
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
  g_tlas.resultSizeInBytes =
      Align(info.ResultDataMaxSizeInBytes,
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);

  // Always reallocate for simplicity in this demo (TODO: reuse)
  AllocateUAVBuffer(g_device.Get(), g_tlas.scratchSizeInBytes, &g_tlas.scratch,
                    D3D12_RESOURCE_STATE_COMMON, L"TLAS Scratch");
  AllocateUAVBuffer(g_device.Get(), g_tlas.resultSizeInBytes, &g_tlas.result,
                    D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                    L"TLAS Result");

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
  buildDesc.Inputs = inputs;
  buildDesc.DestAccelerationStructureData =
      g_tlas.result->GetGPUVirtualAddress();
  buildDesc.ScratchAccelerationStructureData =
      g_tlas.scratch->GetGPUVirtualAddress();

  cmdList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

  // Barrier
  D3D12_RESOURCE_BARRIER uavBarrier = {};
  uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  uavBarrier.UAV.pResource = g_tlas.result.Get();
  cmdList->ResourceBarrier(1, &uavBarrier);

  cmdList->Close();

  // Execute
  ID3D12CommandList *pp[] = {cmdList.Get()};
  g_commandQueue->ExecuteCommandLists(1, pp);

  // Wait again for build to finish (simple sync)
  {
    const UINT64 fence = g_fenceValues[g_frameIndex];
    g_commandQueue->Signal(g_fence.Get(), fence);
    g_fenceValues[g_frameIndex]++;
    if (g_fence->GetCompletedValue() < fence) {
      g_fence->SetEventOnCompletion(fence, g_fenceEvent);
      WaitForSingleObject(g_fenceEvent, INFINITE);
    }
  }

  OutputDebugStringA("DXR Acceleration Structures Built.\n");
}

inline void ThrowIfFailedEx(HRESULT hr, const char *file, int line) {
  if (FAILED(hr)) {
    char buf[512];
    sprintf_s(buf, "HRESULT 0x%08x at %s:%d\n", static_cast<unsigned>(hr), file,
              line);
    OutputDebugStringA(buf);

    // Write to log file for debugging
    FILE *logFile = nullptr;
    if (fopen_s(&logFile, "error.log", "a") == 0 && logFile) {
      fprintf(logFile, "%s", buf);
      fclose(logFile);
    }

    MessageBoxA(nullptr, buf, "Fatal Error", MB_OK | MB_ICONERROR);
    ExitProcess(static_cast<UINT>(hr));
  }
}

#define ThrowIfFailed(hr) ThrowIfFailedEx(hr, __FILE__, __LINE__)

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

// Helper to find shader file with fallback paths
static std::wstring FindShaderFile(const wchar_t *relativePath) {
  // Try multiple possible locations
  std::vector<std::wstring> searchPaths;
  searchPaths.push_back(relativePath); // Current directory
  searchPaths.push_back(std::wstring(L"..\\..\\") +
                        relativePath); // From build\Release\
    searchPaths.push_back(std::wstring(L"..\\") + relativePath);      // From build\

  for (size_t i = 0; i < searchPaths.size(); ++i) {
    if (fs::exists(searchPaths[i])) {
      return searchPaths[i];
    }
  }

  // Return original path if not found (will fail later with clear error)
  return relativePath;
}

// Enable D3D12 debug layer when available (debug builds)
static void EnableD3D12DebugLayer() {
#ifdef _DEBUG
  ComPtr<ID3D12Debug> debugController;
  if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
    debugController->EnableDebugLayer();
  }
#endif
}

// Select the first suitable hardware adapter (non-software) that supports D3D12
static void GetHardwareAdapter(IDXGIFactory4 *pFactory,
                               IDXGIAdapter1 **ppAdapter) {
  *ppAdapter = nullptr;
  ComPtr<IDXGIAdapter1> adapter;
  for (UINT adapterIndex = 0;; ++adapterIndex) {
    if (DXGI_ERROR_NOT_FOUND == pFactory->EnumAdapters1(adapterIndex, &adapter))
      break;

    DXGI_ADAPTER_DESC1 desc;
    adapter->GetDesc1(&desc);

    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
      continue; // skip software adapters

    // Check D3D12 support
    if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                    __uuidof(ID3D12Device), nullptr))) {
      adapter.CopyTo(ppAdapter);
      return;
    }
  }
}

bool InitD3D12(HWND hwnd) {
  FILE *log = nullptr;
  fopen_s(&log, "startup.log", "a");
  if (log) {
    fprintf(log, "InitD3D12 start\n");
    fclose(log);
  }

  g_hwnd = hwnd;
  UINT dxgiFactoryFlags = 0;

  EnableD3D12DebugLayer();

  log = nullptr;
  fopen_s(&log, "startup.log", "a");
  if (log) {
    fprintf(log, "After EnableD3D12DebugLayer\n");
    fclose(log);
  }

  ComPtr<IDXGIFactory4> factory;
  ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

  log = nullptr;
  fopen_s(&log, "startup.log", "a");
  if (log) {
    fprintf(log, "After CreateDXGIFactory2\n");
    fclose(log);
  }

  // Try to find a hardware adapter that supports D3D12
  ComPtr<IDXGIAdapter1> hardwareAdapter;
  GetHardwareAdapter(factory.Get(), &hardwareAdapter);

  log = nullptr;
  fopen_s(&log, "startup.log", "a");
  if (log) {
    fprintf(log, "After GetHardwareAdapter\n");
    fclose(log);
  }

  HRESULT hr = E_FAIL;
  if (hardwareAdapter) {
    hr = D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0,
                           IID_PPV_ARGS(&g_device));
    log = nullptr;
    fopen_s(&log, "startup.log", "a");
    if (log) {
      fprintf(log, "D3D12CreateDevice(hardwareAdapter) = 0x%08x\n", hr);
      fclose(log);
    }
  }

  // Check DXR Support
  if (SUCCEEDED(hr)) {
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
    if (SUCCEEDED(g_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5,
                                                &options5, sizeof(options5)))) {
      if (options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0) {
        g_rayTracingSupported = true;
        if (SUCCEEDED(g_device.As(&g_dxrDevice))) {
          OutputDebugStringA(
              "DXR Ray Tracing Supported and Device Initialized.\n");
          // Initialize Pipeline
          CreateRayTracingPipeline();
        } else {
          g_rayTracingSupported = false;
          OutputDebugStringA(
              "DXR Supported but failed to Query ID3D12Device5 interface.\n");
        }
      }
    }
  }

  if (FAILED(hr)) {
    // Fall back to default adapter/device or WARP
    hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                           IID_PPV_ARGS(&g_device));
    log = nullptr;
    fopen_s(&log, "startup.log", "a");
    if (log) {
      fprintf(log, "D3D12CreateDevice(nullptr) = 0x%08x\n", hr);
      fclose(log);
    }
    if (FAILED(hr)) {
      ComPtr<IDXGIAdapter> warpAdapter;
      ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));
      ThrowIfFailed(D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                      IID_PPV_ARGS(&g_device)));
    }
  }

  log = nullptr;
  fopen_s(&log, "startup.log", "a");
  if (log) {
    fprintf(log, "Device created, about to create command queue\n");
    fflush(log);
    fclose(log);
  }

  // Create command queue
  D3D12_COMMAND_QUEUE_DESC queueDesc = {};
  queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

  log = nullptr;
  fopen_s(&log, "startup.log", "a");
  if (log) {
    fprintf(log, "Before CreateCommandQueue, g_device = %p\n", g_device.Get());
    fflush(log);
    fclose(log);
  }

  hr = g_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&g_commandQueue));

  log = nullptr;
  fopen_s(&log, "startup.log", "a");
  if (log) {
    fprintf(log, "CreateCommandQueue returned 0x%08x\n", hr);
    fflush(log);
    fclose(log);
  }

  ThrowIfFailed(hr);

  log = nullptr;
  fopen_s(&log, "startup.log", "a");
  if (log) {
    fprintf(log, "CommandQueue created successfully\n");
    fflush(log);
    fclose(log);
  }

  // Create swap chain
  DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
  swapChainDesc.BufferCount = 2;
  swapChainDesc.Width = 1280;
  swapChainDesc.Height = 720;
  swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  swapChainDesc.SampleDesc.Count = 1;

  ComPtr<IDXGISwapChain1> swapChain1;
  ThrowIfFailed(factory->CreateSwapChainForHwnd(g_commandQueue.Get(), hwnd,
                                                &swapChainDesc, nullptr,
                                                nullptr, &swapChain1));

  ThrowIfFailed(swapChain1.As(&g_swapChain));

  g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();

  // Create RTV descriptor heap
  D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
  rtvHeapDesc.NumDescriptors = FrameCount;
  rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  ThrowIfFailed(
      g_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&g_rtvHeap)));
  g_rtvDescriptorSize = g_device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

  D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
      g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
  D3D12_CPU_DESCRIPTOR_HANDLE rtvHandleStart = rtvHandle;
  for (UINT i = 0; i < FrameCount; ++i) {
    ThrowIfFailed(g_swapChain->GetBuffer(i, IID_PPV_ARGS(&g_renderTargets[i])));
    g_device->CreateRenderTargetView(g_renderTargets[i].Get(), nullptr,
                                     rtvHandle);
    rtvHandle.ptr =
        rtvHandleStart.ptr + (SIZE_T)((i + 1) * g_rtvDescriptorSize);
  }

  // Initialize descriptor allocator for CBV/SRV/UAV
  g_cbvSrvAllocator.Init(g_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                         1024, FrameCount);

  // Create descriptor heap for ImGui fonts (1 descriptor)
  D3D12_DESCRIPTOR_HEAP_DESC imguiHeapDesc = {};
  imguiHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  imguiHeapDesc.NumDescriptors = 1;
  imguiHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  ThrowIfFailed(g_device->CreateDescriptorHeap(&imguiHeapDesc,
                                               IID_PPV_ARGS(&g_imguiHeap)));

  // Create per-frame resources (command allocators + fence values)
  for (UINT i = 0; i < FrameCount; ++i) {
    ThrowIfFailed(g_device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&g_frameResources[i].commandAllocator)));
    g_frameResources[i].fenceValue = 0;
    g_frameResources[i].transientDescriptorOffset = 0;
  }

  // Create a single command list (can be recycled)
  ThrowIfFailed(g_device->CreateCommandList(
      0, D3D12_COMMAND_LIST_TYPE_DIRECT,
      g_frameResources[g_frameIndex].commandAllocator.Get(), nullptr,
      IID_PPV_ARGS(&g_commandList)));
  // Command lists are created in the recording state. Close it for now.
  ThrowIfFailed(g_commandList->Close());

  // Create fence
  ThrowIfFailed(g_device->CreateFence(g_fenceValues[g_frameIndex],
                                      D3D12_FENCE_FLAG_NONE,
                                      IID_PPV_ARGS(&g_fence)));
  g_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  if (g_fenceEvent == nullptr) {
    ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
  }

  // Initialize fence values
  for (UINT i = 0; i < FrameCount; ++i)
    g_fenceValues[i] = 0;

  // --- Create a root signature with CBV b0 (vertex), descriptor table t0
  // (SRV), and CBV b1 (pixel material) ---
  D3D12_ROOT_PARAMETER rootParameters[3] = {};
  // b0 - transform CBV for vertex shader
  rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  rootParameters[0].Descriptor.ShaderRegister = 0;
  rootParameters[0].Descriptor.RegisterSpace = 0;
  rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
  // t0-t15 - descriptor table (SRV) for pixel shader (16 texture slots)
  static D3D12_DESCRIPTOR_RANGE descRange = {};
  descRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  descRange.NumDescriptors =
      16; // baseColor, metallicRoughness, normal, occlusion, emissive + extras
  descRange.BaseShaderRegister = 0;
  descRange.RegisterSpace = 0;
  descRange.OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
  rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
  rootParameters[1].DescriptorTable.pDescriptorRanges = &descRange;
  rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  // b1 - material CBV for pixel shader
  rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  rootParameters[2].Descriptor.ShaderRegister = 1;
  rootParameters[2].Descriptor.RegisterSpace = 0;
  rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  // static sampler for textures
  D3D12_STATIC_SAMPLER_DESC sampler = {};
  sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
  sampler.ShaderRegister = 0;
  sampler.RegisterSpace = 0;
  sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
  rootSignatureDesc.NumParameters = _countof(rootParameters);
  rootSignatureDesc.pParameters = rootParameters;
  rootSignatureDesc.NumStaticSamplers = 1;
  rootSignatureDesc.pStaticSamplers = &sampler;
  rootSignatureDesc.Flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

  ComPtr<ID3DBlob> signature;
  ComPtr<ID3DBlob> error;
  ThrowIfFailed(D3D12SerializeRootSignature(
      &rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
  ThrowIfFailed(g_device->CreateRootSignature(0, signature->GetBufferPointer(),
                                              signature->GetBufferSize(),
                                              IID_PPV_ARGS(&g_rootSignature)));

  // --- Compile simple shaders for demo triangle ---
  log = nullptr;
  fopen_s(&log, "startup.log", "a");
  if (log) {
    fprintf(log, "About to compile shaders\n");
    fflush(log);
    fclose(log);
  }

  ComPtr<ID3DBlob> vsBlob;
  ComPtr<ID3DBlob> psBlob;
  std::wstring simpleShaderPath = FindShaderFile(L"shaders\\simple.hlsl");
  {
    char debugMsg[512];
    sprintf_s(debugMsg, "Loading simple shader from: %ls\n",
              simpleShaderPath.c_str());
    OutputDebugStringA(debugMsg);

    log = nullptr;
    fopen_s(&log, "startup.log", "a");
    if (log) {
      fprintf(log, "Shader path: %ls\n", simpleShaderPath.c_str());
      fflush(log);
      fclose(log);
    }
  }
  hr = D3DCompileFromFile(simpleShaderPath.c_str(), nullptr,
                          D3D_COMPILE_STANDARD_FILE_INCLUDE, "VSMain", "vs_5_0",
                          0, 0, &vsBlob, &error);
  if (FAILED(hr) && error)
    OutputDebugStringA((char *)error->GetBufferPointer());

  log = nullptr;
  fopen_s(&log, "startup.log", "a");
  if (log) {
    fprintf(log, "VSMain compile returned 0x%08x\n", hr);
    fflush(log);
    fclose(log);
  }

  ThrowIfFailed(hr);
  hr = D3DCompileFromFile(simpleShaderPath.c_str(), nullptr,
                          D3D_COMPILE_STANDARD_FILE_INCLUDE, "PSMain", "ps_5_0",
                          0, 0, &psBlob, &error);
  if (FAILED(hr) && error)
    OutputDebugStringA((char *)error->GetBufferPointer());
  ThrowIfFailed(hr);

  // --- Compile mesh PBR shaders (full vertex layout + PBR material pixel
  // shader) ---
  ComPtr<ID3DBlob> vsMeshBlob;
  ComPtr<ID3DBlob> psMeshBlob;
  std::wstring pbrShaderPath = FindShaderFile(L"shaders\\pbr_mesh.hlsl");
  {
    char debugMsg[512];
    sprintf_s(debugMsg, "Loading PBR shader from: %ls\n",
              pbrShaderPath.c_str());
    OutputDebugStringA(debugMsg);
  }
  hr = D3DCompileFromFile(pbrShaderPath.c_str(), nullptr,
                          D3D_COMPILE_STANDARD_FILE_INCLUDE, "VSMainMesh",
                          "vs_5_0", 0, 0, &vsMeshBlob, &error);
  if (FAILED(hr) && error)
    OutputDebugStringA((char *)error->GetBufferPointer());
  ThrowIfFailed(hr);
  hr = D3DCompileFromFile(pbrShaderPath.c_str(), nullptr,
                          D3D_COMPILE_STANDARD_FILE_INCLUDE, "PSMainMesh",
                          "ps_5_0", 0, 0, &psMeshBlob, &error);
  if (FAILED(hr) && error)
    OutputDebugStringA((char *)error->GetBufferPointer());
  ThrowIfFailed(hr);

  // --- Create PSO ---
  D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};

  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.InputLayout = {inputElementDescs, _countof(inputElementDescs)};
  psoDesc.pRootSignature = g_rootSignature.Get();
  psoDesc.VS = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
  psoDesc.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};

  D3D12_RASTERIZER_DESC rasterDesc = {};
  rasterDesc.FillMode = D3D12_FILL_MODE_SOLID;
  rasterDesc.CullMode = D3D12_CULL_MODE_BACK;
  rasterDesc.FrontCounterClockwise = FALSE;
  rasterDesc.DepthBias = 0;
  rasterDesc.DepthBiasClamp = 0.0f;
  rasterDesc.SlopeScaledDepthBias = 0.0f;
  rasterDesc.DepthClipEnable = TRUE;
  rasterDesc.MultisampleEnable = FALSE;
  rasterDesc.AntialiasedLineEnable = FALSE;
  rasterDesc.ForcedSampleCount = 0;
  rasterDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

  D3D12_BLEND_DESC blendDesc = {};
  blendDesc.AlphaToCoverageEnable = FALSE;
  blendDesc.IndependentBlendEnable = FALSE;
  for (int i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
    blendDesc.RenderTarget[i].BlendEnable = FALSE;
    blendDesc.RenderTarget[i].LogicOpEnable = FALSE;
    blendDesc.RenderTarget[i].SrcBlend = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[i].DestBlend = D3D12_BLEND_ZERO;
    blendDesc.RenderTarget[i].BlendOp = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[i].SrcBlendAlpha = D3D12_BLEND_ONE;
    blendDesc.RenderTarget[i].DestBlendAlpha = D3D12_BLEND_ZERO;
    blendDesc.RenderTarget[i].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[i].LogicOp = D3D12_LOGIC_OP_NOOP;
    blendDesc.RenderTarget[i].RenderTargetWriteMask =
        D3D12_COLOR_WRITE_ENABLE_ALL;
  }

  D3D12_DEPTH_STENCIL_DESC depthDesc = {};
  depthDesc.DepthEnable = FALSE;
  depthDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
  depthDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
  depthDesc.StencilEnable = FALSE;

  psoDesc.RasterizerState = rasterDesc;
  psoDesc.BlendState = blendDesc;
  psoDesc.DepthStencilState = depthDesc;
  psoDesc.SampleMask = UINT_MAX;
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.NumRenderTargets = 1;
  psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  psoDesc.SampleDesc.Count = 1;

  ThrowIfFailed(g_device->CreateGraphicsPipelineState(
      &psoDesc, IID_PPV_ARGS(&g_pipelineState)));

  // --- Create a mesh PSO (position-only vertex layout, simple pixel shader)
  // ---
  D3D12_INPUT_ELEMENT_DESC meshInputLayout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};

  D3D12_GRAPHICS_PIPELINE_STATE_DESC meshPsoDesc =
      psoDesc; // copy base description
  meshPsoDesc.InputLayout = {meshInputLayout, _countof(meshInputLayout)};
  meshPsoDesc.VS = {vsMeshBlob->GetBufferPointer(),
                    vsMeshBlob->GetBufferSize()};
  meshPsoDesc.PS = {psMeshBlob->GetBufferPointer(),
                    psMeshBlob->GetBufferSize()};

  ThrowIfFailed(g_device->CreateGraphicsPipelineState(
      &meshPsoDesc, IID_PPV_ARGS(&g_meshPipelineState)));

  // --- Initialize ImGui ---
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  ImGui_ImplWin32_Init(hwnd);
  // Initialize DX12 backend with device, number of frames, RTV format and
  // descriptor heap for fonts
  ImGui_ImplDX12_Init(g_device.Get(), FrameCount, DXGI_FORMAT_R8G8B8A8_UNORM,
                      g_imguiHeap.Get(),
                      g_imguiHeap->GetCPUDescriptorHandleForHeapStart(),
                      g_imguiHeap->GetGPUDescriptorHandleForHeapStart());

  ImGui_ImplDX12_CreateDeviceObjects();

  // Initialize asset loader with device & command queue so it can perform
  // uploads
  Asset::Initialize(g_device.Get(), g_commandQueue.Get());

  // --- Create a simple vertex buffer in upload heap ---
  struct Vertex {
    float pos[3];
    float col[3];
  };
  Vertex triangleVertices[] = {{{0.0f, 0.25f, 0.0f}, {1.0f, 0.0f, 0.0f}},
                               {{0.25f, -0.25f, 0.0f}, {0.0f, 1.0f, 0.0f}},
                               {{-0.25f, -0.25f, 0.0f}, {0.0f, 0.0f, 1.0f}}};

  const UINT vertexBufferSize = sizeof(triangleVertices);

  D3D12_HEAP_PROPERTIES heapProps = {};
  heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

  D3D12_RESOURCE_DESC vbDesc = {};
  vbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  vbDesc.Width = vertexBufferSize;
  vbDesc.Height = 1;
  vbDesc.DepthOrArraySize = 1;
  vbDesc.MipLevels = 1;
  vbDesc.SampleDesc.Count = 1;
  vbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

  ThrowIfFailed(g_device->CreateCommittedResource(
      &heapProps, D3D12_HEAP_FLAG_NONE, &vbDesc,
      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
      IID_PPV_ARGS(&g_vertexBuffer)));

  // Copy vertex data
  UINT8 *pVertexDataBegin;
  D3D12_RANGE readRange = {0, 0};
  ThrowIfFailed(g_vertexBuffer->Map(
      0, &readRange, reinterpret_cast<void **>(&pVertexDataBegin)));
  memcpy(pVertexDataBegin, triangleVertices, sizeof(triangleVertices));
  g_vertexBuffer->Unmap(0, nullptr);

  g_vertexBufferView.BufferLocation = g_vertexBuffer->GetGPUVirtualAddress();
  g_vertexBufferView.StrideInBytes = sizeof(Vertex);
  g_vertexBufferView.SizeInBytes = vertexBufferSize;

  // --- Create constant buffer (upload heap) ---
  struct AlignConstants {
    float offset[4];
  };
  const UINT64 cbSize = (sizeof(AlignConstants) + 255) & ~255;

  D3D12_RESOURCE_DESC cbDesc = {};
  cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  cbDesc.Width = cbSize;
  cbDesc.Height = 1;
  cbDesc.DepthOrArraySize = 1;
  cbDesc.MipLevels = 1;
  cbDesc.SampleDesc.Count = 1;
  cbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

  D3D12_HEAP_PROPERTIES uploadHeapProps = {};
  uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

  ThrowIfFailed(g_device->CreateCommittedResource(
      &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &cbDesc,
      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
      IID_PPV_ARGS(&g_constantBuffer)));

  // Initialize constant buffer data (small offset)
  AlignConstants constants = {{0.2f, 0.0f, 0.0f, 0.0f}};
  UINT8 *pCbData = nullptr;
  D3D12_RANGE readRangeCB = {0, 0};
  ThrowIfFailed(g_constantBuffer->Map(0, &readRangeCB,
                                      reinterpret_cast<void **>(&pCbData)));
  memcpy(pCbData, &constants, sizeof(constants));
  g_constantBuffer->Unmap(0, nullptr);

  // --- Create persistent material constant buffer ---
  struct MaterialCB {
    float baseColorFactor[4];
    float params1[4];
    float specular[4];
    float emissiveFactor[4];
    int textureIndices[4];
    int emissiveAndPad[4]; // x=emissiveTexIndex, yzw=padding
  };
  const UINT64 matCbSize = (sizeof(MaterialCB) + 255) & ~255;
  D3D12_RESOURCE_DESC matCbDesc = {};
  matCbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  matCbDesc.Width = matCbSize;
  matCbDesc.Height = 1;
  matCbDesc.DepthOrArraySize = 1;
  matCbDesc.MipLevels = 1;
  matCbDesc.SampleDesc.Count = 1;
  matCbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ThrowIfFailed(g_device->CreateCommittedResource(
      &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &matCbDesc,
      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
      IID_PPV_ARGS(&g_materialConstantBuffer)));

  return true;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam,
                         LPARAM lParam) {
  if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
    return true;

  switch (message) {
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  }

  return DefWindowProc(hWnd, message, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
  // Log startup
  FILE *startLog = nullptr;
  if (fopen_s(&startLog, "startup.log", "w") == 0 && startLog) {
    fprintf(startLog, "Application starting...\n");
    fclose(startLog);
  }

  const wchar_t CLASS_NAME[] = L"ProjectRenderWndClass";

  WNDCLASSW wc = {};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = hInstance;
  wc.lpszClassName = CLASS_NAME;

  RegisterClassW(&wc);

  HWND hwnd = CreateWindowExW(0, CLASS_NAME, L"project-render - DX12 Starter",
                              WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              1280, 720, nullptr, nullptr, hInstance, nullptr);

  if (!hwnd) {
    return 0;
  }

  ShowWindow(hwnd, nCmdShow);

  if (!InitD3D12(hwnd)) {
    MessageBoxA(nullptr, "Failed to initialize D3D12", "Error",
                MB_OK | MB_ICONERROR);
    return -1;
  }

  // Basic message loop + simple render
  MSG msg = {};

  auto PopulateCommandList = [&]() {
    // Reset per-frame command allocator and command list
    ThrowIfFailed(g_frameResources[g_frameIndex].commandAllocator->Reset());
    ThrowIfFailed(g_commandList->Reset(
        g_frameResources[g_frameIndex].commandAllocator.Get(), nullptr));

    // Reset per-frame transient descriptor allocator
    g_cbvSrvAllocator.ResetFrame(g_frameIndex);

    // Get RTV handle
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
        g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtvHandle.ptr =
        rtvHandle.ptr + (SIZE_T)(g_frameIndex * g_rtvDescriptorSize);

    // Set viewport and scissor
    D3D12_VIEWPORT viewport = {};
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.Width = 1280.0f;
    viewport.Height = 720.0f;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    D3D12_RECT scissorRect = {0, 0, 1280, 720};

    g_commandList->RSSetViewports(1, &viewport);
    g_commandList->RSSetScissorRects(1, &scissorRect);

    // DXR Path
    if (g_rayTracingSupported && g_rtStateObject && g_tlas.result) {
      ComPtr<ID3D12GraphicsCommandList4> dxrList;
      if (SUCCEEDED(g_commandList.As(&dxrList))) {
        // 1. Dispatch Rays
        dxrList->SetPipelineState1(g_rtStateObject.Get());
        dxrList->SetComputeRootSignature(g_rtGlobalRootSignature.Get());
        dxrList->SetComputeRootShaderResourceView(
            0, g_tlas.result->GetGPUVirtualAddress());
        ID3D12DescriptorHeap *heaps[] = {g_uavHeap.Get()};
        dxrList->SetDescriptorHeaps(1, heaps);
        dxrList->SetComputeRootDescriptorTable(1, g_outputUAVGpuHandle);

        D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
        dispatchDesc.RayGenerationShaderRecord.StartAddress =
            g_rayGenShaderTable;
        dispatchDesc.RayGenerationShaderRecord.SizeInBytes =
            g_shaderTableEntrySize;
        dispatchDesc.MissShaderTable.StartAddress = g_missShaderTable;
        dispatchDesc.MissShaderTable.SizeInBytes = g_shaderTableEntrySize;
        dispatchDesc.MissShaderTable.StrideInBytes = g_shaderTableEntrySize;
        dispatchDesc.HitGroupTable.StartAddress = g_hitGroupShaderTable;
        dispatchDesc.HitGroupTable.SizeInBytes = g_shaderTableEntrySize;
        dispatchDesc.HitGroupTable.StrideInBytes = g_shaderTableEntrySize;
        dispatchDesc.Width = 1280;
        dispatchDesc.Height = 720;
        dispatchDesc.Depth = 1;

        dxrList->DispatchRays(&dispatchDesc);

        // 2. Copy to BackBuffer
        TransitionResource(dxrList.Get(), g_outputUAV.Get(),
                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                           D3D12_RESOURCE_STATE_COPY_SOURCE);
        TransitionResource(dxrList.Get(), g_renderTargets[g_frameIndex].Get(),
                           D3D12_RESOURCE_STATE_PRESENT,
                           D3D12_RESOURCE_STATE_COPY_DEST);
        dxrList->CopyResource(g_renderTargets[g_frameIndex].Get(),
                              g_outputUAV.Get());

        // 3. Prepare for ImGui (RenderTarget state)
        TransitionResource(dxrList.Get(), g_renderTargets[g_frameIndex].Get(),
                           D3D12_RESOURCE_STATE_COPY_DEST,
                           D3D12_RESOURCE_STATE_RENDER_TARGET);
        TransitionResource(dxrList.Get(), g_outputUAV.Get(),
                           D3D12_RESOURCE_STATE_COPY_SOURCE,
                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        g_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
      }
    } else {
      // Rasterization Path
      TransitionResource(
          g_commandList.Get(), g_renderTargets[g_frameIndex].Get(),
          D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

      FLOAT clearColor[] = {0.2f, 0.3f, 0.4f, 1.0f};
      g_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

      g_commandList->SetGraphicsRootSignature(g_rootSignature.Get());
      g_commandList->SetGraphicsRootConstantBufferView(
          0, g_constantBuffer->GetGPUVirtualAddress());

      // Draw demo triangle
      g_commandList->SetPipelineState(g_pipelineState.Get());
      g_commandList->IASetPrimitiveTopology(
          D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      g_commandList->IASetVertexBuffers(0, 1, &g_vertexBufferView);
      g_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
      g_commandList->DrawInstanced(3, 1, 0, 0);

      // Draw loaded meshes
      if (!g_loadedMeshes.empty() && g_meshPipelineState) {
        // ... (Simple iteration of meshes for raster fallback) ...
        const auto &gm = g_loadedMeshes[0];
        g_commandList->SetPipelineState(g_meshPipelineState.Get());
        g_commandList->IASetVertexBuffers(0, 1, &gm.vbView);
        g_commandList->IASetIndexBuffer(&gm.ibView);
        g_commandList->IASetPrimitiveTopology(
            D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12DescriptorHeap *heaps[] = {g_cbvSrvAllocator.Heap()};
        g_commandList->SetDescriptorHeaps(_countof(heaps), heaps);
        if (gm.materialIndex >= 0 &&
            gm.materialIndex < (int)g_loadedMaterials.size()) {
          g_commandList->SetGraphicsRootConstantBufferView(
              2, g_materialConstantBuffer->GetGPUVirtualAddress());
          if (g_textureDescriptorCount > 0)
            g_commandList->SetGraphicsRootDescriptorTable(1,
                                                          g_texturesGpuStart);
        }
        if (gm.ibView.SizeInBytes > 0)
          g_commandList->DrawIndexedInstanced(gm.ibView.SizeInBytes / 4, 1, 0,
                                              0, 0);
      }
    }

    // Render ImGui (Overlay on top of whatever was drawn)
    ID3D12DescriptorHeap *ppHeaps[] = {g_imguiHeap.Get()};
    g_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_commandList.Get());

    // Transition back to present
    TransitionResource(g_commandList.Get(), g_renderTargets[g_frameIndex].Get(),
                       D3D12_RESOURCE_STATE_RENDER_TARGET,
                       D3D12_RESOURCE_STATE_PRESENT);

    ThrowIfFailed(g_commandList->Close());
  };

  auto WaitForPreviousFrame = [&]() {
    const UINT64 currentFenceValue = g_fenceValues[g_frameIndex];
    ThrowIfFailed(g_commandQueue->Signal(g_fence.Get(), currentFenceValue));

    // Update index
    g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();

    // If the next frame is not ready to be rendered yet, wait until it is
    // signaled.
    if (g_fence->GetCompletedValue() < g_fenceValues[g_frameIndex]) {
      ThrowIfFailed(g_fence->SetEventOnCompletion(g_fenceValues[g_frameIndex],
                                                  g_fenceEvent));
      WaitForSingleObject(g_fenceEvent, INFINITE);
    }

    // Set the fence value for the next frame.
    g_fenceValues[g_frameIndex] = currentFenceValue + 1;
  };

  auto RecreateDevice = [&]() {
    // Invalidate ImGui device objects before device reset
    ImGui_ImplDX12_InvalidateDeviceObjects();

    // Release GPU resources
    g_pipelineState.Reset();
    g_rootSignature.Reset();
    g_vertexBuffer.Reset();
    g_constantBuffer.Reset();
    g_commandList.Reset();
    g_imguiHeap.Reset();

    for (UINT i = 0; i < FrameCount; ++i) {
      g_frameResources[i].commandAllocator.Reset();
    }

    // Attempt reinitialization
    if (!InitD3D12(g_hwnd)) {
      MessageBoxA(nullptr, "Failed to recreate D3D12 device.",
                  "Device Recovery", MB_OK | MB_ICONERROR);
      ExitProcess(-1);
    }
  };

  auto CheckDeviceRemoved = [&]() {
    HRESULT reason = g_device->GetDeviceRemovedReason();
    if (FAILED(reason)) {
      // Attempt to recreate the device
      RecreateDevice();
      return true;
    }
    return false;
  };

  while (msg.message != WM_QUIT) {
    if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
      continue;
    }

    // Start ImGui frame
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // Main menu bar: Window toggle + Reset Layout
    if (ImGui::BeginMainMenuBar()) {
      if (ImGui::BeginMenu("Window")) {
        if (ImGui::MenuItem("Assets", NULL, &g_showAssetsWindow)) {
          // toggled via menu
        }
        if (ImGui::MenuItem("Reset Layout")) {
          g_showAssetsWindow = true;
          g_forceUncollapse = true;
        }
        ImGui::EndMenu();
      }
      ImGui::EndMainMenuBar();
    }

    // UI: slider to control X offset
    ImGui::Begin("Controls");
    if (ImGui::SliderFloat("Offset X", &g_offsetX, -1.0f, 1.0f)) {
      // update constant buffer immediately
      struct AlignConstants {
        float offset[4];
      } constants;
      constants.offset[0] = g_offsetX;
      constants.offset[1] = 0.0f;
      constants.offset[2] = 0.0f;
      constants.offset[3] = 0.0f;
      UINT8 *pCbData = nullptr;
      D3D12_RANGE readRange = {0, 0};
      if (SUCCEEDED(g_constantBuffer->Map(
              0, &readRange, reinterpret_cast<void **>(&pCbData)))) {
        memcpy(pCbData, &constants, sizeof(constants));
        g_constantBuffer->Unmap(0, nullptr);
      }
    }
    ImGui::End();

    // Asset loader UI
    // Provide a persistent 'open' toggle and ensure the window starts
    // un-collapsed and a reasonable size
    ImGui::SetNextWindowSize(ImVec2(360, 220), ImGuiCond_FirstUseEver);
    // If user requested a reset, force un-collapse and focus the window this
    // frame
    if (g_forceUncollapse) {
      ImGui::SetNextWindowCollapsed(false, ImGuiCond_Always);
      ImGui::SetNextWindowFocus();
      g_forceUncollapse = false;
    } else {
      ImGui::SetNextWindowCollapsed(false, ImGuiCond_FirstUseEver);
    }
    if (ImGui::Begin("Assets", &g_showAssetsWindow)) {
      ImGui::Columns(2, "asset_cols", false);
      if (ImGui::Button("Load sample.gltf")) {
        // Attempt to load a sample glTF in project folder
        std::vector<Asset::GpuMesh> meshes;
        std::vector<Asset::Material> materials;
        std::vector<Asset::Texture> textures;
        bool ok = Asset::LoadGltf("assets/sample.gltf", meshes, &materials,
                                  &textures);
        if (!ok) {
          g_lastAssetStatus =
              "Load failed: assets/sample.gltf not found or parse error";
          OutputDebugStringA(g_lastAssetStatus.c_str());
        } else {
          // Append to global loaded meshes/materials/textures
          size_t meshBase = g_loadedMeshes.size();
          size_t materialBase = g_loadedMaterials.size();
          size_t textureBase = g_loadedTextures.size();

          g_loadedMeshes.insert(g_loadedMeshes.end(), meshes.begin(),
                                meshes.end());
          g_loadedMaterials.insert(g_loadedMaterials.end(), materials.begin(),
                                   materials.end());
          g_loadedTextures.insert(g_loadedTextures.end(), textures.begin(),
                                  textures.end());

          // Create persistent SRV descriptors for all new textures
          if (!textures.empty()) {
            // Allocate persistent descriptors from frame 0 (they won't be
            // reset)
            DescriptorAllocation alloc =
                g_cbvSrvAllocator.Allocate(0, (UINT)textures.size());
            if (g_textureDescriptorCount == 0) {
              g_texturesGpuStart = alloc.gpu;
            }
            for (size_t i = 0; i < textures.size(); ++i) {
              D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = alloc.cpu;
              cpuHandle.ptr += i * g_device->GetDescriptorHandleIncrementSize(
                                       D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

              const Asset::Texture &tex = textures[i];
              if (tex.resource) {
                D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                srvDesc.Shader4ComponentMapping =
                    D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srvDesc.Format = tex.format;
                srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Texture2D.MipLevels = tex.mipLevels;
                g_device->CreateShaderResourceView(tex.resource.Get(), &srvDesc,
                                                   cpuHandle);
              }
            }
            g_textureDescriptorCount += (UINT)textures.size();
          }

          g_lastAssetStatus = "Loaded and uploaded assets/sample.gltf";
          OutputDebugStringA(g_lastAssetStatus.c_str());

          // Rebuild AS
          BuildAccelerationStructures();
        }
      }

      ImGui::NextColumn();

      // File open button (use native Win32 dialog)
      if (ImGui::Button("Open glTF...")) {
        OPENFILENAMEW ofn = {};
        WCHAR szFile[1024] = {};
        ofn.lStructSize = sizeof(OPENFILENAMEW);
        ofn.hwndOwner = g_hwnd;
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = (DWORD)std::size(szFile);
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
        ofn.lpstrFilter = L"glTF files\0*.gltf;*.glb\0All files\0*.*\0";
        if (GetOpenFileNameW(&ofn)) {
          std::string utf8path = WStringToUtf8(szFile);
          g_selectedAssetPath = utf8path;
          g_lastAssetStatus = std::string("Selected: ") + utf8path;
          OutputDebugStringA(g_lastAssetStatus.c_str());
        } else {
          g_lastAssetStatus = "Open cancelled";
        }
      }
      ImGui::Columns(1);

      if (!g_selectedAssetPath.empty()) {
        ImGui::TextWrapped("Selected: %s", g_selectedAssetPath.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Import Selected")) {
          std::vector<Asset::GpuMesh> meshes;
          std::vector<Asset::Material> materials;
          std::vector<Asset::Texture> textures;
          bool ok = Asset::LoadGltf(g_selectedAssetPath, meshes, &materials,
                                    &textures);
          if (!ok) {
            g_lastAssetStatus = "Import failed: " + g_selectedAssetPath;
            OutputDebugStringA(g_lastAssetStatus.c_str());
          } else {
            size_t meshBase = g_loadedMeshes.size();
            size_t materialBase = g_loadedMaterials.size();
            size_t textureBase = g_loadedTextures.size();

            g_loadedMeshes.insert(g_loadedMeshes.end(), meshes.begin(),
                                  meshes.end());
            g_loadedMaterials.insert(g_loadedMaterials.end(), materials.begin(),
                                     materials.end());
            g_loadedTextures.insert(g_loadedTextures.end(), textures.begin(),
                                    textures.end());

            // Create persistent SRV descriptors for all new textures
            if (!textures.empty()) {
              DescriptorAllocation alloc =
                  g_cbvSrvAllocator.Allocate(0, (UINT)textures.size());
              if (g_textureDescriptorCount == 0) {
                g_texturesGpuStart = alloc.gpu;
              }
              for (size_t i = 0; i < textures.size(); ++i) {
                D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = alloc.cpu;
                cpuHandle.ptr +=
                    i * g_device->GetDescriptorHandleIncrementSize(
                            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

                const Asset::Texture &tex = textures[i];
                if (tex.resource) {
                  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                  srvDesc.Shader4ComponentMapping =
                      D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                  srvDesc.Format = tex.format;
                  srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                  srvDesc.Texture2D.MipLevels = tex.mipLevels;
                  g_device->CreateShaderResourceView(tex.resource.Get(),
                                                     &srvDesc, cpuHandle);
                }
              }
              g_textureDescriptorCount += (UINT)textures.size();
            }

            g_lastAssetStatus = "Imported and uploaded: " + g_selectedAssetPath;
            OutputDebugStringA(g_lastAssetStatus.c_str());
            g_selectedAssetPath.clear();

            // Rebuild AS
            BuildAccelerationStructures();
          }
        }
      }

      if (!g_lastAssetStatus.empty()) {
        ImGui::TextWrapped("Status: %s", g_lastAssetStatus.c_str());
      }

      // Show a simple list of loaded meshes to make panel content visible and
      // prevent accidental collapse confusion
      if (!g_loadedMeshes.empty()) {
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Loaded Meshes",
                                    ImGuiTreeNodeFlags_DefaultOpen)) {
          for (size_t i = 0; i < g_loadedMeshes.size(); ++i) {
            ImGui::Text("Mesh %zu: VB=%p IB=%p", i,
                        (void *)g_loadedMeshes[i].vertexBuffer.Get(),
                        (void *)g_loadedMeshes[i].indexBuffer.Get());
          }
        }
      } else {
        ImGui::TextWrapped("No meshes loaded. Click 'Load sample.gltf' to "
                           "import and upload a mesh.");
      }
    }
    ImGui::End();

    ImGui::Render();

    PopulateCommandList();

    // We can QI.
    ComPtr<ID3D12GraphicsCommandList4> dxrList;
    if (SUCCEEDED(g_commandList.As(&dxrList))) {
      // Bind State
      dxrList->SetPipelineState1(g_rtStateObject.Get());
      dxrList->SetComputeRootSignature(g_rtGlobalRootSignature.Get());

      // Bind Global Root parameters
      // t0: TLAS
      dxrList->SetComputeRootShaderResourceView(
          0, g_tlas.result->GetGPUVirtualAddress());
      // u0: Output (Descriptor Table)
      ID3D12DescriptorHeap *heaps[] = {g_uavHeap.Get()};
      dxrList->SetDescriptorHeaps(1, heaps);
      dxrList->SetComputeRootDescriptorTable(1, g_outputUAVGpuHandle);

      // Dispatch
      D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
      dispatchDesc.RayGenerationShaderRecord.StartAddress = g_rayGenShaderTable;
      dispatchDesc.RayGenerationShaderRecord.SizeInBytes =
          g_shaderTableEntrySize;

      dispatchDesc.MissShaderTable.StartAddress = g_missShaderTable;
      dispatchDesc.MissShaderTable.SizeInBytes = g_shaderTableEntrySize;
      dispatchDesc.MissShaderTable.StrideInBytes = g_shaderTableEntrySize;

      dispatchDesc.HitGroupTable.StartAddress = g_hitGroupShaderTable;
      dispatchDesc.HitGroupTable.SizeInBytes = g_shaderTableEntrySize;
      dispatchDesc.HitGroupTable.StrideInBytes = g_shaderTableEntrySize;

      dispatchDesc.Width = 1280;
      dispatchDesc.Height = 720;
      dispatchDesc.Depth = 1;

      dxrList->DispatchRays(&dispatchDesc);

      // Copy to Backbuffer
      // Transition OutputUAV to CopySrc
      TransitionResource(dxrList.Get(), g_outputUAV.Get(),
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                         D3D12_RESOURCE_STATE_COPY_SOURCE);
      // Transition Backbuffer to CopyDest
      TransitionResource(dxrList.Get(), g_renderTargets[g_frameIndex].Get(),
                         D3D12_RESOURCE_STATE_PRESENT,
                         D3D12_RESOURCE_STATE_COPY_DEST);

      dxrList->CopyResource(g_renderTargets[g_frameIndex].Get(),
                            g_outputUAV.Get());

      // Transition Backbuffer to Present
      TransitionResource(dxrList.Get(), g_renderTargets[g_frameIndex].Get(),
                         D3D12_RESOURCE_STATE_COPY_DEST,
                         D3D12_RESOURCE_STATE_PRESENT);

      // Restore OutputUAV
      TransitionResource(dxrList.Get(), g_outputUAV.Get(),
                         D3D12_RESOURCE_STATE_COPY_SOURCE,
                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
  }
  else {
    // No DXR or not ready - Rasterization path handles it (already populated)
  }

  ID3D12CommandList *ppCommandLists[] = {g_commandList.Get()};
  g_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

  ThrowIfFailed(g_swapChain->Present(1, 0));

  // Signal and increment the fence value.
  const UINT64 currentFenceValue = g_fenceValues[g_frameIndex];
  ThrowIfFailed(g_commandQueue->Signal(g_fence.Get(), currentFenceValue));
  g_fenceValues[g_frameIndex]++;

  // Wait for previous frame
  WaitForPreviousFrame();
}

// Shutdown ImGui and cleanup
ImGui_ImplDX12_Shutdown();
ImGui_ImplWin32_Shutdown();
ImGui::DestroyContext();

// Cleanup fence event
CloseHandle(g_fenceEvent);

return 0;
}
