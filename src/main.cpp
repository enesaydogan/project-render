#include "ImGuizmo.h"
#include "assets/asset_loader.h"
#include "clouds.h" // Add clouds
#include "d3d12_helpers.h"
#include "dx12_context.h"
#include "dxc_wrapper.h"
#include "dxr_helpers.h"
#include "dxr_renderer.h"
#include "editor_ui.h"
#include "file_import.h"
#include "ibl_manager.h"
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"
#include "imgui_theme.h"
#include "input_handler.h"
#include "light.h"
#include "material_editor.h"
#include "oidn_denoiser.h"
#include "raster_renderer.h"
#include "scene.h"
#include "scene_io.h"
#include <algorithm>
#include <chrono>
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

// Top-level exception handler for debug builds.
#ifdef _DEBUG
static LONG WINAPI TopLevelExceptionHandler(EXCEPTION_POINTERS *ep) {
  if (ep && ep->ExceptionRecord) {
    fprintf(stderr, "TopLevelExceptionHandler: code=0x%08x at IP=0x%p\n",
            (unsigned)ep->ExceptionRecord->ExceptionCode,
            ep->ExceptionRecord->ExceptionAddress);
  } else {
    fprintf(stderr, "TopLevelExceptionHandler: called with null record\n");
  }
  return EXCEPTION_EXECUTE_HANDLER;
}
#endif

// Hint to NVIDIA/AMD drivers to prefer the high-performance GPU on Optimus
// systems
extern "C" {
__declspec(dllexport) unsigned long long NvOptimusEnablement = 0x00000001ULL;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

// RenderMode is now defined in scene.h

RenderMode g_currentRenderMode = RenderMode::Raster;
// Panel visibility flags moved to editor_ui.cpp
// Debug toggles for DXR
bool g_dxrDebugUV = false;
bool g_dxrDumpPixels = false;
bool g_dxrHitDebug = false; // encode primitive ID in hit shader for debugging
bool g_dxrDumpD3D12Messages = false; // dump D3D12 InfoQueue messages to stderr
bool g_rasterDebugUV = false; // show raster UVs in mesh pixel shader (debug)
bool g_verboseRenderLogs =
    false; // when true, prints render-loop diagnostics (disabled by default)
bool g_rasterWireframe =
    false; // show meshes in wireframe / disable culling (debug)
bool g_rasterDebugDepth =
    false; // compile shader to output depth as color for debugging

// Global runtime flags (set by command-line)
bool g_debugLog = false; // enable verbose debug logging (use --debug-log)
bool g_fastImport =
    false; // enable Assimp optimization flags to speed imports (--fast-import)

CloudManager g_cloudManager; // Global Global Manager
bool g_cloudRenderingEnabled = true;

ComPtr<ID3D12Resource> g_exportRenderTarget;
ComPtr<ID3D12DescriptorHeap> g_exportRtvHeap;
UINT g_exportRenderTargetWidth = 0;
UINT g_exportRenderTargetHeight = 0;
D3D12_RESOURCE_STATES g_exportRenderTargetState = D3D12_RESOURCE_STATE_PRESENT;
D3D12_CPU_DESCRIPTOR_HANDLE g_exportPreviewSrvCpu = {0};
D3D12_GPU_DESCRIPTOR_HANDLE g_exportPreviewSrvGpu = {0};
bool g_exportPreviewSrvAllocated = false;

static ComPtr<ID3D12DescriptorHeap> g_imguiHeap;
DescriptorHeapAllocator g_cbvSrvAllocator;
HWND g_hwnd = nullptr;

// Window dimensions
bool g_appClosing = false;

// Loaded meshes from Asset loader
std::vector<Asset::GpuMesh> g_loadedMeshes;
std::vector<Asset::Material> g_loadedMaterials;
std::vector<Asset::Texture> g_loadedTextures;
D3D12_GPU_DESCRIPTOR_HANDLE g_texturesGpuStart = {0};
D3D12_GPU_DESCRIPTOR_HANDLE g_envMapGpuHandle = {0};
static ComPtr<ID3D12Resource>
    g_materialBuffer; // Persistent material constant buffer
UINT g_textureDescriptorCount = 0;
static ComPtr<ID3D12Resource> g_materialConstantBuffer;
static void *g_materialCbMappedData = nullptr;
static ComPtr<ID3D12Resource>
    g_materialStructuredBuffer; // Tightly packed for DXR
static ComPtr<ID3D12Resource>
    g_meshStructuredBuffer; // Mesh mapping info for DXR

static std::string g_lastAssetStatus; // Human-readable status for the Assets UI
static std::string
    g_selectedAssetPath; // Path chosen by Open dialog (not yet imported)

// Simple pipeline objects
ComPtr<ID3D12RootSignature> g_rootSignature;
static ComPtr<ID3D12PipelineState> g_pipelineState;
static ComPtr<ID3D12Resource> g_vertexBuffer;
static D3D12_VERTEX_BUFFER_VIEW g_vertexBufferView = {};
ComPtr<ID3D12Resource> g_constantBuffer;
void *g_constantCbMappedData = nullptr;
static float g_offsetX = 0.2f;

// Grid rendering resources
static ComPtr<ID3D12Resource> g_gridVertexBuffer;
static D3D12_VERTEX_BUFFER_VIEW g_gridVBView = {};
static UINT g_gridVertexCount = 0;
static ComPtr<ID3D12PipelineState> g_gridPipelineState;
static ComPtr<ID3D12PipelineState> g_meshSimplePipelineState;
// Grid line thickness in world units (used to expand lines into thin quads)
static float g_gridThickness = 0.02f; // increase to make lines thicker

bool g_drawGrid = false; // toggle grid rendering (default OFF)

// Small camera module is defined in src/camera.h/.cpp
#include "camera.h"

// Simple Vec3 helper for CPU-side math
struct Vec3 {
  float x, y, z;
};

// --- DXR Globals ---
// DXR implementation moved to DxrRenderer module
#include "dxr_renderer.h"

// NVIDIA Streamline (DLSS-SR + DLSS-RR)
#include "streamline_manager.h"
// Now defined in DX12Context::g_streamline

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

// Ray tracing pipeline and AS builder moved into DxrRenderer module

/* Ray Tracing state object creation moved into
 * DxrRenderer::CreateRayTracingPipeline() */

/* Ray tracing pipeline creation moved to DxrRenderer */

/* Shader table creation moved into DxrRenderer::CreateRayTracingPipeline() */

/* Output UAV creation moved into DxrRenderer::CreateRayTracingPipeline() */

// DXR acceleration structure build moved to
// DxrRenderer::BuildAccelerationStructures (see src/dxr_renderer.cpp for
// implementation)

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

// Fallback helper to avoid name-resolution problems after refactor
static inline void TR(ID3D12GraphicsCommandList *cmdList,
                      ID3D12Resource *resource, D3D12_RESOURCE_STATES before,
                      D3D12_RESOURCE_STATES after) {
  TransitionResource(cmdList, resource, before, after);
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

static void EnforceReleaseDebugFlags() {
#ifndef _DEBUG
  g_dxrDebugUV = false;
  g_dxrDumpPixels = false;
  g_dxrHitDebug = false;
  g_dxrDumpD3D12Messages = false;
  g_rasterDebugUV = false;
  g_rasterWireframe = false;
  g_rasterDebugDepth = false;
  g_debugLog = false;
#endif
}

// Select the first suitable hardware adapter (non-software) that supports D3D12
static void GetHardwareAdapter(IDXGIFactory4 *pFactory,
                               IDXGIAdapter1 **ppAdapter) {
  *ppAdapter = nullptr;
  ComPtr<IDXGIAdapter1> adapter;
  SIZE_T maxDedicatedMem = 0;
  ComPtr<IDXGIAdapter1> bestAdapter;

  for (UINT adapterIndex = 0;; ++adapterIndex) {
    if (DXGI_ERROR_NOT_FOUND == pFactory->EnumAdapters1(adapterIndex, &adapter))
      break;

    DXGI_ADAPTER_DESC1 desc;
    adapter->GetDesc1(&desc);

    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
      continue; // skip software adapters

    // Check D3D12 support
    ComPtr<ID3D12Device> testDevice;
    if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                    IID_PPV_ARGS(&testDevice)))) {
      // Prefer the adapter with the most dedicated video memory (likely the
      // dGPU)
      if (desc.DedicatedVideoMemory > maxDedicatedMem) {
        maxDedicatedMem = desc.DedicatedVideoMemory;
        bestAdapter = adapter;
      }
    }
  }

  if (bestAdapter) {
    bestAdapter.CopyTo(ppAdapter);
  }
}

static void ExecuteCommandListAndWait(ID3D12GraphicsCommandList *cmdList) {
  ThrowIfFailed(cmdList->Close());
  ID3D12CommandList *lists[] = {cmdList};
  DX12Context::g_commandQueue->ExecuteCommandLists(1, lists);

  // Wait for completion
  ComPtr<ID3D12Fence> fence;
  ThrowIfFailed(DX12Context::g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                                   IID_PPV_ARGS(&fence)));
  HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  ThrowIfFailed(DX12Context::g_commandQueue->Signal(fence.Get(), 1));
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
      255, 0,   0,   255, // Red
      0,   255, 0,   255, // Green
      0,   0,   255, 255, // Blue
      255, 255, 0,   255  // Yellow
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
  ThrowIfFailed(DX12Context::g_device->CreateCommittedResource(
      &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &uploadDesc,
      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer)));

  // Copy texture data to upload buffer
  void *mappedData = nullptr;
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
  ThrowIfFailed(DX12Context::g_device->CreateCommittedResource(
      &defaultHeapProps, D3D12_HEAP_FLAG_NONE, &textureDesc,
      D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture)));

  // Allocate descriptor for the texture - using persistent allocation
  DescriptorAllocation alloc = g_cbvSrvAllocator.AllocatePersistent(1);
  if (g_textureDescriptorCount == 0) {
    g_texturesGpuStart = alloc.gpu;
  }

  // Create SRV
  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Texture2D.MipLevels = 1;
  DX12Context::g_device->CreateShaderResourceView(texture.Get(), &srvDesc,
                                                  alloc.cpu);

  // Copy from upload buffer to texture
  ComPtr<ID3D12CommandAllocator> cmdAlloc;
  ComPtr<ID3D12GraphicsCommandList> cmdList;
  ThrowIfFailed(DX12Context::g_device->CreateCommandAllocator(
      D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAlloc)));
  ThrowIfFailed(DX12Context::g_device->CreateCommandList(
      0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc.Get(), nullptr,
      IID_PPV_ARGS(&cmdList)));

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

  // Log to stderr only
  fprintf(stderr, "CreateTestTexture: Created 2x2 checkerboard texture\n");
}

bool InitApplication(HWND hwnd) {

  g_hwnd = hwnd;

  if (!DX12Context::InitD3D12(hwnd)) {
    return false;
  }

  // Provide Streamline manager to DXR module (optional feature).
  DxrRenderer::SetStreamlineManager(&DX12Context::g_streamline);

  // Probe DXR support on the current device.
  DxrRenderer::Initialize(DX12Context::g_device.Get());

  if (g_rayTracingSupported) {
    fprintf(stderr, "DXR Ray Tracing Supported (probe)\n");
    DxrRenderer::CreateRayTracingPipeline(DX12Context::g_windowWidth,
                                          DX12Context::g_windowHeight);
    fprintf(stderr, "InitApplication: CreateRayTracingPipeline finished\n");
  } else {
    fprintf(stderr, "DXR Ray Tracing NOT supported on this device\n");
  }

  // Initialize descriptor allocator for CBV/SRV/UAV
  g_cbvSrvAllocator.Init(DX12Context::g_device.Get(),
                         D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 65536,
                         FrameCount);

  // Now that fence and event are valid, attach command queue & fence to DXR
  // renderer
  DxrRenderer::SetCommandQueue(
      DX12Context::g_commandQueue.Get(), DX12Context::g_fence.Get(),
      DX12Context::g_fenceValues, &DX12Context::g_frameIndex,
      DX12Context::g_fenceEvent);

  // --- Create a root signature with CBV b0 (vertex), descriptor table t0
  // (SRV), and CBV b1 (pixel material) ---
  D3D12_ROOT_PARAMETER rootParameters[6] = {};
  // b0 - transform CBV for vertex shader AND pixel shader (needed for view
  // direction)
  rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  rootParameters[0].Descriptor.ShaderRegister = 0;
  rootParameters[0].Descriptor.RegisterSpace = 0;
  rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  // t0-t1023 - descriptor table (SRV) for pixel shader (large buffer for
  // bindless-style indexing)
  static D3D12_DESCRIPTOR_RANGE descRange = {};
  descRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  descRange.NumDescriptors = 2048; // Support many textures concurrently
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
  // b2 - world matrix as root constants for vertex shader
  rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  rootParameters[3].Constants.ShaderRegister = 2;
  rootParameters[3].Constants.RegisterSpace = 0;
  rootParameters[3].Constants.Num32BitValues = 16;
  rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

  // t0, space1 - Environment Map Descriptor Table (Texture2D)
  static D3D12_DESCRIPTOR_RANGE envMapRange = {};
  envMapRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  envMapRange.NumDescriptors = 1;
  envMapRange.BaseShaderRegister = 0;
  envMapRange.RegisterSpace = 1;
  envMapRange.OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  rootParameters[4].DescriptorTable.NumDescriptorRanges = 1;
  rootParameters[4].DescriptorTable.pDescriptorRanges = &envMapRange;
  rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  // Cloud resources (space2): CBV b10 + SRV t10,t11
  static D3D12_DESCRIPTOR_RANGE cloudRanges[2] = {};
  cloudRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
  cloudRanges[0].NumDescriptors = 1;
  cloudRanges[0].BaseShaderRegister = 10; // b10
  cloudRanges[0].RegisterSpace = 2;
  cloudRanges[0].OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  cloudRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  cloudRanges[1].NumDescriptors = 3;      // Base + Detail + BakedSky
  cloudRanges[1].BaseShaderRegister = 10; // t10, t11, t12
  cloudRanges[1].RegisterSpace = 2;
  cloudRanges[1].OffsetInDescriptorsFromTableStart =
      D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  rootParameters[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  rootParameters[5].DescriptorTable.NumDescriptorRanges = 2;
  rootParameters[5].DescriptorTable.pDescriptorRanges = cloudRanges;
  rootParameters[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  // static sampler for textures
  D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
  // Default sampler (space 0, register 0)
  samplers[0].Filter = D3D12_FILTER_ANISOTROPIC;
  samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  samplers[0].MipLODBias = 0;
  samplers[0].MaxAnisotropy = 16;
  samplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
  samplers[0].ShaderRegister = 0;
  samplers[0].RegisterSpace = 0;
  samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  // Cloud sampler (space 2, register 0)
  samplers[1].Filter = D3D12_FILTER_ANISOTROPIC;
  samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  samplers[1].MipLODBias = 0;
  samplers[1].MaxAnisotropy = 16;
  samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
  samplers[1].ShaderRegister = 0;
  samplers[1].RegisterSpace = 2;
  samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
  rootSignatureDesc.NumParameters = _countof(rootParameters);
  rootSignatureDesc.pParameters = rootParameters;
  rootSignatureDesc.NumStaticSamplers = _countof(samplers);
  rootSignatureDesc.pStaticSamplers = samplers;
  rootSignatureDesc.Flags =
      D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

  ComPtr<ID3DBlob> signature;
  ComPtr<ID3DBlob> error;
  ThrowIfFailed(D3D12SerializeRootSignature(
      &rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
  ThrowIfFailed(DX12Context::g_device->CreateRootSignature(
      0, signature->GetBufferPointer(), signature->GetBufferSize(),
      IID_PPV_ARGS(&g_rootSignature)));

  // --- Compile simple shaders for demo triangle (DXC / SM6) ---
  ComPtr<IDxcBlob> vsBlob;
  ComPtr<IDxcBlob> psBlob;
  std::wstring simpleShaderPath = FindShaderFile(L"shaders\\simple.hlsl");
  {
    char debugMsg[512];
    sprintf_s(debugMsg, "Loading simple shader from: %ls\n",
              simpleShaderPath.c_str());
    fprintf(stderr, "%s", debugMsg);
  }
  // Use local DXC helper instance here (module-level ones exist in raster/dxr
  // modules)
  DxcHelper localDxc;
  try {
    vsBlob = localDxc.Compile(simpleShaderPath, L"VSMain", L"vs_6_0");
  } catch (const std::exception &e) {
    // Log to stderr only
    fprintf(stderr, "InitD3D12: VS compile failed (DXC) for %ls: %s\n",
            simpleShaderPath.c_str(), e.what());
    return false;
  }
  try {
    psBlob = localDxc.Compile(simpleShaderPath, L"PSMain", L"ps_6_0");
  } catch (const std::exception &e) {
    // Log to stderr only
    fprintf(stderr, "InitD3D12: PS compile failed (DXC) for %ls: %s\n",
            simpleShaderPath.c_str(), e.what());
    return false;
  }

  // --- Compile mesh PBR shaders (full vertex layout + PBR material pixel
  // shader) using DXC (SM6) ---
  ComPtr<IDxcBlob> vsMeshBlob;
  ComPtr<IDxcBlob> psMeshBlob;
  std::wstring pbrShaderPath = FindShaderFile(L"shaders\\pbr_mesh.hlsl");
  {
    char debugMsg[512];
    sprintf_s(debugMsg, "Loading PBR shader from: %ls\n",
              pbrShaderPath.c_str());
    fprintf(stderr, "%s", debugMsg);
  }
  try {
    vsMeshBlob = localDxc.Compile(pbrShaderPath, L"VSMainMesh", L"vs_6_0");
  } catch (const std::exception &e) {
    // Log to stderr only
    fprintf(stderr, "InitD3D12: VS compile failed (DXC) for %ls: %s\n",
            pbrShaderPath.c_str(), e.what());
    return false;
  }
  try {
    psMeshBlob = localDxc.Compile(pbrShaderPath, L"PSMainMesh", L"ps_6_0");
  } catch (const std::exception &e) {
    // Log to stderr only
    fprintf(stderr, "InitD3D12: PS compile failed (DXC) for %ls: %s\n",
            pbrShaderPath.c_str(), e.what());
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
  psoDesc.RTVFormats[0] = DXGI_FORMAT_R10G10B10A2_UNORM;
  psoDesc.SampleDesc.Count = 1;

  ThrowIfFailed(DX12Context::g_device->CreateGraphicsPipelineState(
      &psoDesc, IID_PPV_ARGS(&g_pipelineState)));

  // Create grid resources using raster module
  RasterRenderer::CreateGridResources(DX12Context::g_device.Get(),
                                      g_gridThickness);

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

  // Enable depth testing for mesh rendering
  meshPsoDesc.DepthStencilState.DepthEnable = TRUE;
  meshPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
  meshPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
  meshPsoDesc.DepthStencilState.StencilEnable = FALSE;

  {
    HRESULT hrMesh = DX12Context::g_device->CreateGraphicsPipelineState(
        &meshPsoDesc, IID_PPV_ARGS(&RasterRenderer::g_meshPipelineState));
    if (FAILED(hrMesh)) {
      fprintf(stderr,
              "InitApplication: CreateGraphicsPipelineState (mesh) failed: "
              "0x%08x\n",
              (unsigned)hrMesh);
#ifdef _DEBUG
      ComPtr<ID3D12InfoQueue> infoQueue;
      if (SUCCEEDED(DX12Context::g_device->QueryInterface(
              IID_PPV_ARGS(&infoQueue)))) {
        UINT64 num = infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();
        for (UINT64 mi = 0; mi < num; ++mi) {
          SIZE_T messageLength = 0;
          infoQueue->GetMessage(mi, nullptr, &messageLength);
          std::vector<char> message(messageLength);
          D3D12_MESSAGE *pMsg =
              reinterpret_cast<D3D12_MESSAGE *>(message.data());
          infoQueue->GetMessage(mi, pMsg, &messageLength);
          fprintf(
              stderr,
              "D3D12 INFO (PSO create): Category=%d Severity=%d ID=%d: %s\n",
              (int)pMsg->Category, (int)pMsg->Severity, (int)pMsg->ID,
              pMsg->pDescription);
        }
      }
#endif
    }
    ThrowIfFailed(hrMesh);
  }

  // Recreate the mesh PSO via RasterRenderer to pick up debug defines (e.g.
  // RASTER_DEBUG_DEPTH)
  RasterRenderer::RecreateMeshPipeline(DX12Context::g_device.Get(),
                                       g_rootSignature.Get());

  // Additionally create a simple mesh PSO that reads only POSITION and draws a
  // constant color
  {
    std::wstring simplePath = FindShaderFile(L"shaders\\simple.hlsl");
    ComPtr<IDxcBlob> vsMeshSimpleBlob;
    ComPtr<IDxcBlob> psMeshSimpleBlob;
    try {
      vsMeshSimpleBlob =
          localDxc.Compile(simplePath, L"VSMainMeshSimple", L"vs_6_0");
      psMeshSimpleBlob =
          localDxc.Compile(simplePath, L"PSMainMeshSimple", L"ps_6_0");
    } catch (const std::exception &e) {
      fprintf(stderr, "InitD3D12: simple mesh shader compile failed: %s\n",
              e.what());
      vsMeshSimpleBlob = nullptr;
      psMeshSimpleBlob = nullptr;
    }

    if (vsMeshSimpleBlob && psMeshSimpleBlob) {
      D3D12_GRAPHICS_PIPELINE_STATE_DESC simplePso = meshPsoDesc;
      D3D12_INPUT_ELEMENT_DESC posOnlyLayout[] = {
          {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
           D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
      simplePso.InputLayout = {posOnlyLayout, _countof(posOnlyLayout)};
      simplePso.VS = {vsMeshSimpleBlob->GetBufferPointer(),
                      vsMeshSimpleBlob->GetBufferSize()};
      simplePso.PS = {psMeshSimpleBlob->GetBufferPointer(),
                      psMeshSimpleBlob->GetBufferSize()};
      if (g_rootSignature)
        simplePso.pRootSignature = g_rootSignature.Get();
      ThrowIfFailed(DX12Context::g_device->CreateGraphicsPipelineState(
          &simplePso, IID_PPV_ARGS(&g_meshSimplePipelineState)));
    }
  }

  // --- Initialize ImGui ---
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ApplyModernImGuiTheme();
  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
  // Legacy ImGui DX12 init path does not advertise RendererHasTextures.
  // Build the atlas up-front so first NewFrame doesn't hit font-atlas assert.
  if (!io.Fonts->IsBuilt()) {
    unsigned char *pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
  }
  ImGui_ImplWin32_Init(hwnd);
  // Initialize DX12 backend with the main CBV/SRV/UAV heap so we can show
  // thumbnails using existing engine texture SRVs.
  DescriptorAllocation imguiFontAlloc = g_cbvSrvAllocator.AllocatePersistent(1);
  ImGui_ImplDX12_Init(DX12Context::g_device.Get(), FrameCount,
                      DXGI_FORMAT_R10G10B10A2_UNORM, g_cbvSrvAllocator.Heap(),
                      imguiFontAlloc.cpu, imguiFontAlloc.gpu);

  ImGui_ImplDX12_CreateDeviceObjects();
  // When viewports are enabled we want windows created by ImGui to look
  // consistent across platform-native child windows.
  ImGuiStyle &style = ImGui::GetStyle();
  if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
    style.WindowRounding = 0.0f;
  }

  // Initialize asset loader with device & command queue so it can perform
  // uploads
  Asset::Initialize(DX12Context::g_device.Get(),
                    DX12Context::g_commandQueue.Get());

  // Initialize IBL Manager and load default environment map
  IBLManager::Get().Initialize(DX12Context::g_device.Get(),
                               DX12Context::g_commandQueue.Get());
  /*
  if (!IBLManager::Get().LoadEnvironmentMap("assets/env.exr")) {
    fprintf(
        stderr,
        "Main: Failed to load assets/env.exr, checking for assets/env.hdr\n");
    IBLManager::Get().LoadEnvironmentMap("assets/env.hdr");
  }
  */

  // Initialize Prague Sky Model
  IBLManager::Get().InitializeSkyModel("assets/PragueSkyModelDataset.dat");
  IBLManager::Get().SetIBLSource(IBLManager::IBLSource::PragueSkyModel);

  // Always allocate a descriptor for the environment map, so it can be updated
  // later even if no file is currently loaded.
  {
    DescriptorAllocation alloc = g_cbvSrvAllocator.AllocatePersistent(1);
    IBLManager::Get().SetGPUHandle(alloc.gpu);
    IBLManager::Get().SetCPUHandle(alloc.cpu);
    g_envMapGpuHandle = alloc.gpu;

    // If loaded, create the view immediately.
    // If not loaded, we ideally need a valid descriptor (null SRV or dummy
    // texture) to prevent crashes if the shader accesses it.
    if (IBLManager::Get().IsLoaded()) {
      D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
      srvDesc.Shader4ComponentMapping =
          D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      srvDesc.Format = IBLManager::Get().GetEnvMap().format;
      srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      srvDesc.Texture2D.MipLevels = (UINT)-1;
      DX12Context::g_device->CreateShaderResourceView(
          IBLManager::Get().GetEnvMap().resource.Get(), &srvDesc, alloc.cpu);
    } else {
      // Create a default scalar (null) SRV or similar?
      // For Texture2D, a null SRV describes a "null resource" but with valid
      // format info. Or we can rely on IBLManager creating a dummy texture.
      // Let's create a NULL SRV so it's a valid descriptor (returns 0 on
      // sample).
      D3D12_SHADER_RESOURCE_VIEW_DESC nullSrvDesc = {};
      nullSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      nullSrvDesc.Shader4ComponentMapping =
          D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      nullSrvDesc.Format =
          DXGI_FORMAT_R32G32B32A32_FLOAT; // Standard env map format
      nullSrvDesc.Texture2D.MipLevels = 1;
      DX12Context::g_device->CreateShaderResourceView(nullptr, &nullSrvDesc,
                                                      alloc.cpu);
    }
  }

  // Log: reached post-Create pipeline initialization (stderr only)
  fprintf(stderr, "InitD3D12: Post-pipeline initialization reached\n");

  // Demo triangle removed. Scene will rely on loaded assets for visible
  // geometry. If you need a synthetic fallback, the code below will create a
  // cube when auto-load fails.

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

  ThrowIfFailed(DX12Context::g_device->CreateCommittedResource(
      &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &cbDesc,
      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
      IID_PPV_ARGS(&g_constantBuffer)));
  ThrowIfFailed(g_constantBuffer->Map(
      0, nullptr, reinterpret_cast<void **>(&g_constantCbMappedData)));

  // Initialize constant buffer data (small offset)
  AlignConstants constants = {{0.2f, 0.0f, 0.0f, 0.0f}};
  memcpy(g_constantCbMappedData, &constants, sizeof(constants));

  // --- Create persistent material constant buffer ---
  // Large enough to hold many unique material instances per frame
  struct MaterialCB {
    float diffuseColor[4];
    float reflectionColor[4];
    float refractionColor[4];
    float emissiveColor[4];
    int textureIndices[4];
    int emissiveAndPad[4];   // x=emissive, y=occlusion, z=metalRough
    float extraParams[4];    // x=metalness, y=emissiveIntensity, zw=unused
    float archvizParams0[4]; // x=clearcoat, y=clearcoatRoughness, z=thinWalled,
                             // w=translucency
    float uvTransform[4];    // xy=uvScale, zw=uvOffset
    float
        triPlanarParams[4]; // x=enabled, y=scale, z=sharpness, w=normalStrength
  };
  const UINT64 matCbSizeSingle = (sizeof(MaterialCB) + 255) & ~255;
  const UINT64 matCbSize = matCbSizeSingle * 16384; // Support up to 16384 calls
  D3D12_RESOURCE_DESC matCbDesc = {};
  matCbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  matCbDesc.Width = matCbSize;
  matCbDesc.Height = 1;
  matCbDesc.DepthOrArraySize = 1;
  matCbDesc.MipLevels = 1;
  matCbDesc.SampleDesc.Count = 1;
  matCbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ThrowIfFailed(DX12Context::g_device->CreateCommittedResource(
      &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &matCbDesc,
      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
      IID_PPV_ARGS(&g_materialConstantBuffer)));
  ThrowIfFailed(g_materialConstantBuffer->Map(
      0, nullptr, reinterpret_cast<void **>(&g_materialCbMappedData)));

  // Material Structured Buffer for DXR (tightly packed, no 256B alignment)
  {
    const UINT64 matSbSize = sizeof(MaterialCB) * 16384;
    D3D12_RESOURCE_DESC matSbDesc = matCbDesc;
    matSbDesc.Width = matSbSize;
    ThrowIfFailed(DX12Context::g_device->CreateCommittedResource(
        &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &matSbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&g_materialStructuredBuffer)));
  }

  // Mesh Structured Buffer for DXR
  {
    struct MeshData {
      int materialIndex;
      int vbIndex;
      int ibIndex;
      int pad;
    };
    const UINT64 meshSbSize = sizeof(MeshData) * 16384;
    D3D12_RESOURCE_DESC meshSbDesc = matCbDesc;
    meshSbDesc.Width = meshSbSize;
    ThrowIfFailed(DX12Context::g_device->CreateCommittedResource(
        &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &meshSbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&g_meshStructuredBuffer)));
  }

  // Allocate camera constant buffer (upload heap)
  {
    const UINT64 camCbSize = (sizeof(CameraCB) + 255) & ~255;
    D3D12_RESOURCE_DESC camCbDesc = {};
    camCbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    camCbDesc.Width = camCbSize;
    camCbDesc.Height = 1;
    camCbDesc.DepthOrArraySize = 1;
    camCbDesc.MipLevels = 1;
    camCbDesc.SampleDesc.Count = 1;
    camCbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ThrowIfFailed(DX12Context::g_device->CreateCommittedResource(
        &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &camCbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&g_cameraConstantBuffer)));

    // Initialize with default camera
    UINT8 *pCamData = nullptr;
    D3D12_RANGE readRange = {0, 0};
    ThrowIfFailed(g_cameraConstantBuffer->Map(
        0, &readRange, reinterpret_cast<void **>(&pCamData)));
    memcpy(pCamData, &g_cameraData, sizeof(g_cameraData));
    g_cameraConstantBuffer->Unmap(0, nullptr);
  }

  // Initialize Cloud Manager (Generate noise texture, upload params)
  {
    fprintf(stderr, "Initializing Cloud Manager...\n");

    // Use a temporary command list to ensure clean state
    ComPtr<ID3D12CommandAllocator> tempAlloc;
    ThrowIfFailed(DX12Context::g_device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&tempAlloc)));
    ComPtr<ID3D12GraphicsCommandList> tempList;
    ThrowIfFailed(DX12Context::g_device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, tempAlloc.Get(), nullptr,
        IID_PPV_ARGS(&tempList)));

    g_cloudManager.Initialize(DX12Context::g_device.Get(), tempList.Get());

    // Execute immediately using the existing helper which closes the list and
    // waits
    ExecuteCommandListAndWait(tempList.Get());
  }

  // NOTE: g_commandList was closed early in InitD3D12 (after creation).
  // It will be Reset() in the first frame's PopulateCommandList.

  // Log successful InitD3D12 completion (stderr only)
  fprintf(stderr, "InitD3D12: Completed OK (returning true)\n");

  return true;
}

// Mesh PSO recreation moved to RasterRenderer::RecreateMeshPipeline
// (src/raster_renderer.cpp)

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam,
                         LPARAM lParam) {
  if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
    return true;

  switch (message) {
  case WM_CLOSE:
    g_appClosing = true;
    PostQuitMessage(0);
    return 0;
  case WM_SIZE:
    if (!g_appClosing && DX12Context::g_swapChain && wParam != SIZE_MINIMIZED) {
      UINT width = LOWORD(lParam);
      UINT height = HIWORD(lParam);
      DX12Context::ResizeSwapChain(width, height);
    }
    return 0;
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  }

  return DefWindowProc(hWnd, message, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine,
                   int nCmdShow) {
#ifdef _DEBUG
  SetUnhandledExceptionFilter(TopLevelExceptionHandler);
#endif

  // Do not create or show a console window here.
  // We assume the caller runs the executable from a terminal (or redirects
  // stdout/stderr). Logging still uses stderr but we won't forcibly allocate a
  // console window.

  // Log to stderr only
  fprintf(stderr, "Application starting...\n");

  // Parse command line for flags and optional custom glTF file
  std::string customGltfPath;
  std::string sceneToLoad;
  if (lpCmdLine && *lpCmdLine) {
    std::string cmd = lpCmdLine;
    std::istringstream iss(cmd);
    std::string token;
    while (iss >> token) {
      if (token == "--debug-log") {
#ifdef _DEBUG
        g_debugLog = true;
#else
        fprintf(stderr, "--debug-log ignored in non-debug builds\n");
#endif
      } else if (token == "--fast-import" || token == "--optimize-import") {
        g_fastImport = true;
      } else if (token == "--load") {
        if (iss >> token) {
          // remove quotes if present
          if (!token.empty() && token.front() == '"')
            token = token.substr(1);
          if (!token.empty() && token.back() == '"')
            token = token.substr(0, token.size() - 1);
          sceneToLoad = token;
        }
      } else {
        // first non-flag token is interpreted as a path
        if (customGltfPath.empty()) {
          // remove quotes if present
          if (!token.empty() && token.front() == '"')
            token = token.substr(1);
          if (!token.empty() && token.back() == '"')
            token = token.substr(0, token.size() - 1);
          customGltfPath = token;
        }
      }
    }
  }

  const wchar_t CLASS_NAME[] = L"ProjectRenderWndClass";

  WNDCLASSW wc = {};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = hInstance;
  wc.lpszClassName = CLASS_NAME;

  RegisterClassW(&wc);

  HWND hwnd = CreateWindowExW(0, CLASS_NAME, L"Project-Render",
                              WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              1920, 1080, nullptr, nullptr, hInstance, nullptr);

  if (!hwnd) {
    return 0;
  }

  ShowWindow(hwnd, nCmdShow);

  EnforceReleaseDebugFlags();

  // Log that we showed the window (stderr only)
  fprintf(stderr, "ShowWindow called\n");

  if (!InitApplication(hwnd)) {
    MessageBoxA(nullptr, "Failed to initialize application", "Error",
                MB_OK | MB_ICONERROR);
    return -1;
  }

  fprintf(stderr, "InitApplication returned OK\n");

  // Scene Setup
  if (!sceneToLoad.empty()) {
    if (fs::exists(sceneToLoad)) {
      if (SceneIO::LoadScene(sceneToLoad)) {
        fprintf(stderr, "Startup: loaded scene %s\n", sceneToLoad.c_str());
      } else {
        fprintf(stderr, "Startup: failed to load scene %s\n",
                sceneToLoad.c_str());
      }
    } else {
      fprintf(stderr, "Startup: --load path not found: %s\n",
              sceneToLoad.c_str());
    }
  } else if (!customGltfPath.empty()) {
    if (fs::exists(customGltfPath)) {
      float rootPos[3] = {0, 0, 0};
      if (Scene::ImportModel(customGltfPath, rootPos)) {
        ResetCamera();
      }
    } else {
      fprintf(stderr, "Startup: custom mesh path not found: %s\n",
              customGltfPath.c_str());
    }
  } else {
    // Default: Just ground plane, no auto-loaded GLBs anymore
    Scene::AddDefaultPlane(0.0f);
  }

  // ReSTIR DI: Initialize test lights for Phase 2
  {
    // User requested to remove all lights except the sun
    std::vector<GpuLight> testLights;
    DxrRenderer::UpdateLights(testLights);
  }

  // State variables for Time and North Offset
  static float g_timeOfDay = 10.0f;
  static float g_northOffset = 0.0f;
  static float g_latitudeDeg = 50.08f; // Prague default latitude
  static float g_dayOfYear = 172.0f;   // June solstice-ish

  // Basic message loop + simple render
  MSG msg = {};
  // DenoiserModeFromIndex, DenoiserIndexFromMode, EnsureExportRenderTarget,
  // RestoreRenderExportState, StartRenderExportJob moved to editor_ui.cpp

  auto PopulateCommandList = [&]() {
    if (g_renderExportJob.active) {
      g_currentRenderMode = RenderMode::DXR;
    }

    // Update Sky Parameters (Run every frame to ensure consistency)
    {
      const float PI = 3.14159265f;
      const float DEG2RAD = PI / 180.0f;

      // Physically-based solar position model (local solar time).
      // Latitude: [-66.5, 66.5], Day: [1, 365], Hour angle: 15 deg/hour.
      const float latitudeRad = g_latitudeDeg * DEG2RAD;
      const float day = (std::clamp)(g_dayOfYear, 1.0f, 365.0f);
      const float declDeg =
          23.44f * std::sin(2.0f * PI * (284.0f + day) / 365.0f);
      const float declRad = declDeg * DEG2RAD;
      const float hourAngleRad = (g_timeOfDay - 12.0f) * 15.0f * DEG2RAD;

      float sinEl = std::sin(latitudeRad) * std::sin(declRad) +
                    std::cos(latitudeRad) * std::cos(declRad) *
                        std::cos(hourAngleRad);
      sinEl = (std::clamp)(sinEl, -1.0f, 1.0f);
      float elRad = std::asin(sinEl);

      // Azimuth from North, clockwise: 90=east, 180=south, 270=west.
      float azNorthRad =
          std::atan2(std::sin(hourAngleRad),
                     std::cos(hourAngleRad) * std::sin(latitudeRad) -
                         std::tan(declRad) * std::cos(latitudeRad)) +
          PI;
      azNorthRad += g_northOffset * DEG2RAD;

      // Prague model convention: azimuth is in XY plane from +X toward +Y.
      // Our world convention in shaders is +Z = north, +X = east, +Y = up.
      // Mapping Prague(X,Y,Z) -> World(X,Z,Y) means:
      //   azPrague = 0 at world +X (east), +PI/2 at world +Z (north).
      float azPragueRad = (PI * 0.5f) - azNorthRad;
      while (azPragueRad < -PI)
        azPragueRad += 2.0f * PI;
      while (azPragueRad > PI)
        azPragueRad -= 2.0f * PI;

      if (elRad < -0.15f) {
        // Keep a bit below horizon for twilight behavior from Prague model,
        // but avoid excessively negative values.
        elRad = -0.15f;
      }

      // Get current parameters to preserve other sliders
      float sunSize = IBLManager::Get().GetSunSize();
      float sunInt = IBLManager::Get().GetSunIntensity();

      // Apply to Sky Model
      IBLManager::Get().SetSolarAltitude(elRad);
      IBLManager::Get().SetSolarAzimuth(azPragueRad);
      IBLManager::Get().UpdateSkyModel();

      // Sync Directional Light
      float sunX = std::sin(azNorthRad) * std::cos(elRad);
      float sunZ = std::cos(azNorthRad) * std::cos(elRad);
      float sunY = std::sin(elRad);

      g_cameraData.lightDir[0] = sunX;
      g_cameraData.lightDir[1] = sunY;
      g_cameraData.lightDir[2] = sunZ;
      // Pass angular radius in radians to w component
      g_cameraData.lightDir[3] = sunSize * DEG2RAD * 0.5f;

      // Sync Sun Color from Sky Model
      auto sunRGB = IBLManager::Get().GetSunColor();
      g_cameraData.lightColor[0] = sunRGB.x;
      g_cameraData.lightColor[1] = sunRGB.y;
      g_cameraData.lightColor[2] = sunRGB.z;

      // Sync Sun Intensity
      g_cameraData.lightColor[3] = sunInt;

        // IBL environment map rotation (degrees), consumed by shaders.
        g_cameraData.iblRotationDegrees =
          IBLManager::Get().GetIblRotationDegrees();

      UpdateCameraCB();
    }

    // Log to stderr only (controlled by verbose flag)
    if (g_verboseRenderLogs)
      fprintf(stderr, "PopulateCommandList start\n");

    // Reset per-frame command allocator and command list
    ThrowIfFailed(DX12Context::g_frameResources[DX12Context::g_frameIndex]
                      .commandAllocator->Reset());
    ThrowIfFailed(DX12Context::g_commandList->Reset(
        DX12Context::g_frameResources[DX12Context::g_frameIndex]
            .commandAllocator.Get(),
        nullptr));

    DxrRenderer::BeginFrameProfiling(DX12Context::g_commandList.Get());

    // Reset per-frame transient descriptor allocator
    g_cbvSrvAllocator.ResetFrame(DX12Context::g_frameIndex);

    // Get RTV handle
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
        DX12Context::g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtvHandle.ptr = rtvHandle.ptr + (SIZE_T)(DX12Context::g_frameIndex *
                                             DX12Context::g_rtvDescriptorSize);

    // Set viewport and scissor
    D3D12_VIEWPORT viewport = {};
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.Width = (float)DX12Context::g_windowWidth;
    viewport.Height = (float)DX12Context::g_windowHeight;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    D3D12_RECT scissorRect = {0, 0, (LONG)DX12Context::g_windowWidth,
                              (LONG)DX12Context::g_windowHeight};

    DX12Context::g_commandList->RSSetViewports(1, &viewport);
    DX12Context::g_commandList->RSSetScissorRects(1, &scissorRect);

    // Render based on current mode
    switch (g_currentRenderMode) {
    case RenderMode::DXR: {
      if (!DxrRenderer::IsReady()) {
        try {
          DX12Context::WaitGPUIdle();
          DxrRenderer::CreateRayTracingPipeline(DX12Context::g_windowWidth,
                                                DX12Context::g_windowHeight);
        } catch (const std::exception &e) {
          fprintf(stderr,
                  "DXR lazy pipeline create failed during mode switch: %s\n",
                  e.what());
        } catch (...) {
          fprintf(stderr,
                  "DXR lazy pipeline create failed during mode switch: unknown "
                  "exception\n");
        }
      }

      // Use DXR module to perform ray dispatch and copy to backbuffer
      if (DxrRenderer::IsReady()) {
        // Update Structured Material Buffer for DXR
        if (g_materialStructuredBuffer && !g_loadedMaterials.empty()) {
          struct MaterialData {
            float diffuseColor[4];
            float reflectionColor[4];
            float refractionColor[4];
            float emissiveColor[4];
            int textureIndices[4];
            int emissiveAndPad[4];
            float extraParams[4];
            float archvizParams0[4];
            float uvTransform[4];
            float triPlanarParams[4];
          };
          UINT8 *pData = nullptr;
          D3D12_RANGE readRange = {0, 0};
          if (SUCCEEDED(g_materialStructuredBuffer->Map(
                  0, &readRange, reinterpret_cast<void **>(&pData)))) {
            for (size_t i = 0; i < g_loadedMaterials.size() && i < 16384; ++i) {
              const auto &srcMat = g_loadedMaterials[i];
              MaterialData mat = {};
              memcpy(mat.diffuseColor, srcMat.diffuseColor, sizeof(float) * 4);

              memcpy(mat.reflectionColor, srcMat.reflectionColor,
                     sizeof(float) * 3);
              mat.reflectionColor[3] = srcMat.reflectionGlossiness;

              memcpy(mat.refractionColor, srcMat.refractionColor,
                     sizeof(float) * 3);
              mat.refractionColor[3] = srcMat.refractionGlossiness;

              memcpy(mat.emissiveColor, srcMat.emissiveColor,
                     sizeof(float) * 3);
              mat.emissiveColor[3] = srcMat.ior; // Pack IOR in W

              mat.textureIndices[0] = srcMat.diffuseTexture;
              mat.textureIndices[1] = srcMat.reflectionTexture;
              mat.textureIndices[2] = srcMat.normalTexture;
              mat.textureIndices[3] = srcMat.refractionTexture;

              mat.emissiveAndPad[0] = srcMat.emissiveTexture;
              mat.emissiveAndPad[1] = srcMat.occlusionTexture;
              mat.emissiveAndPad[2] = srcMat.metalRoughTexture;
              mat.emissiveAndPad[3] = 0; // Pad

              mat.extraParams[0] = srcMat.metalness;
              mat.extraParams[1] = srcMat.emissiveIntensity;
              mat.extraParams[2] = 0.0f;
              mat.extraParams[3] = 0.0f;

              mat.archvizParams0[0] = srcMat.clearcoat;
              mat.archvizParams0[1] = srcMat.clearcoatRoughness;
              mat.archvizParams0[2] = srcMat.thinWalled;
              mat.archvizParams0[3] = srcMat.translucency;

              mat.uvTransform[0] = srcMat.uvScale[0];
              mat.uvTransform[1] = srcMat.uvScale[1];
              mat.uvTransform[2] = srcMat.uvOffset[0];
              mat.uvTransform[3] = srcMat.uvOffset[1];

              mat.triPlanarParams[0] = srcMat.triPlanarEnabled;
              mat.triPlanarParams[1] = srcMat.triPlanarScale;
              mat.triPlanarParams[2] = srcMat.triPlanarSharpness;
              mat.triPlanarParams[3] = srcMat.triPlanarNormalStrength;

              memcpy(pData + i * sizeof(MaterialData), &mat,
                     sizeof(MaterialData));
            }
            g_materialStructuredBuffer->Unmap(0, nullptr);
          }
        }

        // Update Mesh Structured Buffer for DXR
        auto activeMeshes = Scene::GetActiveMeshes();
        auto sceneInstances = Scene::GetInstances();
        if (g_meshStructuredBuffer && !activeMeshes.empty()) {
          struct MeshData {
            int materialIndex;
            int vbIndex;
            int ibIndex;
            int pad;
          };
          UINT8 *pData = nullptr;
          D3D12_RANGE readRange = {0, 0};
          if (SUCCEEDED(g_meshStructuredBuffer->Map(
                  0, &readRange, reinterpret_cast<void **>(&pData)))) {
            // We use global indices for vertices/indices in DXR
            for (size_t i = 0; i < activeMeshes.size() && i < 16384; ++i) {
              MeshData m = {};
              m.materialIndex = activeMeshes[i]->materialIndex;
              m.vbIndex = (int)i;
              m.ibIndex = (int)i;
              memcpy(pData + i * sizeof(MeshData), &m, sizeof(MeshData));
            }
            g_meshStructuredBuffer->Unmap(0, nullptr);
          }
        }

        ID3D12Resource *dxrTarget =
            DX12Context::g_renderTargets[DX12Context::g_frameIndex].Get();
        D3D12_CPU_DESCRIPTOR_HANDLE dxrRtv = rtvHandle;
        if (g_renderExportJob.active && g_exportRenderTarget &&
            g_exportRtvHeap) {
          dxrTarget = g_exportRenderTarget.Get();
          dxrRtv = g_exportRtvHeap->GetCPUDescriptorHandleForHeapStart();
        }

        bool dxrOk = DxrRenderer::RenderFrame(
          DX12Context::g_commandList.Get(),
          DX12Context::g_frameResources[DX12Context::g_frameIndex]
            .commandAllocator.Get(),
          DX12Context::g_frameIndex, dxrTarget, dxrRtv,
          g_cameraConstantBuffer.Get(), g_materialStructuredBuffer.Get(),
          g_texturesGpuStart, g_textureDescriptorCount, activeMeshes,
          g_meshStructuredBuffer.Get());
        if (dxrOk) {
          if (g_renderExportJob.active &&
              dxrTarget == g_exportRenderTarget.Get()) {
            g_exportRenderTargetState = D3D12_RESOURCE_STATE_RENDER_TARGET;
          }
          // Success DXR render - Draw Grid with depth checks
          if (!g_renderExportJob.active && g_drawGrid) {
            D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle =
                DX12Context::g_dsvHeap->GetCPUDescriptorHandleForHeapStart();
            DX12Context::g_commandList->ClearDepthStencilView(
                dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

            // 1. Scene Depth Pre-pass (populate depth buffer for grid
            // occlusion)
            DX12Context::g_commandList->OMSetRenderTargets(0, nullptr, FALSE,
                                                           &dsvHandle);
            DX12Context::g_commandList->SetGraphicsRootSignature(
                g_rootSignature.Get());
            RasterRenderer::DrawSceneDepthOnly(DX12Context::g_commandList.Get(),
                                               g_cameraConstantBuffer.Get(),
                                               sceneInstances);

            // 2. Draw Grid (test against the populated depth buffer)
            DX12Context::g_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE,
                                                           &dsvHandle);
            RasterRenderer::DrawGrid(DX12Context::g_commandList.Get(),
                                     g_cameraConstantBuffer.Get());
          }
          if (g_renderExportJob.active) {
            TR(DX12Context::g_commandList.Get(),
               DX12Context::g_renderTargets[DX12Context::g_frameIndex].Get(),
               D3D12_RESOURCE_STATE_PRESENT,
               D3D12_RESOURCE_STATE_RENDER_TARGET);
            FLOAT clearColor[] = {0.08f, 0.08f, 0.09f, 1.0f};
            DX12Context::g_commandList->ClearRenderTargetView(
                rtvHandle, clearColor, 0, nullptr);
            DX12Context::g_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE,
                                                           nullptr);
          }
        } else {
          // If RenderFrame failed, fall back to red clear
          TR(DX12Context::g_commandList.Get(),
             DX12Context::g_renderTargets[DX12Context::g_frameIndex].Get(),
             D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

          FLOAT clearColor[] = {0.8f, 0.2f, 0.2f,
                                1.0f}; // Red to indicate fallback
          DX12Context::g_commandList->ClearRenderTargetView(
              rtvHandle, clearColor, 0, nullptr);
          DX12Context::g_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE,
                                                         nullptr);
        }
      } else {
        // DXR mode was selected but the renderer isn't ready yet (missing
        // TLAS or state object).  Avoid leaving the previous raster frame
        // sitting on the screen by clearing to red and logging.
        if (g_verboseRenderLogs)
          fprintf(stderr, "Main: DXR path selected but IsReady()==false\n");
        TR(DX12Context::g_commandList.Get(),
           DX12Context::g_renderTargets[DX12Context::g_frameIndex].Get(),
           D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        FLOAT clearColor[] = {0.8f, 0.2f, 0.2f, 1.0f};
        DX12Context::g_commandList->ClearRenderTargetView(rtvHandle, clearColor,
                                                          0, nullptr);
        DX12Context::g_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE,
                                                       nullptr);
      }
      break;
    }

    case RenderMode::Raster: {
      // Fast Rasterization Path
      // Log to stderr only (controlled by verbose flag)
      if (g_verboseRenderLogs)
        fprintf(stderr, "Entering Raster Path\n");

      TR(DX12Context::g_commandList.Get(),
         DX12Context::g_renderTargets[DX12Context::g_frameIndex].Get(),
         D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

      D3D12_CPU_DESCRIPTOR_HANDLE rasterRtv = rtvHandle;
      bool rasterHdrReady = RasterRenderer::PrepareHdrRenderTarget(
          DX12Context::g_device.Get(), DX12Context::g_commandList.Get(),
          DX12Context::g_windowWidth, DX12Context::g_windowHeight, &rasterRtv);

      FLOAT clearColor[] = {0.0f, 0.0f, 0.0f, 1.0f};
      DX12Context::g_commandList->ClearRenderTargetView(rasterRtv, clearColor,
                                                        0, nullptr);

      // Clear depth buffer
      D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle =
          DX12Context::g_dsvHeap->GetCPUDescriptorHandleForHeapStart();
      DX12Context::g_commandList->ClearDepthStencilView(
          dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

      DX12Context::g_commandList->SetGraphicsRootSignature(
          g_rootSignature.Get());
      // Use camera constant buffer for proper camera movement
      if (g_cameraConstantBuffer) {
        DX12Context::g_commandList->SetGraphicsRootConstantBufferView(
            0, g_cameraConstantBuffer->GetGPUVirtualAddress());
      }

      // No demo triangle; ensure render target is bound for subsequent draws
      DX12Context::g_commandList->OMSetRenderTargets(1, &rasterRtv, FALSE,
                                                     &dsvHandle);

      // Bind global descriptor heap once for all raster calls
      ID3D12DescriptorHeap *heaps[] = {g_cbvSrvAllocator.Heap()};
      DX12Context::g_commandList->SetDescriptorHeaps(_countof(heaps), heaps);

      // Draw Skybox (Always passes depth, but doesn't write depth)
      if (g_cloudManager.NeedsBake()) {
        fprintf(stderr,
                "Main: calling g_cloudManager.BakeSky() before DrawSkybox\n");
        g_cloudManager.BakeSky(DX12Context::g_commandList.Get(),
                               g_cameraConstantBuffer.Get());
        fprintf(stderr, "Main: returned from g_cloudManager.BakeSky()\n");
      }
      RasterRenderer::DrawSkybox(DX12Context::g_commandList.Get(),
                                 g_cameraConstantBuffer.Get());

      // Draw ground grid (optional) via raster module
      if (g_drawGrid) {
        RasterRenderer::DrawGrid(DX12Context::g_commandList.Get(),
                                 g_cameraConstantBuffer.Get());
      }

      // Draw loaded meshes
      auto sceneInstances = Scene::GetInstances();
      if (!sceneInstances.empty() && RasterRenderer::g_meshPipelineState) {
        // Log to stderr only (controlled by verbose flag)
        if (g_verboseRenderLogs)
          fprintf(stderr, "Drawing %zu instances\n", sceneInstances.size());
        // Use the RasterRenderer mesh PSO (may output debug depth/uv depending
        // on compile defines)
        DX12Context::g_commandList->SetPipelineState(
            RasterRenderer::g_meshPipelineState.Get());
        DX12Context::g_commandList->IASetPrimitiveTopology(
            D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        // Ensure render targets are set for mesh rendering
        DX12Context::g_commandList->OMSetRenderTargets(1, &rasterRtv, FALSE,
                                                       &dsvHandle);
        // Use camera constant buffer for mesh rendering
        if (g_cameraConstantBuffer) {
          DX12Context::g_commandList->SetGraphicsRootConstantBufferView(
              0, g_cameraConstantBuffer->GetGPUVirtualAddress());
        }

        // Bind common textures and IBL once for all instances
        if (g_textureDescriptorCount > 0) {
          DX12Context::g_commandList->SetGraphicsRootDescriptorTable(
              1, g_texturesGpuStart);
        }
        if (IBLManager::Get().IsLoaded()) {
          DX12Context::g_commandList->SetGraphicsRootDescriptorTable(
              4, IBLManager::Get().GetGPUHandle());
        }

        // Draw all instances
        int lastMaterialIndex = -2;
        ID3D12Resource *lastVB = nullptr;
        ID3D12Resource *lastIB = nullptr;

        for (size_t i = 0; i < sceneInstances.size(); ++i) {
          const auto &inst = sceneInstances[i];
          const auto &gm = *inst.mesh;
          // Skip meshes that have been deleted or not properly initialized
          if (!gm.vertexBuffer || !gm.indexBuffer || gm.ibView.SizeInBytes == 0)
            continue;

          // Set instance transform
          DX12Context::g_commandList->SetGraphicsRoot32BitConstants(
              3, 16, &inst.transform, 0);

          // material binding...
          if (gm.materialIndex >= 0 &&
              gm.materialIndex < (int)g_loadedMaterials.size()) {

            if (gm.materialIndex != lastMaterialIndex) {
              struct MaterialCB {
                float diffuseColor[4];
                float reflectionColor[4];
                float refractionColor[4];
                float emissiveColor[4];
                int textureIndices[4];
                int emissiveAndPad[4];
                float extraParams[4];
                float archvizParams0[4];
                float uvTransform[4];
                float triPlanarParams[4];
              } matCB;

              const auto &srcMat = g_loadedMaterials[gm.materialIndex];
              memcpy(matCB.diffuseColor, srcMat.diffuseColor, 16);
              memcpy(matCB.reflectionColor, srcMat.reflectionColor, 12);
              matCB.reflectionColor[3] = srcMat.reflectionGlossiness;

              memcpy(matCB.refractionColor, srcMat.refractionColor, 12);
              matCB.refractionColor[3] = srcMat.refractionGlossiness;

              memcpy(matCB.emissiveColor, srcMat.emissiveColor, 12);
              matCB.emissiveColor[3] = srcMat.ior;

              matCB.textureIndices[0] = srcMat.diffuseTexture;
              matCB.textureIndices[1] = srcMat.reflectionTexture;
              matCB.textureIndices[2] = srcMat.normalTexture;
              matCB.textureIndices[3] = srcMat.refractionTexture;

              matCB.emissiveAndPad[0] = srcMat.emissiveTexture;
              matCB.emissiveAndPad[1] = srcMat.occlusionTexture;
              matCB.emissiveAndPad[2] = srcMat.metalRoughTexture;
              matCB.emissiveAndPad[3] = 0;

              matCB.extraParams[0] = srcMat.metalness;
              matCB.extraParams[1] = srcMat.emissiveIntensity;
              matCB.extraParams[2] = 0.0f;
              matCB.extraParams[3] = 0.0f;

              matCB.archvizParams0[0] = srcMat.clearcoat;
              matCB.archvizParams0[1] = srcMat.clearcoatRoughness;
              matCB.archvizParams0[2] = srcMat.thinWalled;
              matCB.archvizParams0[3] = srcMat.translucency;

              matCB.uvTransform[0] = srcMat.uvScale[0];
              matCB.uvTransform[1] = srcMat.uvScale[1];
              matCB.uvTransform[2] = srcMat.uvOffset[0];
              matCB.uvTransform[3] = srcMat.uvOffset[1];

              matCB.triPlanarParams[0] = srcMat.triPlanarEnabled;
              matCB.triPlanarParams[1] = srcMat.triPlanarScale;
              matCB.triPlanarParams[2] = srcMat.triPlanarSharpness;
              matCB.triPlanarParams[3] = srcMat.triPlanarNormalStrength;

              if (g_materialCbMappedData) {
                const UINT64 matSlotSize = (sizeof(MaterialCB) + 255) & ~255;
                // Use material index based slotting to capitalize on shared
                // materials
                UINT64 offset = (gm.materialIndex % 16384) * matSlotSize;
                memcpy((uint8_t *)g_materialCbMappedData + offset, &matCB,
                       sizeof(matCB));
                DX12Context::g_commandList->SetGraphicsRootConstantBufferView(
                    2,
                    g_materialConstantBuffer->GetGPUVirtualAddress() + offset);
              }
              lastMaterialIndex = gm.materialIndex;
            }
          }

          if (gm.vertexBuffer.Get() != lastVB) {
            DX12Context::g_commandList->IASetVertexBuffers(0, 1, &gm.vbView);
            lastVB = gm.vertexBuffer.Get();
          }
          if (gm.indexBuffer.Get() != lastIB) {
            DX12Context::g_commandList->IASetIndexBuffer(&gm.ibView);
            lastIB = gm.indexBuffer.Get();
          }

          if (gm.ibView.SizeInBytes > 0) {
            DX12Context::g_commandList->DrawIndexedInstanced(
                gm.ibView.SizeInBytes / 4, 1, 0, 0, 0);
          }
        }
      }

      if (rasterHdrReady) {
        RasterRenderer::TonemapHdrToBackbuffer(
            DX12Context::g_device.Get(), DX12Context::g_commandList.Get(),
            DX12Context::g_renderTargets[DX12Context::g_frameIndex].Get(),
            DX12Context::g_windowWidth, DX12Context::g_windowHeight);
      }

      DxrRenderer::EndFrameProfiling(DX12Context::g_commandList.Get());
      break;
    }
    }

    // Render ImGui (Overlay on top of whatever was drawn)
    if (g_renderExportJob.active && g_exportRenderTarget &&
        g_exportPreviewSrvGpu.ptr != 0 &&
        g_exportRenderTargetState !=
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
      TR(DX12Context::g_commandList.Get(), g_exportRenderTarget.Get(),
         g_exportRenderTargetState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
      g_exportRenderTargetState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    // Ensure UI always targets the swapchain backbuffer RTV.
    DX12Context::g_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE,
                             nullptr);

    ID3D12DescriptorHeap *ppHeaps[] = {g_cbvSrvAllocator.Heap()};
    DX12Context::g_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(),
                                  DX12Context::g_commandList.Get());

    // Handle multi-viewport windows (platform windows) when enabled so
    // ImGui viewports receive input and are properly rendered.
    ImGuiIO &io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
      ImGui::UpdatePlatformWindows();
      ImGui::RenderPlatformWindowsDefault();
    }

    if (g_renderExportJob.active && g_exportRenderTarget &&
        g_exportRenderTargetState != D3D12_RESOURCE_STATE_PRESENT) {
      TR(DX12Context::g_commandList.Get(), g_exportRenderTarget.Get(),
         g_exportRenderTargetState, D3D12_RESOURCE_STATE_PRESENT);
      g_exportRenderTargetState = D3D12_RESOURCE_STATE_PRESENT;
    }

    // Transition back to present
    TR(DX12Context::g_commandList.Get(),
       DX12Context::g_renderTargets[DX12Context::g_frameIndex].Get(),
       D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

    ThrowIfFailed(DX12Context::g_commandList->Close());
  };

  auto RecreateDevice = [&]() {
    // Invalidate ImGui device objects before device reset
    ImGui_ImplDX12_InvalidateDeviceObjects();

    // Release GPU resources
    g_pipelineState.Reset();
    g_rootSignature.Reset();
    g_vertexBuffer.Reset();
    g_constantBuffer.Reset();
    DX12Context::g_commandList.Reset();

    for (UINT i = 0; i < FrameCount; ++i) {
      DX12Context::g_frameResources[i].commandAllocator.Reset();
    }

    // Attempt reinitialization
    if (!InitApplication(g_hwnd)) {
      MessageBoxA(nullptr, "Failed to recreate D3D12 device.",
                  "Device Recovery", MB_OK | MB_ICONERROR);
      ExitProcess(static_cast<UINT>(-1));
    }
  };

  auto CheckDeviceRemoved = [&]() {
    HRESULT reason = DX12Context::g_device->GetDeviceRemovedReason();
    if (FAILED(reason)) {
      // Attempt to recreate the device
      RecreateDevice();
      return true;
    }
    return false;
  };

  // Log entering main loop (stderr only)
  fprintf(stderr, "Entering main loop\n");
  fflush(stderr);

  // Setup timing for camera movement
  static auto prevTime = std::chrono::high_resolution_clock::now();

  // Enter main loop (simple, no extra SEH wrappers)
  while (msg.message != WM_QUIT) {
    // fprintf(stderr, "MainLoop: start iteration\n");
    fflush(stderr);
    if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
      if (msg.message == WM_QUIT || g_appClosing)
        break;
      continue;
    }
    if (g_appClosing)
      break;

    // Compute delta time
    auto now = std::chrono::high_resolution_clock::now();
    std::chrono::duration<float> dtDur = now - prevTime;
    float dt = dtDur.count();
    prevTime = now;

    // FPS accumulation (smoothed, updated every 0.25s)
    static float g_fps = 0.0f;
    static float g_fpsTimer = 0.0f;
    static int g_fpsFrames = 0;
    ++g_fpsFrames;
    g_fpsTimer += dt;
    if (g_fpsTimer >= 0.25f) {
      g_fps = static_cast<float>(g_fpsFrames) / g_fpsTimer;
      g_fpsFrames = 0;
      g_fpsTimer = 0.0f;
    }

    Input::Update(dt);

    if (g_renderExportJob.active) {
      g_currentRenderMode = RenderMode::DXR;

      const UINT currentSpp = DxrRenderer::GetDisplayedSampleCount();
      const float currentNoise = DxrRenderer::GetCurrentNoiseLevel();
      const bool sppDone = currentSpp >= (UINT)g_renderExportJob.targetMaxSpp;
      const bool noiseDone =
          (currentSpp >= g_renderExportJob.minSppBeforeNoiseStop) &&
          (currentNoise > 0.0f) &&
          (currentNoise <= g_renderExportJob.targetNoiseThreshold * 0.90f);

      const bool reachedEnd = sppDone || noiseDone;

      if (reachedEnd && !g_renderExportJob.completionArmed) {
        g_renderExportJob.completionArmed = true;
        g_renderExportJob.completionFrames = 0;
        g_renderExportJob.settleFramesRemaining =
            (g_renderExportSettings.denoiserIndex == 0) ? 1 : 3;
      }

      if (g_renderExportJob.completionArmed) {
        ++g_renderExportJob.completionFrames;
        if (g_renderExportJob.settleFramesRemaining > 0) {
          --g_renderExportJob.settleFramesRemaining;
        } else {
          const bool denoiserEnabled =
              (g_renderExportSettings.denoiserIndex != 0);
          const bool denoisedReady = DxrRenderer::HasDenoisedOutput();
          // Wait for the one-shot denoiser to produce output. Keep a timeout so
          // export cannot hang forever on denoiser failures.
          if (denoiserEnabled && !denoisedReady &&
              g_renderExportJob.completionFrames < 240) {
            // keep waiting
          } else {
            const bool exported = DxrRenderer::ExportTonemappedFrameToPng(
                g_renderExportJob.outputPath);
            const std::string outPathUtf8 =
                WStringToUtf8(g_renderExportJob.outputPath);
            if (exported) {
              g_renderExportStatus = "Saved: " + outPathUtf8;
              fprintf(stderr, "Render export finished: %s\n",
                      outPathUtf8.c_str());
            } else {
              g_renderExportStatus = "Export failed: " + outPathUtf8;
              fprintf(stderr, "Render export failed: %s\n",
                      outPathUtf8.c_str());
            }
            RestoreRenderExportState();
          }
        }
      }
    }

    // Update camera forward from yaw/pitch
    g_cameraData.forward[0] = (cosf(g_camPitch) * sinf(g_camYaw));
    g_cameraData.forward[1] = sinf(g_camPitch);
    g_cameraData.forward[2] = (cosf(g_camPitch) * -cosf(g_camYaw));

    // Ensure aspect matches the window and update camera CB on GPU
    g_cameraData.aspect =
        (float)DX12Context::g_windowWidth / (float)DX12Context::g_windowHeight;
#ifdef _DEBUG
    g_cameraData.debugMode = (float)g_debugMode;
#else
    g_debugMode = 0;
    g_cameraData.debugMode = 0.0f;
    g_cameraData.debugVisualizationMode = 0.0f;
#endif
    g_cameraData.lightCount = (float)DxrRenderer::GetLightCount();
    g_cameraData.frameCount = (float)DxrRenderer::GetDisplayedSampleCount();
    const bool fileIblActive =
        (IBLManager::Get().GetIBLSource() == IBLManager::IBLSource::File);
    const bool effectiveCloudRendering =
        g_cloudRenderingEnabled && !fileIblActive;
    g_cameraData.cloudRenderingEnabled = effectiveCloudRendering ? 1.0f : 0.0f;
    UpdateCameraCB();

    // Update Cloud Manager (uploads changed params to GPU)
    g_cloudManager.Update(dt, DX12Context::g_frameIndex);

    // Editor UI (moved to editor_ui.cpp)
    DrawEditorUI(g_fps, g_timeOfDay, g_northOffset, g_latitudeDeg,
           g_dayOfYear);

    // fprintf(stderr, "MainLoop: PopulateCommandList start\n");
    PopulateCommandList();
    // fprintf(stderr, "MainLoop: PopulateCommandList done\n");

    ID3D12CommandList *ppCommandLists[] = {DX12Context::g_commandList.Get()};
    DX12Context::g_commandQueue->ExecuteCommandLists(_countof(ppCommandLists),
                                                     ppCommandLists);
    // fprintf(stderr, "MainLoop: ExecuteCommandLists done\n");

    // fprintf(stderr, "MainLoop: Present start\n");
    ThrowIfFailed(DX12Context::g_swapChain->Present(1, 0));
    // fprintf(stderr, "MainLoop: Present done\n");
    //  Signal and increment the fence value.
    const UINT64 currentFenceValue =
        DX12Context::g_fenceValues[DX12Context::g_frameIndex];
    ThrowIfFailed(DX12Context::g_commandQueue->Signal(
        DX12Context::g_fence.Get(), currentFenceValue));
    DX12Context::g_fenceValues[DX12Context::g_frameIndex]++;

    // Wait for previous frame
    DX12Context::WaitForPreviousFrame();

    // Check if the GPU was removed (TDR) during the last frame
    if (CheckDeviceRemoved()) {
      fprintf(stderr, "MainLoop: Device was removed. Re-initializing...\n");
      continue; // Start fresh next iteration
    }

    // fprintf(stderr, "MainLoop: end iteration\n");
  }

  // Shutdown ImGui and cleanup
  // Persist panel visibility so user window open/closed state is remembered
  SavePanelVisibility();
  ImGui_ImplDX12_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();

  // Cleanup fence event
  CloseHandle(DX12Context::g_fenceEvent);

  return 0;
}
