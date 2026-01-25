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
#include <chrono>
#include <algorithm>

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

// Camera for Ray Tracing
struct CameraCB {
  float pos[3];
  float _pad0;
  float forward[3];
  float _pad1;
  float up[3];
  float _pad2;
  float params[4]; // fov(deg), aspect, znear, zfar
};

// Simple Vec3 helper for CPU-side math
struct Vec3 { float x, y, z; };
static CameraCB g_cameraData = {{0.0f, 0.0f, 2.0f}, 0.0f, {0.0f, 0.0f, -1.0f}, 0.0f, {0.0f, 1.0f, 0.0f}, 0.0f, {45.0f, 1280.0f/720.0f, 0.1f, 1000.0f}};
static ComPtr<ID3D12Resource> g_cameraConstantBuffer;
static float g_camYaw = 0.0f;
static float g_camPitch = 0.0f;
static float g_camSpeed = 2.5f; // units/sec
static POINT g_prevMousePos = {0, 0};
static bool g_mouseCaptured = false;

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

  FILE *log = nullptr;
  fopen_s(&log, "startup.log", "a");
  if (log) {
    fprintf(log, "CreateRT: Compiling shader...\n");
    fclose(log);
  }

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
  // u0: Output UAV descriptor table
  // t1-t16: Texture SRV descriptor table (16 texture slots)
  // b0: Camera CBV
  {
    D3D12_ROOT_PARAMETER params[4] = {};
    // t0 - TLAS
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].Descriptor.RegisterSpace = 0;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    // u0 - output UAV descriptor table
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

    // t1-t16 - texture SRV descriptor table (16 texture slots)
    static D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 16; // baseColor, metallicRoughness, normal, occlusion, emissive + extras
    srvRange.BaseShaderRegister = 1; // Start at t1 (t0 is TLAS)
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges = &srvRange;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // b0 - Camera CBV
    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[3].Descriptor.ShaderRegister = 0;
    params[3].Descriptor.RegisterSpace = 0;
    params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = 4;
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

  fopen_s(&log, "startup.log", "a");
  if (log) {
    fprintf(log, "CreateRT: CreateStateObject...\n");
    fclose(log);
  }

  ThrowIfFailed(g_dxrDevice->CreateStateObject(&stateObjDesc,
                                               IID_PPV_ARGS(&g_rtStateObject)));

  fopen_s(&log, "startup.log", "a");
  if (log) {
    fprintf(log, "CreateRT: StateObject OK\n");
    fclose(log);
  }

  OutputDebugStringA("Ray Tracing Pipeline Created!\n");

  // 4. Create Shader Table
  // Get Properties
  ComPtr<ID3D12StateObjectProperties> properties;
  ThrowIfFailed(g_rtStateObject.As(&properties));

  void *rayGenId = properties->GetShaderIdentifier(L"RayGen");
  void *missId = properties->GetShaderIdentifier(L"Miss");
  void *hitGroupId = properties->GetShaderIdentifier(L"HitGroup");

  fopen_s(&log, "startup.log", "a");
  if (log) {
    fprintf(log, "CreateRT: IDs retrieved: RG=%p, Ms=%p, HG=%p\n", rayGenId,
            missId, hitGroupId);
    fclose(log);
  }

  if (!rayGenId || !missId || !hitGroupId) {
    fopen_s(&log, "startup.log", "a");
    if (log) {
      fprintf(log, "CreateRT: ERROR: One or more Shader IDs are null!\n");
      fclose(log);
    }
    return;
  }

  UINT shaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
  g_shaderTableEntrySize = Align(shaderIdentifierSize,
                                 D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
  UINT shaderTableSize =
      g_shaderTableEntrySize * 3; // 1 RayGen, 1 Miss, 1 HitGroup

  try {
    AllocateUploadBuffer(g_device.Get(), nullptr, shaderTableSize, &g_sbtStorage,
                         L"Shader Table");
  } catch (const std::exception &e) {
    FILE *log = nullptr;
    fopen_s(&log, "startup.log", "a");
    if (log) {
      fprintf(log, "CreateRT: AllocateUploadBuffer exception (SBT): %s\n", e.what());
      fclose(log);
    }
    return;
  }

  // Map and Write
  UINT8 *pData;
  g_sbtStorage->Map(0, nullptr, (void **)&pData);

  memcpy(pData, rayGenId, shaderIdentifierSize); // RayGen at 0
  memcpy(pData + g_shaderTableEntrySize, missId,
         shaderIdentifierSize); // Miss at 1
  memcpy(pData + g_shaderTableEntrySize * 2, hitGroupId,
         shaderIdentifierSize); // Hit at 2

  g_sbtStorage->Unmap(0, nullptr);

  // Log: shader table created
  {
    FILE *log = nullptr;
    fopen_s(&log, "startup.log", "a");
    if (log) {
      fprintf(log, "CreateRT: Shader table uploaded\n");
      fclose(log);
    }
  }

  g_rayGenShaderTable = g_sbtStorage->GetGPUVirtualAddress();
  g_missShaderTable = g_rayGenShaderTable + g_shaderTableEntrySize;
  g_hitGroupShaderTable = g_missShaderTable + g_shaderTableEntrySize;

  // 5. Create Output UAV
  D3D12_DESCRIPTOR_HEAP_DESC uavHeapDesc = {};
  uavHeapDesc.NumDescriptors = 1;
  uavHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  uavHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  HRESULT hrUavHeap = g_device->CreateDescriptorHeap(&uavHeapDesc, IID_PPV_ARGS(&g_uavHeap));
  if (FAILED(hrUavHeap)) {
    FILE *log = nullptr;
    fopen_s(&log, "startup.log", "a");
    if (log) {
      fprintf(log, "CreateRT: CreateDescriptorHeap failed: 0x%08x\n", (unsigned)hrUavHeap);
      fclose(log);
    }
    return;
  }
  {
    FILE *log = nullptr;
    fopen_s(&log, "startup.log", "a");
    if (log) {
      fprintf(log, "CreateRT: UAV descriptor heap created\n");
      fclose(log);
    }
  }

  HRESULT hrAlloc = S_OK;
  try {
    AllocateUAVBuffer(g_device.Get(), 1280 * 720 * 4 * 4, &g_outputUAV,
                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS, L"RT Output");
  } catch (const std::exception &e) {
    FILE *log = nullptr;
    fopen_s(&log, "startup.log", "a");
    if (log) {
      fprintf(log, "CreateRT: AllocateUAVBuffer exception: %s\n", e.what());
      fclose(log);
    }
    return;
  }
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
    HRESULT hrCreate = g_device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
        IID_PPV_ARGS(&g_outputUAV));
    if (FAILED(hrCreate)) {
      FILE *log = nullptr;
      fopen_s(&log, "startup.log", "a");
      if (log) {
        fprintf(log, "CreateRT: CreateCommittedResource failed: 0x%08x\n", (unsigned)hrCreate);
        fclose(log);
      }
      return;
    }
    {
      FILE *log = nullptr;
      fopen_s(&log, "startup.log", "a");
      if (log) {
        fprintf(log, "CreateRT: Output texture created\n");
        fclose(log);
      }
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0;
    uavDesc.Texture2D.PlaneSlice = 0;

    HRESULT hrUAV = S_OK;
    if (g_uavHeap) {
      g_device->CreateUnorderedAccessView(
          g_outputUAV.Get(), nullptr, &uavDesc,
          g_uavHeap->GetCPUDescriptorHandleForHeapStart());
      g_outputUAVGpuHandle = g_uavHeap->GetGPUDescriptorHandleForHeapStart();
      FILE *log = nullptr;
      fopen_s(&log, "startup.log", "a");
      if (log) {
        fprintf(log, "CreateRT: UAV view created\n");
        fclose(log);
      }
    } else {
      FILE *log = nullptr;
      fopen_s(&log, "startup.log", "a");
      if (log) {
        fprintf(log, "CreateRT: g_uavHeap is null, cannot create UAV view\n");
        fclose(log);
      }
      return;
    }
  }
}

// Helper to build AS for all loaded meshes
// This is a naive implementation that rebuilds everything from scratch
void BuildAccelerationStructures() {
  // Log entry
  {
    FILE *log = nullptr;
    if (fopen_s(&log, "startup.log", "a") == 0 && log) {
      fprintf(log, "BuildAS: Entering BuildAccelerationStructures (meshes=%zu)\n", g_loadedMeshes.size());
      fclose(log);
    }
  }

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

  // Log completion
  {
    FILE *log = nullptr;
    if (fopen_s(&log, "startup.log", "a") == 0 && log) {
      fprintf(log, "BuildAS: Completed BuildAccelerationStructures\n");
      fclose(log);
    }
  }
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

// Helper to find shader file with fallback paths
static std::wstring FindShaderFile(const wchar_t *relativePath) {
  // Try multiple possible locations
  std::vector<std::wstring> searchPaths;
  searchPaths.push_back(relativePath); // Current directory
  searchPaths.push_back(std::wstring(L"..\\..\\") +
                        relativePath); // From build/Release/
  searchPaths.push_back(std::wstring(L"..\\") +
                        relativePath); // From build directory

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

static void ExecuteCommandListAndWait(ID3D12GraphicsCommandList* cmdList)
{
    ThrowIfFailed(cmdList->Close());
    ID3D12CommandList* lists[] = { cmdList };
    g_commandQueue->ExecuteCommandLists(1, lists);
    
    // Wait for completion
    ComPtr<ID3D12Fence> fence;
    ThrowIfFailed(g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    ThrowIfFailed(g_commandQueue->Signal(fence.Get(), 1));
    if (fence->GetCompletedValue() < 1) {
        ThrowIfFailed(fence->SetEventOnCompletion(1, event));
        WaitForSingleObject(event, INFINITE);
    }
    CloseHandle(event);
}

void CreateTestTexture() {
  // Create a simple 2x2 checkerboard texture for testing
  const UINT width = 2;
  const UINT height = 2;
  const UINT pixelSize = 4; // RGBA
  BYTE textureData[width * height * pixelSize] = {
    255, 0, 0, 255,    // Red
    0, 255, 0, 255,    // Green
    0, 0, 255, 255,    // Blue
    255, 255, 0, 255   // Yellow
  };

  D3D12_HEAP_PROPERTIES uploadHeapProps = {};
  uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

  D3D12_RESOURCE_DESC uploadDesc = {};
  uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  uploadDesc.Width = width * height * pixelSize;
  uploadDesc.Height = 1;
  uploadDesc.DepthOrArraySize = 1;
  uploadDesc.MipLevels = 1;
  uploadDesc.SampleDesc.Count = 1;
  uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

  ComPtr<ID3D12Resource> uploadBuffer;
  ThrowIfFailed(g_device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &uploadDesc,
                                                 D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                 IID_PPV_ARGS(&uploadBuffer)));

  // Copy texture data to upload buffer
  void* mappedData = nullptr;
  uploadBuffer->Map(0, nullptr, &mappedData);
  memcpy(mappedData, textureData, sizeof(textureData));
  uploadBuffer->Unmap(0, nullptr);

  // Create the texture resource
  D3D12_HEAP_PROPERTIES defaultHeapProps = {};
  defaultHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

  D3D12_RESOURCE_DESC textureDesc = {};
  textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  textureDesc.Width = width;
  textureDesc.Height = height;
  textureDesc.DepthOrArraySize = 1;
  textureDesc.MipLevels = 1;
  textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  textureDesc.SampleDesc.Count = 1;
  textureDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

  ComPtr<ID3D12Resource> texture;
  ThrowIfFailed(g_device->CreateCommittedResource(&defaultHeapProps, D3D12_HEAP_FLAG_NONE, &textureDesc,
                                                 D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                 IID_PPV_ARGS(&texture)));

  // Allocate descriptor for the texture
  DescriptorAllocation alloc = g_cbvSrvAllocator.Allocate(0, 1);
  if (g_textureDescriptorCount == 0) {
    g_texturesGpuStart = alloc.gpu;
  }

  // Create SRV
  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Texture2D.MipLevels = 1;
  g_device->CreateShaderResourceView(texture.Get(), &srvDesc, alloc.cpu);

  // Copy from upload buffer to texture
  ComPtr<ID3D12CommandAllocator> cmdAlloc;
  ComPtr<ID3D12GraphicsCommandList> cmdList;
  ThrowIfFailed(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAlloc)));
  ThrowIfFailed(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc.Get(), nullptr, IID_PPV_ARGS(&cmdList)));

  D3D12_TEXTURE_COPY_LOCATION dst = {};
  dst.pResource = texture.Get();
  dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  dst.SubresourceIndex = 0;

  D3D12_TEXTURE_COPY_LOCATION src = {};
  src.pResource = uploadBuffer.Get();
  src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  src.PlacedFootprint.Offset = 0;
  src.PlacedFootprint.Footprint.Width = width;
  src.PlacedFootprint.Footprint.Height = height;
  src.PlacedFootprint.Footprint.Depth = 1;
  src.PlacedFootprint.Footprint.RowPitch = width * pixelSize;
  src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

  cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

  // Transition to shader resource
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = texture.Get();
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  cmdList->ResourceBarrier(1, &barrier);

  ExecuteCommandListAndWait(cmdList.Get());

  // Store the texture
  Asset::Texture testTex;
  testTex.resource = texture;
  testTex.width = width;
  testTex.height = height;
  testTex.format = DXGI_FORMAT_R8G8B8A8_UNORM;
  testTex.mipLevels = 1;
  g_loadedTextures.push_back(testTex);
  g_textureDescriptorCount = 1;

  FILE *log = nullptr;
  if (fopen_s(&log, "startup.log", "a") == 0 && log) {
    fprintf(log, "CreateTestTexture: Created 2x2 checkerboard texture\n");
    fclose(log);
  }
}

bool InitD3D12(HWND hwnd) {

  g_hwnd = hwnd;
  UINT dxgiFactoryFlags = 0;

  EnableD3D12DebugLayer();

  ComPtr<IDXGIFactory4> factory;
  ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

  // Try to find a hardware adapter that supports D3D12
  ComPtr<IDXGIAdapter1> hardwareAdapter;
  GetHardwareAdapter(factory.Get(), &hardwareAdapter);

  FILE *log = nullptr;
  fopen_s(&log, "startup.log", "a");
  if (log) {
    fprintf(log, "InitD3D12: Hardware adapter obtained\n");
    fclose(log);
  }

  HRESULT hr = E_FAIL;
  if (hardwareAdapter) {
    hr = D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0,
                           IID_PPV_ARGS(&g_device));
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
          // Log: CreateRayTracingPipeline finished
          {
            FILE *log = nullptr;
            fopen_s(&log, "startup.log", "a");
            if (log) {
              fprintf(log, "InitD3D12: CreateRayTracingPipeline finished\n");
              fclose(log);
            }
          }
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

    if (FAILED(hr)) {
      ComPtr<IDXGIAdapter> warpAdapter;
      ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));
      ThrowIfFailed(D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                      IID_PPV_ARGS(&g_device)));
    }
  }

  // Log: about to create command queue
  {
    FILE *log = nullptr;
    fopen_s(&log, "startup.log", "a");
    if (log) {
      fprintf(log, "InitD3D12: Before CreateCommandQueue\n");
      fclose(log);
    }
  }

  // Create command queue
  D3D12_COMMAND_QUEUE_DESC queueDesc = {};
  queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

  hr = g_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&g_commandQueue));
  if (FAILED(hr)) {
    FILE *log = nullptr;
    fopen_s(&log, "startup.log", "a");
    if (log) {
      fprintf(log, "InitD3D12: CreateCommandQueue failed 0x%08x\n", (unsigned)hr);
      fclose(log);
    }
    return false;
  }

  // Log: command queue created
  {
    FILE *log = nullptr;
    fopen_s(&log, "startup.log", "a");
    if (log) {
      fprintf(log, "InitD3D12: CreateCommandQueue succeeded\n");
      fclose(log);
    }
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
  {
    FILE *log = nullptr;
    fopen_s(&log, "startup.log", "a");
    if (log) {
      fprintf(log, "InitD3D12: Before CreateSwapChainForHwnd\n");
      fclose(log);
    }
  }

  HRESULT hrSwap = factory->CreateSwapChainForHwnd(g_commandQueue.Get(), hwnd,
                                                   &swapChainDesc, nullptr,
                                                   nullptr, &swapChain1);
  if (FAILED(hrSwap)) {
    FILE *log = nullptr;
    fopen_s(&log, "startup.log", "a");
    if (log) {
      fprintf(log, "InitD3D12: CreateSwapChainForHwnd failed 0x%08x\n", (unsigned)hrSwap);
      fclose(log);
    }
    return false;
  }

  HRESULT hrAs = swapChain1.As(&g_swapChain);
  if (FAILED(hrAs)) {
    FILE *log = nullptr;
    fopen_s(&log, "startup.log", "a");
    if (log) {
      fprintf(log, "InitD3D12: swapChain1.As failed 0x%08x\n", (unsigned)hrAs);
      fclose(log);
    }
    return false;
  }

  // Log: swap chain created
  {
    FILE *log = nullptr;
    fopen_s(&log, "startup.log", "a");
    if (log) {
      fprintf(log, "InitD3D12: Swap chain created\n");
      fclose(log);
    }
  }

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
    HRESULT hrBuf = g_swapChain->GetBuffer(i, IID_PPV_ARGS(&g_renderTargets[i]));
    if (FAILED(hrBuf)) {
      FILE *log = nullptr;
      fopen_s(&log, "startup.log", "a");
      if (log) {
        fprintf(log, "InitD3D12: GetBuffer failed for index %u: 0x%08x\n", i, (unsigned)hrBuf);
        fclose(log);
      }
      return false;
    }
    g_device->CreateRenderTargetView(g_renderTargets[i].Get(), nullptr,
                                     rtvHandle);
    rtvHandle.ptr =
        rtvHandleStart.ptr + (SIZE_T)((i + 1) * g_rtvDescriptorSize);
  }

  // Log: RTVs created
  {
    FILE *log = nullptr;
    fopen_s(&log, "startup.log", "a");
    if (log) {
      fprintf(log, "InitD3D12: RTVs created\n");
      fclose(log);
    }
  }

  // Log: before CBV/SRV allocator init
  {
    FILE *log = nullptr;
    fopen_s(&log, "startup.log", "a");
    if (log) {
      fprintf(log, "InitD3D12: Before CBV/SRV allocator Init\n");
      fclose(log);
    }
  }
  // Initialize descriptor allocator for CBV/SRV/UAV
  g_cbvSrvAllocator.Init(g_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                         1024, FrameCount);
  {
    FILE *log = nullptr;
    fopen_s(&log, "startup.log", "a");
    if (log) {
      fprintf(log, "InitD3D12: CBV/SRV allocator initialized\n");
      fclose(log);
    }
  }

  // Create descriptor heap for ImGui fonts (1 descriptor)
  D3D12_DESCRIPTOR_HEAP_DESC imguiHeapDesc = {};
  imguiHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  imguiHeapDesc.NumDescriptors = 1;
  imguiHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  HRESULT hrImg = g_device->CreateDescriptorHeap(&imguiHeapDesc,
                                               IID_PPV_ARGS(&g_imguiHeap));
  if (FAILED(hrImg)) {
    FILE *log = nullptr;
    fopen_s(&log, "startup.log", "a");
    if (log) {
      fprintf(log, "InitD3D12: CreateDescriptorHeap for ImGui failed 0x%08x\n", (unsigned)hrImg);
      fclose(log);
    }
    return false;
  }
  {
    FILE *log = nullptr;
    fopen_s(&log, "startup.log", "a");
    if (log) {
      fprintf(log, "InitD3D12: ImGui descriptor heap created\n");
      fclose(log);
    }
  }

  // Create per-frame resources (command allocators + fence values)
  for (UINT i = 0; i < FrameCount; ++i) {
    HRESULT hrAlloc = g_device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&g_frameResources[i].commandAllocator));
    if (FAILED(hrAlloc)) {
      FILE *log = nullptr;
      fopen_s(&log, "startup.log", "a");
      if (log) {
        fprintf(log, "InitD3D12: CreateCommandAllocator failed for frame %u: 0x%08x\n", i, (unsigned)hrAlloc);
        fclose(log);
      }
      return false;
    }
    g_frameResources[i].fenceValue = 0;
    g_frameResources[i].transientDescriptorOffset = 0;
  }
  {
    FILE *log = nullptr;
    fopen_s(&log, "startup.log", "a");
    if (log) {
      fprintf(log, "InitD3D12: per-frame command allocators created\n");
      fclose(log);
    }
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

  // --- Compile simple shaders for demo triangle (DXC / SM6) ---
  ComPtr<IDxcBlob> vsBlob;
  ComPtr<IDxcBlob> psBlob;
  std::wstring simpleShaderPath = FindShaderFile(L"shaders\\simple.hlsl");
  {
    char debugMsg[512];
    sprintf_s(debugMsg, "Loading simple shader from: %ls\n",
              simpleShaderPath.c_str());
    OutputDebugStringA(debugMsg);
  }
  try {
    vsBlob = g_dxcHelper.Compile(simpleShaderPath, L"VSMain", L"vs_6_0");
  } catch (const std::exception &e) {
    FILE *log = nullptr;
    if (fopen_s(&log, "startup.log", "a") == 0 && log) {
      fprintf(log, "InitD3D12: VS compile failed (DXC) for %ls: %s\n", simpleShaderPath.c_str(), e.what());
      fclose(log);
    }
    return false;
  }
  try {
    psBlob = g_dxcHelper.Compile(simpleShaderPath, L"PSMain", L"ps_6_0");
  } catch (const std::exception &e) {
    FILE *log = nullptr;
    if (fopen_s(&log, "startup.log", "a") == 0 && log) {
      fprintf(log, "InitD3D12: PS compile failed (DXC) for %ls: %s\n", simpleShaderPath.c_str(), e.what());
      fclose(log);
    }
    return false;
  }

  // --- Compile mesh PBR shaders (full vertex layout + PBR material pixel shader) using DXC (SM6) ---
  ComPtr<IDxcBlob> vsMeshBlob;
  ComPtr<IDxcBlob> psMeshBlob;
  std::wstring pbrShaderPath = FindShaderFile(L"shaders\\pbr_mesh.hlsl");
  {
    char debugMsg[512];
    sprintf_s(debugMsg, "Loading PBR shader from: %ls\n",
              pbrShaderPath.c_str());
    OutputDebugStringA(debugMsg);
  }
  try {
    vsMeshBlob = g_dxcHelper.Compile(pbrShaderPath, L"VSMainMesh", L"vs_6_0");
  } catch (const std::exception &e) {
    FILE *log = nullptr;
    if (fopen_s(&log, "startup.log", "a") == 0 && log) {
      fprintf(log, "InitD3D12: VS compile failed (DXC) for %ls: %s\n", pbrShaderPath.c_str(), e.what());
      fclose(log);
    }
    return false;
  }
  try {
    psMeshBlob = g_dxcHelper.Compile(pbrShaderPath, L"PSMainMesh", L"ps_6_0");
  } catch (const std::exception &e) {
    FILE *log = nullptr;
    if (fopen_s(&log, "startup.log", "a") == 0 && log) {
      fprintf(log, "InitD3D12: PS compile failed (DXC) for %ls: %s\n", pbrShaderPath.c_str(), e.what());
      fclose(log);
    }
    return false;
  }

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

  // Log: reached post-Create pipeline initialization
  {
    FILE *log = nullptr;
    fopen_s(&log, "startup.log", "a");
    if (log) {
      fprintf(log, "InitD3D12: Post-pipeline initialization reached\n");
      fclose(log);
    }
  }

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

  // Allocate camera constant buffer (upload heap)
  {
    const UINT64 camCbSize = (sizeof(CameraCB) + 255) & ~255;
    D3D12_RESOURCE_DESC cbDesc = {};
    cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    cbDesc.Width = camCbSize;
    cbDesc.Height = 1;
    cbDesc.DepthOrArraySize = 1;
    cbDesc.MipLevels = 1;
    cbDesc.SampleDesc.Count = 1;
    cbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ThrowIfFailed(g_device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &cbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&g_cameraConstantBuffer)));

    // Initialize with default camera
    UINT8 *pCamData = nullptr;
    D3D12_RANGE readRange = {0, 0};
    ThrowIfFailed(g_cameraConstantBuffer->Map(0, &readRange, reinterpret_cast<void **>(&pCamData)));
    memcpy(pCamData, &g_cameraData, sizeof(g_cameraData));
    g_cameraConstantBuffer->Unmap(0, nullptr);
  }

  // Log successful InitD3D12 completion
  {
    FILE *log = nullptr;
    if (fopen_s(&log, "startup.log", "a") == 0 && log) {
      fprintf(log, "InitD3D12: Completed OK (returning true)\n");
      fclose(log);
    }
  }

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

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int nCmdShow) {
  // Log startup
  FILE *startLog = nullptr;
  if (fopen_s(&startLog, "startup.log", "w") == 0 && startLog) {
    fprintf(startLog, "Application starting...\n");
    fclose(startLog);
  }

  // Parse command line for custom glTF file
  std::string customGltfPath;
  if (lpCmdLine && *lpCmdLine) {
    customGltfPath = lpCmdLine;
    // Remove quotes if present
    if (!customGltfPath.empty() && customGltfPath.front() == '"') {
      customGltfPath = customGltfPath.substr(1);
    }
    if (!customGltfPath.empty() && customGltfPath.back() == '"') {
      customGltfPath = customGltfPath.substr(0, customGltfPath.size() - 1);
    }
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

  // Log that we showed the window
  FILE *logShow = nullptr;
  if (fopen_s(&logShow, "startup.log", "a") == 0 && logShow) {
    fprintf(logShow, "ShowWindow called\n");
    fclose(logShow);
  }

  if (!InitD3D12(hwnd)) {
    MessageBoxA(nullptr, "Failed to initialize D3D12", "Error",
                MB_OK | MB_ICONERROR);
    return -1;
  }

  // Log successful D3D12 initialization
  FILE *logInit = nullptr;
  if (fopen_s(&logInit, "startup.log", "a") == 0 && logInit) {
    fprintf(logInit, "InitD3D12 returned OK\n");
    fclose(logInit);
  }

  // Auto-load sample glTF at startup (for automated DXR testing)
  // Use command line argument if provided, otherwise use default
  std::string autoLoadPath = customGltfPath.empty() ? "assets/sample.glb" : customGltfPath;
  try {
    if (fs::exists(autoLoadPath)) {
      std::vector<Asset::GpuMesh> meshes;
      std::vector<Asset::Material> materials;
      std::vector<Asset::Texture> textures;
      bool ok = Asset::LoadGltf(autoLoadPath, meshes, &materials, &textures);
      FILE *logAuto = nullptr;
      if (fopen_s(&logAuto, "startup.log", "a") == 0 && logAuto) {
        if (!ok) {
          fprintf(logAuto, "AutoLoad: failed to load %s\n", autoLoadPath.c_str());
        } else {
          fprintf(logAuto, "AutoLoad: loaded %s (meshes=%zu, materials=%zu, textures=%zu)\n",
                  autoLoadPath.c_str(), meshes.size(), materials.size(), textures.size());
        }
        fclose(logAuto);
      }

      if (ok) {
        size_t meshBase = g_loadedMeshes.size();
        size_t materialBase = g_loadedMaterials.size();
        size_t textureBase = g_loadedTextures.size();

        g_loadedMeshes.insert(g_loadedMeshes.end(), meshes.begin(), meshes.end());
        g_loadedMaterials.insert(g_loadedMaterials.end(), materials.begin(), materials.end());
        g_loadedTextures.insert(g_loadedTextures.end(), textures.begin(), textures.end());

        if (!textures.empty()) {
          // Allocate persistent descriptors from frame 0
          DescriptorAllocation alloc = g_cbvSrvAllocator.Allocate(0, (UINT)textures.size());
          if (g_textureDescriptorCount == 0) {
            g_texturesGpuStart = alloc.gpu;
          }
          for (size_t i = 0; i < textures.size(); ++i) {
            D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = alloc.cpu;
            cpuHandle.ptr += i * g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            const Asset::Texture &tex = textures[i];
            if (tex.resource) {
              D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
              srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
              srvDesc.Format = tex.format;
              srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
              srvDesc.Texture2D.MipLevels = tex.mipLevels;
              g_device->CreateShaderResourceView(tex.resource.Get(), &srvDesc, cpuHandle);
            }
          }
          g_textureDescriptorCount += (UINT)textures.size();
        }

        // If no textures were loaded, create a simple 2x2 checkerboard texture for testing
        if (textures.empty()) {
          CreateTestTexture();
        }

        // Rebuild AS to exercise DXR path
        FILE *logBuild = nullptr;
        if (fopen_s(&logBuild, "startup.log", "a") == 0 && logBuild) {
          fprintf(logBuild, "AutoLoad: calling BuildAccelerationStructures()\n");
          fclose(logBuild);
        }
        BuildAccelerationStructures();
      }
    } else {
      FILE *logNoSample = nullptr;
      if (fopen_s(&logNoSample, "startup.log", "a") == 0 && logNoSample) {
        fprintf(logNoSample, "AutoLoad: %s not found - creating synthetic test mesh\n", autoLoadPath.c_str());
        fclose(logNoSample);
      }

      // Create a cube mesh (centered at origin, size 1.0) to exercise TLAS/DispatchRays
      try {
        Asset::GpuMesh gm;
        Asset::Vertex cubeVerts[8] = {
            {{-0.5f, -0.5f, -0.5f}, {0,0,-1}, {1,0,0,1}, {0.0f, 0.0f}},
            {{ 0.5f, -0.5f, -0.5f}, {0,0,-1}, {1,0,0,1}, {1.0f, 0.0f}},
            {{ 0.5f,  0.5f, -0.5f}, {0,0,-1}, {1,0,0,1}, {1.0f, 1.0f}},
            {{-0.5f,  0.5f, -0.5f}, {0,0,-1}, {1,0,0,1}, {0.0f, 1.0f}},

            {{-0.5f, -0.5f,  0.5f}, {0,0,1}, {1,0,0,1}, {0.0f, 0.0f}},
            {{ 0.5f, -0.5f,  0.5f}, {0,0,1}, {1,0,0,1}, {1.0f, 0.0f}},
            {{ 0.5f,  0.5f,  0.5f}, {0,0,1}, {1,0,0,1}, {1.0f, 1.0f}},
            {{-0.5f,  0.5f,  0.5f}, {0,0,1}, {1,0,0,1}, {0.0f, 1.0f}},
        };
        // 12 triangles (36 indices)
        UINT indices[36] = {
          // back face
          0,1,2, 0,2,3,
          // front face
          4,6,5, 4,7,6,
          // left
          4,0,3, 4,3,7,
          // right
          1,5,6, 1,6,2,
          // bottom
          4,5,1, 4,1,0,
          // top
          3,2,6, 3,6,7
        };

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC vbDesc = {};
        vbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        vbDesc.Width = sizeof(cubeVerts);
        vbDesc.Height = 1;
        vbDesc.DepthOrArraySize = 1;
        vbDesc.MipLevels = 1;
        vbDesc.SampleDesc.Count = 1;
        vbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ThrowIfFailed(g_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &vbDesc,
                                                       D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                       IID_PPV_ARGS(&gm.vertexBuffer)));

        // copy vertex data
        UINT8 *pData = nullptr;
        D3D12_RANGE readRange = {0,0};
        ThrowIfFailed(gm.vertexBuffer->Map(0, &readRange, reinterpret_cast<void **>(&pData)));
        memcpy(pData, cubeVerts, sizeof(cubeVerts));
        gm.vertexBuffer->Unmap(0, nullptr);

        gm.vbView.BufferLocation = gm.vertexBuffer->GetGPUVirtualAddress();
        gm.vbView.StrideInBytes = sizeof(Asset::Vertex);
        gm.vbView.SizeInBytes = sizeof(cubeVerts);

        D3D12_RESOURCE_DESC ibDesc = vbDesc;
        ibDesc.Width = sizeof(indices);
        ThrowIfFailed(g_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &ibDesc,
                                                       D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                       IID_PPV_ARGS(&gm.indexBuffer)));
        pData = nullptr;
        ThrowIfFailed(gm.indexBuffer->Map(0, &readRange, reinterpret_cast<void **>(&pData)));
        memcpy(pData, indices, sizeof(indices));
        gm.indexBuffer->Unmap(0, nullptr);

        gm.ibView.BufferLocation = gm.indexBuffer->GetGPUVirtualAddress();
        gm.ibView.Format = DXGI_FORMAT_R32_UINT;
        gm.ibView.SizeInBytes = sizeof(indices);

        gm.vertexCount = (UINT)std::size(cubeVerts);
        gm.indexCount = (UINT)std::size(indices);
        gm.materialIndex = -1;

        g_loadedMeshes.push_back(gm);

        FILE *logSynth = nullptr;
        if (fopen_s(&logSynth, "startup.log", "a") == 0 && logSynth) {
          fprintf(logSynth, "AutoLoad: Added synthetic cube mesh\n");
          fclose(logSynth);
        }

        // Build AS for synthetic mesh
        BuildAccelerationStructures();

      } catch (const std::exception &e2) {
        FILE *logErr = nullptr;
        if (fopen_s(&logErr, "startup.log", "a") == 0 && logErr) {
          fprintf(logErr, "AutoLoad: exception creating synthetic mesh: %s\n", e2.what());
          fclose(logErr);
        }
      }
    }
  } catch (const std::exception &e) {
    FILE *logEx = nullptr;
    if (fopen_s(&logEx, "startup.log", "a") == 0 && logEx) {
      fprintf(logEx, "AutoLoad: exception: %s\n", e.what());
      fclose(logEx);
    }
  }

  // Basic message loop + simple render
  MSG msg = {};

  auto PopulateCommandList = [&]() {
    FILE *log = nullptr;
    fopen_s(&log, "frame.log", "a");
    if (log) {
      fprintf(log, "PopulateCommandList start\n");
      fclose(log);
    }

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
      log = nullptr;
      fopen_s(&log, "frame.log", "a");
      if (log) {
        fprintf(log, "Entering DXR Path\n");
        fclose(log);
      }
      ComPtr<ID3D12GraphicsCommandList4> dxrList;
      if (SUCCEEDED(g_commandList.As(&dxrList))) {
        // 1. Dispatch Rays
        log = nullptr;
        fopen_s(&log, "frame.log", "a");
        if (log) {
          fprintf(log, "Setting Pipeline State\n");
          fclose(log);
        }
        dxrList->SetPipelineState1(g_rtStateObject.Get());
        dxrList->SetComputeRootSignature(g_rtGlobalRootSignature.Get());
        dxrList->SetComputeRootShaderResourceView(
            0, g_tlas.result->GetGPUVirtualAddress());
        ID3D12DescriptorHeap *heaps[] = {g_uavHeap.Get()};
        dxrList->SetDescriptorHeaps(1, heaps);
        dxrList->SetComputeRootDescriptorTable(1, g_outputUAVGpuHandle);

        // Set texture descriptor table (root parameter 2) if textures are available
        // if (g_textureDescriptorCount > 0) {
        //     dxrList->SetComputeRootDescriptorTable(2, g_texturesGpuStart);
        // }

        // Set camera constant buffer view (root parameter 3)
        if (g_cameraConstantBuffer) {
          dxrList->SetComputeRootConstantBufferView(3, g_cameraConstantBuffer->GetGPUVirtualAddress());
        }

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

        log = nullptr;
        fopen_s(&log, "frame.log", "a");
        if (log) {
          fprintf(log, "Calling DispatchRays\n");
          fclose(log);
        }
        dxrList->DispatchRays(&dispatchDesc);
        log = nullptr;
        fopen_s(&log, "frame.log", "a");
        if (log) {
          fprintf(log, "DispatchRays returned\n");
          fclose(log);
        }

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
      ExitProcess(static_cast<UINT>(-1));
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

  // Log entering main loop
  {
    FILE *logMain = nullptr;
    if (fopen_s(&logMain, "startup.log", "a") == 0 && logMain) {
      fprintf(logMain, "Entering main loop\n");
      fclose(logMain);
    }
  }

  // Setup timing for camera movement
  static auto prevTime = std::chrono::high_resolution_clock::now();

  while (msg.message != WM_QUIT) {
    if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
      continue;
    }

    // Compute delta time
    auto now = std::chrono::high_resolution_clock::now();
    std::chrono::duration<float> dtDur = now - prevTime;
    float dt = dtDur.count();
    prevTime = now;

    // Handle mouse rotation when RMB is pressed
    if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) {
      POINT curPos;
      GetCursorPos(&curPos);
      ScreenToClient(g_hwnd, &curPos);
      if (!g_mouseCaptured) {
        g_prevMousePos = curPos;
        g_mouseCaptured = true;
      }
      int dx = curPos.x - g_prevMousePos.x;
      int dy = curPos.y - g_prevMousePos.y;
      const float sensitivity = 0.12f; // degrees per pixel
      g_camYaw += dx * sensitivity * (3.14159265f / 180.0f);
      // Inverted Y: subtract dy so moving mouse up looks up
      g_camPitch -= dy * sensitivity * (3.14159265f / 180.0f);
      // clamp pitch
      g_camPitch = max(-1.5f, min(1.5f, g_camPitch));
      g_prevMousePos = curPos;
    } else {
      // release capture
      g_mouseCaptured = false;
      GetCursorPos(&g_prevMousePos);
      ScreenToClient(g_hwnd, &g_prevMousePos);
    }

    // Movement: WASD
    float moveSpeed = g_camSpeed;
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
      moveSpeed *= 3.0f;

    // Build forward/right vectors
    Vec3 camF = {g_cameraData.forward[0], g_cameraData.forward[1], g_cameraData.forward[2]};
    Vec3 camU = {g_cameraData.up[0], g_cameraData.up[1], g_cameraData.up[2]};
    // rotate forward by yaw/pitch
    {
      float cp = cosf(g_camPitch);
      float sp = sinf(g_camPitch);
      float cy = cosf(g_camYaw);
      float sy = sinf(g_camYaw);
      camF.x = cp * sy;
      camF.y = sp;
      camF.z = cp * -cy;
    }
    // compute right
    Vec3 camR = { camF.z * camU.y - camF.y * camU.z,
                  camF.x * camU.z - camF.z * camU.x,
                  camF.y * camU.x - camF.x * camU.y };
    // normalize
    auto normalize3 = [](Vec3 &v){ float l = sqrtf(v.x*v.x+v.y*v.y+v.z*v.z); if (l>0.00001f){ v.x/=l; v.y/=l; v.z/=l;} };
    normalize3(camF); normalize3(camR);

    Vec3 move = {0,0,0};
    // W/S forward/back
    if (GetAsyncKeyState('W') & 0x8000) { move.x += camF.x; move.y += camF.y; move.z += camF.z; }
    if (GetAsyncKeyState('S') & 0x8000) { move.x -= camF.x; move.y -= camF.y; move.z -= camF.z; }
    // Swapped: A now moves RIGHT, D now moves LEFT
    if (GetAsyncKeyState('A') & 0x8000) { move.x += camR.x; move.y += camR.y; move.z += camR.z; }
    if (GetAsyncKeyState('D') & 0x8000) { move.x -= camR.x; move.y -= camR.y; move.z -= camR.z; }
    // Vertical movement: E up, Q down
    if (GetAsyncKeyState('E') & 0x8000) { move.x += camU.x; move.y += camU.y; move.z += camU.z; }
    if (GetAsyncKeyState('Q') & 0x8000) { move.x -= camU.x; move.y -= camU.y; move.z -= camU.z; }

    if (move.x != 0 || move.y != 0 || move.z != 0) {
      normalize3(move);
      g_cameraData.pos[0] += move.x * moveSpeed * dt;
      g_cameraData.pos[1] += move.y * moveSpeed * dt;
      g_cameraData.pos[2] += move.z * moveSpeed * dt;
    }

    // Update camera forward from yaw/pitch
    g_cameraData.forward[0] = (cosf(g_camPitch) * sinf(g_camYaw));
    g_cameraData.forward[1] = sinf(g_camPitch);
    g_cameraData.forward[2] = (cosf(g_camPitch) * -cosf(g_camYaw));

    // Update camera CB on GPU
    if (g_cameraConstantBuffer) {
      UINT8 *pCam = nullptr;
      D3D12_RANGE readRange = {0,0};
      if (SUCCEEDED(g_cameraConstantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pCam)))) {
        memcpy(pCam, &g_cameraData, sizeof(g_cameraData));
        g_cameraConstantBuffer->Unmap(0, nullptr);
      }
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
      if (ImGui::Button("Load sample.glb")) {
        // Attempt to load a sample glTF in project folder
        std::vector<Asset::GpuMesh> meshes;
        std::vector<Asset::Material> materials;
        std::vector<Asset::Texture> textures;
        bool ok = Asset::LoadGltf("assets/sample.glb", meshes, &materials,
                                  &textures);
        if (!ok) {
          g_lastAssetStatus =
              "Load failed: assets/sample.glb not found or parse error";
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

          g_lastAssetStatus = "Loaded and uploaded assets/sample.glb";
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

    ID3D12CommandList *ppCommandLists[] = {g_commandList.Get()};
    g_commandQueue->ExecuteCommandLists(_countof(ppCommandLists),
                                        ppCommandLists);

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
