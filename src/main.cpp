#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <windows.h>
#include <wrl.h>

#ifdef _DEBUG
#include <d3d12sdklayers.h>
#endif
#include "ImGuizmo.h"
#include "assets/asset_loader.h"
#include "clouds.h" // Add clouds
#include "d3d12_helpers.h"
#include "dxc_wrapper.h"
#include "dxr_helpers.h"
#include "dxr_renderer.h"
#include "file_import.h"
#include "ibl_manager.h"
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"
#include "light.h"
#include "material_editor.h"
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

using Microsoft::WRL::ComPtr;

// Top-level exception handler for debug builds (file scope)
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

static const UINT FrameCount = 2;

// RenderMode is now defined in scene.h

RenderMode g_currentRenderMode = RenderMode::Raster;
static bool g_showRenderModeWindow = false;
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

ComPtr<ID3D12Device> g_device;
static ComPtr<ID3D12CommandQueue> g_commandQueue;
static ComPtr<IDXGISwapChain3> g_swapChain;

static ComPtr<ID3D12Resource> g_renderTargets[FrameCount];
static ComPtr<ID3D12DescriptorHeap> g_rtvHeap;
static UINT g_rtvDescriptorSize = 0;

// Depth buffer for raster mode
static ComPtr<ID3D12Resource> g_depthBuffer;
static ComPtr<ID3D12DescriptorHeap> g_dsvHeap;
static UINT g_dsvDescriptorSize = 0;

static ComPtr<ID3D12DescriptorHeap> g_imguiHeap;
DescriptorHeapAllocator g_cbvSrvAllocator;
static FrameResource g_frameResources[FrameCount];
static ComPtr<ID3D12GraphicsCommandList> g_commandList;
static UINT g_frameIndex = 0;
static HWND g_hwnd = nullptr;

// Window dimensions
static UINT g_windowWidth = 1280;
static UINT g_windowHeight = 720;
static bool g_appClosing = false;

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
static ComPtr<ID3D12Resource>
    g_materialStructuredBuffer; // Tightly packed for DXR
static ComPtr<ID3D12Resource>
    g_meshStructuredBuffer; // Mesh mapping info for DXR
static bool g_showAssetsWindow =
    false; // Controls visibility of the Assets panel (can be closed/reopened)
static bool g_showMaterialEditor = false;
static bool g_showControlsWindow =
    false; // Controls visibility of the Controls panel (can be closed/reopened)
static bool g_forceUncollapse =
    false; // When true, next Assets window will be forced open and focused
static std::string g_lastAssetStatus; // Human-readable status for the Assets UI
static std::string
    g_selectedAssetPath; // Path chosen by Open dialog (not yet imported)
static int g_debugMode =
    0; // 0=None, 1=Albedo, 2=Normal, 3=Emissive, 4=Glossiness, 5=Refl. Color,
       // 6=Metalness, 7=AO, 8=Motion Vectors

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
ComPtr<ID3D12RootSignature> g_rootSignature;
static ComPtr<ID3D12PipelineState> g_pipelineState;
static ComPtr<ID3D12Resource> g_vertexBuffer;
static D3D12_VERTEX_BUFFER_VIEW g_vertexBufferView = {};
static ComPtr<ID3D12Resource> g_constantBuffer;
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
StreamlineManager g_streamline;

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
  g_commandQueue->ExecuteCommandLists(1, lists);

  // Wait for completion
  ComPtr<ID3D12Fence> fence;
  ThrowIfFailed(
      g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
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
  ThrowIfFailed(g_device->CreateCommittedResource(
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
  ThrowIfFailed(g_device->CreateCommittedResource(
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
  g_device->CreateShaderResourceView(texture.Get(), &srvDesc, alloc.cpu);

  // Copy from upload buffer to texture
  ComPtr<ID3D12CommandAllocator> cmdAlloc;
  ComPtr<ID3D12GraphicsCommandList> cmdList;
  ThrowIfFailed(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                 IID_PPV_ARGS(&cmdAlloc)));
  ThrowIfFailed(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                            cmdAlloc.Get(), nullptr,
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

bool InitD3D12(HWND hwnd) {

  g_hwnd = hwnd;
  UINT dxgiFactoryFlags = 0;

  // Streamline must be initialized before any DXGI/D3D calls.
  const bool streamlineReady = g_streamline.InitializeEarly();

  EnableD3D12DebugLayer();

  ComPtr<IDXGIFactory4> factory;
  {
    HRESULT hrFactory = E_NOINTERFACE;
    if (streamlineReady) {
      hrFactory = g_streamline.CreateDXGIFactory2(dxgiFactoryFlags,
                                                  IID_PPV_ARGS(&factory));
    }
    if (FAILED(hrFactory)) {
      ThrowIfFailed(
          ::CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));
    }
  }

  auto CreateDevice = [&](IUnknown *adapter, REFIID riid,
                          void **ppDevice) -> HRESULT {
    if (streamlineReady) {
      HRESULT hrSL = g_streamline.D3D12CreateDevice(
          adapter, D3D_FEATURE_LEVEL_11_0, riid, ppDevice);
      if (SUCCEEDED(hrSL))
        return hrSL;
    }
    return ::D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, riid, ppDevice);
  };

  // Try to find a hardware adapter that supports D3D12
  ComPtr<IDXGIAdapter1> hardwareAdapter;
  GetHardwareAdapter(factory.Get(), &hardwareAdapter);

  // Print adapter info (vendor/name/memory)
  if (hardwareAdapter) {
    DXGI_ADAPTER_DESC1 desc;
    hardwareAdapter->GetDesc1(&desc);
    char descBuf[128];
    size_t converted = 0;
    wcstombs_s(&converted, descBuf, desc.Description, sizeof(descBuf));
    fprintf(stderr,
            "InitD3D12: Using adapter: %s (VendorId=0x%04x, DeviceId=0x%04x, "
            "DedicatedVidMem=%llu bytes)\n",
            descBuf, (unsigned)desc.VendorId, (unsigned)desc.DeviceId,
            (unsigned long long)desc.DedicatedVideoMemory);
  } else {
    fprintf(stderr,
            "InitD3D12: No hardware adapter selected (will try WARP)\n");
  }

  HRESULT hr = E_FAIL;
  if (hardwareAdapter) {
    hr = CreateDevice(hardwareAdapter.Get(), IID_PPV_ARGS(&g_device));
  }

  if (SUCCEEDED(hr) && streamlineReady) {
    g_streamline.OnD3D12DeviceCreated(g_device.Get());
  }

  // Check DXR Support
  if (SUCCEEDED(hr)) {
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
    if (SUCCEEDED(g_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5,
                                                &options5, sizeof(options5)))) {
      if (options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0) {
        g_rayTracingSupported = true;
        fprintf(stderr, "DXR Ray Tracing Supported (probe)\n");
        // Initialize DXR probe with device; command queue/fence will be
        // attached later
        DxrRenderer::Initialize(g_device.Get());
        DxrRenderer::CreateRayTracingPipeline(g_windowWidth, g_windowHeight);
        // Log to stderr only
        fprintf(stderr, "InitD3D12: CreateRayTracingPipeline finished\n");
      }
    }
  }

  if (FAILED(hr)) {
    // Fall back to default adapter/device or WARP
    hr = CreateDevice(nullptr, IID_PPV_ARGS(&g_device));

    if (SUCCEEDED(hr) && streamlineReady) {
      g_streamline.OnD3D12DeviceCreated(g_device.Get());
    }

    if (FAILED(hr)) {
      ComPtr<IDXGIAdapter> warpAdapter;
      ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));
      ThrowIfFailed(CreateDevice(warpAdapter.Get(), IID_PPV_ARGS(&g_device)));

      if (streamlineReady) {
        g_streamline.OnD3D12DeviceCreated(g_device.Get());
      }
    }
  }

  // Provide Streamline manager to DXR module (optional feature).
  DxrRenderer::SetStreamlineManager(&g_streamline);

  // Log: about to create command queue (stderr only)
  fprintf(stderr, "InitD3D12: Before CreateCommandQueue\n");

  // Create command queue
  D3D12_COMMAND_QUEUE_DESC queueDesc = {};
  queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

  hr = g_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&g_commandQueue));
  if (FAILED(hr)) {
    // Log to stderr only
    fprintf(stderr, "InitD3D12: CreateCommandQueue failed 0x%08x\n",
            (unsigned)hr);

#ifdef _DEBUG
    // Print device removed reason (helps diagnose driver/device removal)
    HRESULT removedReason = g_device->GetDeviceRemovedReason();
    fprintf(stderr, "InitD3D12: GetDeviceRemovedReason() = 0x%08x\n",
            (unsigned)removedReason);

    // If info queue available, dump recent messages
    ComPtr<ID3D12InfoQueue> infoQueue;
    if (SUCCEEDED(g_device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
      UINT64 num = infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();
      for (UINT64 i = 0; i < num; ++i) {
        SIZE_T messageLength = 0;
        infoQueue->GetMessage(i, nullptr, &messageLength);
        std::vector<char> message(messageLength);
        D3D12_MESSAGE *pMsg = reinterpret_cast<D3D12_MESSAGE *>(message.data());
        infoQueue->GetMessage(i, pMsg, &messageLength);
        fprintf(stderr, "D3D12 INFO: Category=%d Severity=%d ID=%d: %s\n",
                (int)pMsg->Category, (int)pMsg->Severity, (int)pMsg->ID,
                pMsg->pDescription);
      }
    }
#endif

    return false;
  }

  // Log: command queue created (stderr only)
  fprintf(stderr, "InitD3D12: CreateCommandQueue succeeded\n");

  // Create swap chain
  RECT clientRect;
  GetClientRect(hwnd, &clientRect);
  UINT clientWidth = clientRect.right - clientRect.left;
  UINT clientHeight = clientRect.bottom - clientRect.top;

  g_windowWidth = clientWidth;
  g_windowHeight = clientHeight;

  DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
  swapChainDesc.BufferCount = 2;
  swapChainDesc.Width = clientWidth;
  swapChainDesc.Height = clientHeight;
  swapChainDesc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
  swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  swapChainDesc.SampleDesc.Count = 1;

  ComPtr<IDXGISwapChain1> swapChain1;
  // Log to stderr only
  fprintf(stderr, "InitD3D12: Before CreateSwapChainForHwnd\n");

  HRESULT hrSwap = factory->CreateSwapChainForHwnd(g_commandQueue.Get(), hwnd,
                                                   &swapChainDesc, nullptr,
                                                   nullptr, &swapChain1);
  if (FAILED(hrSwap)) {
    // Log to stderr only
    fprintf(stderr, "InitD3D12: CreateSwapChainForHwnd failed 0x%08x\n",
            (unsigned)hrSwap);
    return false;
  }

  HRESULT hrAs = swapChain1.As(&g_swapChain);
  if (FAILED(hrAs)) {
    // Log to stderr only
    fprintf(stderr, "InitD3D12: swapChain1.As failed 0x%08x\n", (unsigned)hrAs);
    return false;
  }

  // Log: swap chain created (stderr only)
  fprintf(stderr, "InitD3D12: Swap chain created\n");

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

  // Create DSV descriptor heap for depth buffer
  D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
  dsvHeapDesc.NumDescriptors = 1;
  dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
  dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  ThrowIfFailed(
      g_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&g_dsvHeap)));
  g_dsvDescriptorSize = g_device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

  // Create depth buffer
  D3D12_RESOURCE_DESC depthBufferDesc = {};
  depthBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  depthBufferDesc.Alignment = 0;
  depthBufferDesc.Width = g_windowWidth;
  depthBufferDesc.Height = g_windowHeight;
  depthBufferDesc.DepthOrArraySize = 1;
  depthBufferDesc.MipLevels = 1;
  depthBufferDesc.Format = DXGI_FORMAT_D32_FLOAT;
  depthBufferDesc.SampleDesc.Count = 1;
  depthBufferDesc.SampleDesc.Quality = 0;
  depthBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  depthBufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

  D3D12_CLEAR_VALUE depthClear = {};
  depthClear.Format = DXGI_FORMAT_D32_FLOAT;
  depthClear.DepthStencil.Depth = 1.0f;
  depthClear.DepthStencil.Stencil = 0;

  D3D12_HEAP_PROPERTIES depthHeapProps = {};
  depthHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
  depthHeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
  depthHeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
  depthHeapProps.CreationNodeMask = 1;
  depthHeapProps.VisibleNodeMask = 1;

  ThrowIfFailed(g_device->CreateCommittedResource(
      &depthHeapProps, D3D12_HEAP_FLAG_NONE, &depthBufferDesc,
      D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear,
      IID_PPV_ARGS(&g_depthBuffer)));

  // Create DSV
  D3D12_DEPTH_STENCIL_VIEW_DESC dsvView = {};
  dsvView.Format = DXGI_FORMAT_D32_FLOAT;
  dsvView.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
  dsvView.Flags = D3D12_DSV_FLAG_NONE;

  g_device->CreateDepthStencilView(
      g_depthBuffer.Get(), &dsvView,
      g_dsvHeap->GetCPUDescriptorHandleForHeapStart());

  D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
      g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
  D3D12_CPU_DESCRIPTOR_HANDLE rtvHandleStart = rtvHandle;
  for (UINT i = 0; i < FrameCount; ++i) {
    HRESULT hrBuf =
        g_swapChain->GetBuffer(i, IID_PPV_ARGS(&g_renderTargets[i]));
    if (FAILED(hrBuf)) {
      // Log to stderr only
      fprintf(stderr, "InitD3D12: GetBuffer failed for index %u: 0x%08x\n", i,
              (unsigned)hrBuf);
      return false;
    }
    g_device->CreateRenderTargetView(g_renderTargets[i].Get(), nullptr,
                                     rtvHandle);
    rtvHandle.ptr =
        rtvHandleStart.ptr + (SIZE_T)((i + 1) * g_rtvDescriptorSize);
  }

  // Log: RTVs created (stderr only)
  fprintf(stderr, "InitD3D12: RTVs created\n");

  // Log: before CBV/SRV allocator init (stderr only)
  fprintf(stderr, "InitD3D12: Before CBV/SRV allocator Init\n");
  // Initialize descriptor allocator for CBV/SRV/UAV
  g_cbvSrvAllocator.Init(g_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                         65536, FrameCount);
  // Log: CBV/SRV allocator initialized (stderr only)
  fprintf(stderr, "InitD3D12: CBV/SRV allocator initialized\n");

  // Create per-frame resources (command allocators + fence values)
  for (UINT i = 0; i < FrameCount; ++i) {
    HRESULT hrAlloc = g_device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&g_frameResources[i].commandAllocator));
    if (FAILED(hrAlloc)) {
      // Log to stderr only
      fprintf(stderr,
              "InitD3D12: CreateCommandAllocator failed for frame %u: 0x%08x\n",
              i, (unsigned)hrAlloc);
      return false;
    }
    g_frameResources[i].fenceValue = 0;
    g_frameResources[i].transientDescriptorOffset = 0;
  }
  // Log: per-frame command allocators created (stderr only)
  fprintf(stderr, "InitD3D12: per-frame command allocators created\n");

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

  // Now that fence and event are valid, attach command queue & fence to DXR
  // renderer
  DxrRenderer::SetCommandQueue(g_commandQueue.Get(), g_fence.Get(),
                               g_fenceValues, &g_frameIndex, g_fenceEvent);

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

  ThrowIfFailed(g_device->CreateGraphicsPipelineState(
      &psoDesc, IID_PPV_ARGS(&g_pipelineState)));

  // Create grid resources using raster module
  RasterRenderer::CreateGridResources(g_device.Get(), g_gridThickness);

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
    HRESULT hrMesh = g_device->CreateGraphicsPipelineState(
        &meshPsoDesc, IID_PPV_ARGS(&RasterRenderer::g_meshPipelineState));
    if (FAILED(hrMesh)) {
      fprintf(stderr,
              "InitD3D12: CreateGraphicsPipelineState (mesh) failed: 0x%08x\n",
              (unsigned)hrMesh);
#ifdef _DEBUG
      ComPtr<ID3D12InfoQueue> infoQueue;
      if (SUCCEEDED(g_device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
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
  RasterRenderer::RecreateMeshPipeline(g_device.Get(), g_rootSignature.Get());

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
      ThrowIfFailed(g_device->CreateGraphicsPipelineState(
          &simplePso, IID_PPV_ARGS(&g_meshSimplePipelineState)));
    }
  }

  // --- Initialize ImGui ---
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  ImGui_ImplWin32_Init(hwnd);
  // Initialize DX12 backend with the main CBV/SRV/UAV heap so we can show
  // thumbnails using existing engine texture SRVs.
  DescriptorAllocation imguiFontAlloc = g_cbvSrvAllocator.AllocatePersistent(1);
  ImGui_ImplDX12_Init(g_device.Get(), FrameCount, DXGI_FORMAT_R10G10B10A2_UNORM,
                      g_cbvSrvAllocator.Heap(), imguiFontAlloc.cpu,
                      imguiFontAlloc.gpu);

  ImGui_ImplDX12_CreateDeviceObjects();

  // Initialize asset loader with device & command queue so it can perform
  // uploads
  Asset::Initialize(g_device.Get(), g_commandQueue.Get());

  // Initialize IBL Manager and load default environment map
  IBLManager::Get().Initialize(g_device.Get(), g_commandQueue.Get());
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
      g_device->CreateShaderResourceView(
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
      g_device->CreateShaderResourceView(nullptr, &nullSrvDesc, alloc.cpu);
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
  ThrowIfFailed(g_device->CreateCommittedResource(
      &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &matCbDesc,
      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
      IID_PPV_ARGS(&g_materialConstantBuffer)));

  // Material Structured Buffer for DXR (tightly packed, no 256B alignment)
  {
    const UINT64 matSbSize = sizeof(MaterialCB) * 16384;
    D3D12_RESOURCE_DESC matSbDesc = matCbDesc;
    matSbDesc.Width = matSbSize;
    ThrowIfFailed(g_device->CreateCommittedResource(
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
    ThrowIfFailed(g_device->CreateCommittedResource(
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

    ThrowIfFailed(g_device->CreateCommittedResource(
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
    ThrowIfFailed(g_device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&tempAlloc)));
    ComPtr<ID3D12GraphicsCommandList> tempList;
    ThrowIfFailed(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              tempAlloc.Get(), nullptr,
                                              IID_PPV_ARGS(&tempList)));

    g_cloudManager.Initialize(g_device.Get(), tempList.Get());

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

void WaitForPreviousFrame() {
  const UINT64 currentFenceValue = g_fenceValues[g_frameIndex];
  ThrowIfFailed(g_commandQueue->Signal(g_fence.Get(), currentFenceValue));

  // Update index
  g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();

  // If the next frame is not ready to be rendered yet, wait until it is
  // signaled.
  if (g_fence->GetCompletedValue() < g_fenceValues[g_frameIndex]) {
    ThrowIfFailed(g_fence->SetEventOnCompletion(g_fenceValues[g_frameIndex],
                                                g_fenceEvent));
    const DWORD waitMs = g_appClosing ? 50u : INFINITE;
    DWORD wr = WaitForSingleObject(g_fenceEvent, waitMs);
    if (wr == WAIT_TIMEOUT && g_appClosing) {
      return;
    }
  }

  // Set the fence value for the next frame.
  g_fenceValues[g_frameIndex] = currentFenceValue + 1;
}

void WaitGPUIdle() {
  if (!g_commandQueue || !g_fence || !g_fenceEvent)
    return;
  const UINT64 waitValue = g_fenceValues[g_frameIndex] + 100;
  HRESULT hr = g_commandQueue->Signal(g_fence.Get(), waitValue);
  if (FAILED(hr)) {
    if (g_device->GetDeviceRemovedReason() != S_OK) {
      fprintf(stderr, "WaitGPUIdle: Signal failed due to Device Removal\n");
    }
    return;
  }
  g_fence->SetEventOnCompletion(waitValue, g_fenceEvent);
  if (WaitForSingleObject(g_fenceEvent, 5000) == WAIT_TIMEOUT) {
    fprintf(
        stderr,
        "WaitGPUIdle: Timeout waiting for GPU idle (5s). GPU might be hung.\n");
  }
  for (UINT i = 0; i < FrameCount; ++i) {
    g_fenceValues[i] = waitValue + 1;
  }
}

void ResizeSwapChain(UINT width, UINT height) {
  if (width == 0 || height == 0)
    return;

  // Wait for GPU to finish
  WaitGPUIdle();

  // Release render targets
  for (UINT i = 0; i < FrameCount; ++i) {
    g_renderTargets[i].Reset();
  }
  g_depthBuffer.Reset();

  // Resize swap chain
  DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
  g_swapChain->GetDesc(&swapChainDesc);
  ThrowIfFailed(g_swapChain->ResizeBuffers(FrameCount, width, height,
                                           swapChainDesc.BufferDesc.Format,
                                           swapChainDesc.Flags));

  g_windowWidth = width;
  g_windowHeight = height;

  // Recreate render targets
  D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
      g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
  for (UINT i = 0; i < FrameCount; ++i) {
    ThrowIfFailed(g_swapChain->GetBuffer(i, IID_PPV_ARGS(&g_renderTargets[i])));
    g_device->CreateRenderTargetView(g_renderTargets[i].Get(), nullptr,
                                     rtvHandle);
    rtvHandle.ptr += g_rtvDescriptorSize;
  }

  // If DXR is active, recreate its pipeline/output texture to match new size
  DxrRenderer::CreateRayTracingPipeline(width, height);

  // Recreate depth buffer
  D3D12_RESOURCE_DESC depthDesc = {};
  depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  depthDesc.Width = width;
  depthDesc.Height = height;
  depthDesc.DepthOrArraySize = 1;
  depthDesc.MipLevels = 1;
  depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
  depthDesc.SampleDesc.Count = 1;
  depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

  D3D12_HEAP_PROPERTIES heapProps = {};
  heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

  D3D12_CLEAR_VALUE clearValue = {};
  clearValue.Format = DXGI_FORMAT_D32_FLOAT;
  clearValue.DepthStencil.Depth = 1.0f;

  ThrowIfFailed(g_device->CreateCommittedResource(
      &heapProps, D3D12_HEAP_FLAG_NONE, &depthDesc,
      D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue,
      IID_PPV_ARGS(&g_depthBuffer)));

  D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
  dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
  dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

  g_device->CreateDepthStencilView(
      g_depthBuffer.Get(), &dsvDesc,
      g_dsvHeap->GetCPUDescriptorHandleForHeapStart());

  g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();
}

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
    if (!g_appClosing && g_swapChain && wParam != SIZE_MINIMIZED) {
      UINT width = LOWORD(lParam);
      UINT height = HIWORD(lParam);
      ResizeSwapChain(width, height);
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

  HWND hwnd = CreateWindowExW(0, CLASS_NAME, L"project-render - DX12 Starter",
                              WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              1280, 720, nullptr, nullptr, hInstance, nullptr);

  if (!hwnd) {
    return 0;
  }

  ShowWindow(hwnd, nCmdShow);

  EnforceReleaseDebugFlags();

  // Log that we showed the window (stderr only)
  fprintf(stderr, "ShowWindow called\n");

  if (!InitD3D12(hwnd)) {
    MessageBoxA(nullptr, "Failed to initialize D3D12", "Error",
                MB_OK | MB_ICONERROR);
    return -1;
  }

  // Log successful D3D12 initialization (stderr only)
  fprintf(stderr, "InitD3D12 returned OK\n");

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
  static float g_northOffset = 45.0f;

  // Basic message loop + simple render
  MSG msg = {};

  auto PopulateCommandList = [&]() {
    // Update Sky Parameters (Run every frame to ensure consistency)
    {
      const float PI = 3.14159265f;
      const float DEG2RAD = PI / 180.0f;

      // Simple sun path logic
      float hourArg = (g_timeOfDay - 12.0f) / 6.0f;
      float elRad = std::cos(hourArg * (PI / 2.0f)) * (PI / 2.0f);
      if (elRad < 0)
        elRad = 0;

      float azDeg = (g_timeOfDay - 12.0f) * 15.0f + g_northOffset;
      float azRad = azDeg * DEG2RAD;

      // Get current parameters to preserve other sliders
      float sunSize = IBLManager::Get().GetSunSize();
      float sunInt = IBLManager::Get().GetSunIntensity();

      // Apply to Sky Model
      IBLManager::Get().SetSolarAltitude(elRad);
      IBLManager::Get().SetSolarAzimuth(azRad);
      IBLManager::Get().UpdateSkyModel();

      // Sync Directional Light
      float sunX = std::cos(azRad) * std::cos(elRad);
      float sunZ = std::sin(azRad) * std::cos(elRad);
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

      UpdateCameraCB();
    }

    // Log to stderr only (controlled by verbose flag)
    if (g_verboseRenderLogs)
      fprintf(stderr, "PopulateCommandList start\n");

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
    viewport.Width = (float)g_windowWidth;
    viewport.Height = (float)g_windowHeight;
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    D3D12_RECT scissorRect = {0, 0, (LONG)g_windowWidth, (LONG)g_windowHeight};

    g_commandList->RSSetViewports(1, &viewport);
    g_commandList->RSSetScissorRects(1, &scissorRect);

    // Render based on current mode
    switch (g_currentRenderMode) {
    case RenderMode::DXR: {
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
              m.materialIndex = activeMeshes[i].materialIndex;
              m.vbIndex = (int)i;
              m.ibIndex = (int)i;
              memcpy(pData + i * sizeof(MeshData), &m, sizeof(MeshData));
            }
            g_meshStructuredBuffer->Unmap(0, nullptr);
          }
        }

        bool dxrOk = DxrRenderer::RenderFrame(
                g_commandList.Get(),
                g_frameResources[g_frameIndex].commandAllocator.Get(),
                g_frameIndex, g_renderTargets[g_frameIndex].Get(), rtvHandle,
                g_cameraConstantBuffer.Get(), g_materialStructuredBuffer.Get(),
                g_texturesGpuStart, g_textureDescriptorCount, activeMeshes,
                g_meshStructuredBuffer.Get());
        if (dxrOk) {
          // Success DXR render - Draw Grid with depth checks
          if (g_drawGrid) {
            D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle =
                g_dsvHeap->GetCPUDescriptorHandleForHeapStart();
            g_commandList->ClearDepthStencilView(
                dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

            // 1. Scene Depth Pre-pass (populate depth buffer for grid
            // occlusion)
            g_commandList->OMSetRenderTargets(0, nullptr, FALSE, &dsvHandle);
            g_commandList->SetGraphicsRootSignature(g_rootSignature.Get());
            RasterRenderer::DrawSceneDepthOnly(g_commandList.Get(),
                                               g_cameraConstantBuffer.Get(),
                                               sceneInstances);

            // 2. Draw Grid (test against the populated depth buffer)
            g_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
            RasterRenderer::DrawGrid(g_commandList.Get(),
                                     g_cameraConstantBuffer.Get());
          }
        } else {
          // If RenderFrame failed, fall back to red clear
          TR(g_commandList.Get(), g_renderTargets[g_frameIndex].Get(),
             D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

          FLOAT clearColor[] = {0.8f, 0.2f, 0.2f,
                                1.0f}; // Red to indicate fallback
          g_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0,
                                               nullptr);
          g_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
        }
      } else {
        // Fallback to raster if DXR not available
        TR(g_commandList.Get(), g_renderTargets[g_frameIndex].Get(),
           D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

        FLOAT clearColor[] = {0.8f, 0.2f, 0.2f,
                              1.0f}; // Red to indicate fallback
        g_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
        g_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
      }
      break;
    }

    case RenderMode::Raster: {
      // Fast Rasterization Path
      // Log to stderr only (controlled by verbose flag)
      if (g_verboseRenderLogs)
        fprintf(stderr, "Entering Raster Path\n");
      TR(g_commandList.Get(), g_renderTargets[g_frameIndex].Get(),
         D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

      FLOAT clearColor[] = {0.1f, 0.1f, 0.12f, 1.0f};
      g_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

      // Clear depth buffer
      D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle =
          g_dsvHeap->GetCPUDescriptorHandleForHeapStart();
      g_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH,
                                           1.0f, 0, 0, nullptr);

      g_commandList->SetGraphicsRootSignature(g_rootSignature.Get());
      // Use camera constant buffer for proper camera movement
      if (g_cameraConstantBuffer) {
        g_commandList->SetGraphicsRootConstantBufferView(
            0, g_cameraConstantBuffer->GetGPUVirtualAddress());
      }

      // No demo triangle; ensure render target is bound for subsequent draws
      g_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

      // Bind global descriptor heap once for all raster calls
      ID3D12DescriptorHeap *heaps[] = {g_cbvSrvAllocator.Heap()};
      g_commandList->SetDescriptorHeaps(_countof(heaps), heaps);

      // Draw Skybox (Always passes depth, but doesn't write depth)
      if (g_cloudManager.NeedsBake()) {
          fprintf(stderr, "Main: calling g_cloudManager.BakeSky() before DrawSkybox\n");
          g_cloudManager.BakeSky(g_commandList.Get(), g_cameraConstantBuffer.Get());
          fprintf(stderr, "Main: returned from g_cloudManager.BakeSky()\n");
      }
      RasterRenderer::DrawSkybox(g_commandList.Get(),
                                 g_cameraConstantBuffer.Get());

      // Draw ground grid (optional) via raster module
      if (g_drawGrid) {
        RasterRenderer::DrawGrid(g_commandList.Get(),
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
        g_commandList->SetPipelineState(
            RasterRenderer::g_meshPipelineState.Get());
        g_commandList->IASetPrimitiveTopology(
            D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        // Ensure render targets are set for mesh rendering
        g_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
        // Use camera constant buffer for mesh rendering
        if (g_cameraConstantBuffer) {
          g_commandList->SetGraphicsRootConstantBufferView(
              0, g_cameraConstantBuffer->GetGPUVirtualAddress());
        }

        // Draw all instances
        for (size_t i = 0; i < sceneInstances.size(); ++i) {
          const auto &inst = sceneInstances[i];
          const auto &gm = inst.mesh;
          // Skip meshes that have been deleted or not properly initialized
          if (!gm.vertexBuffer || !gm.indexBuffer || gm.ibView.SizeInBytes == 0)
            continue;

          // Set instance transform
          g_commandList->SetGraphicsRoot32BitConstants(3, 16, inst.transform,
                                                       0);

          // material binding... (existing logic needed)
          if (gm.materialIndex >= 0 &&
              (size_t)gm.materialIndex < g_loadedMaterials.size()) {
            // ...
          }

          if (g_verboseRenderLogs) {
            fprintf(stderr,
                    "Mesh[%zu]: vb=0x%016llx vbSize=%u vbStride=%u "
                    "ib=0x%016llx ibSize=%u verts=%u idx=%u mat=%d\n",
                    i, (unsigned long long)gm.vbView.BufferLocation,
                    (unsigned)gm.vbView.SizeInBytes,
                    (unsigned)gm.vbView.StrideInBytes,
                    (unsigned long long)gm.ibView.BufferLocation,
                    (unsigned)gm.ibView.SizeInBytes, (unsigned)gm.vertexCount,
                    (unsigned)gm.indexCount, gm.materialIndex);
          }

          g_commandList->IASetVertexBuffers(0, 1, &gm.vbView);
          g_commandList->IASetIndexBuffer(&gm.ibView);

          if (gm.materialIndex >= 0 &&
              gm.materialIndex < (int)g_loadedMaterials.size()) {
            // Upload material data for this mesh into a unique slot in the
            // material CB We'll use a simple linear allocation (i % 1024) for
            // now. In a production engine, this would use a dynamic ring
            // buffer.
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

            if (g_materialConstantBuffer) {
              const UINT64 matSlotSize = (sizeof(MaterialCB) + 255) & ~255;
              UINT64 offset = (i % 1024) * matSlotSize;
              UINT8 *pMat = nullptr;
              D3D12_RANGE readRange = {0, 0};
              // Note: For high frequency updates, persistent mapping or
              // multiple buffers are preferred.
              if (SUCCEEDED(g_materialConstantBuffer->Map(
                      0, &readRange, reinterpret_cast<void **>(&pMat)))) {
                memcpy(pMat + offset, &matCB, sizeof(matCB));
                g_materialConstantBuffer->Unmap(0, nullptr);
              }
              g_commandList->SetGraphicsRootConstantBufferView(
                  2, g_materialConstantBuffer->GetGPUVirtualAddress() + offset);
            }
            if (g_textureDescriptorCount > 0) {
              g_commandList->SetGraphicsRootDescriptorTable(1,
                                                            g_texturesGpuStart);
            }
            if (IBLManager::Get().IsLoaded()) {
              g_commandList->SetGraphicsRootDescriptorTable(
                  4, IBLManager::Get().GetGPUHandle());
            }
          }
          if (gm.ibView.SizeInBytes > 0) {
            g_commandList->DrawIndexedInstanced(gm.ibView.SizeInBytes / 4, 1, 0,
                                                0, 0);
            if (g_verboseRenderLogs) {
              fprintf(stderr, "Issued DrawIndexedInstanced for mesh[%zu]\n", i);
            }
          }
        }
      }
      break;
    }
    }

    // Render ImGui (Overlay on top of whatever was drawn)
    ID3D12DescriptorHeap *ppHeaps[] = {g_cbvSrvAllocator.Heap()};
    g_commandList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_commandList.Get());

    // Transition back to present
    TR(g_commandList.Get(), g_renderTargets[g_frameIndex].Get(),
       D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

    ThrowIfFailed(g_commandList->Close());
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

    // Input handling guarded by application focus
    bool appFocused = (GetForegroundWindow() == g_hwnd);

    // Handle mouse rotation when RMB is pressed (only when the app is focused)
    // Use FPS-style capture: confine cursor and recenter each frame for smooth
    // relative motion
    RECT clientRect;
    GetClientRect(g_hwnd, &clientRect);
    POINT centerScreen = {clientRect.right / 2, clientRect.bottom / 2};
    ClientToScreen(g_hwnd, &centerScreen);

    if (appFocused && (GetAsyncKeyState(VK_RBUTTON) & 0x8000)) {
      if (!g_mouseCaptured) {
        // Enter capture mode
        SetCursorPos(centerScreen.x, centerScreen.y);
        ShowCursor(FALSE);
        RECT winRect;
        GetWindowRect(g_hwnd, &winRect);
        ClipCursor(&winRect);
        g_mouseCaptured = true;
      }

      POINT curPos;
      GetCursorPos(&curPos);
      int dx = curPos.x - centerScreen.x;
      int dy = curPos.y - centerScreen.y;

      const float sensitivity = g_mouseSensitivity; // radians per pixel
      // Update yaw/pitch directly (FPS-style mouse look)
      g_camYaw += dx * sensitivity;
      g_camPitch -= dy * sensitivity;

      // Clamp pitch to avoid flipping
      const float maxPitch = 3.14159265f * 0.5f - 0.01f;
      if (g_camPitch > maxPitch)
        g_camPitch = maxPitch;
      if (g_camPitch < -maxPitch)
        g_camPitch = -maxPitch;

      // Compute forward from yaw/pitch
      g_cameraData.forward[0] = cosf(g_camPitch) * sinf(g_camYaw);
      g_cameraData.forward[1] = sinf(g_camPitch);
      g_cameraData.forward[2] = cosf(g_camPitch) * -cosf(g_camYaw);

      // Reset accumulation immediately when the camera orientation changes via
      // mouse
      DxrRenderer::ResetAccumulation();

      // Recenter cursor for next delta
      SetCursorPos(centerScreen.x, centerScreen.y);
    } else {
      if (g_mouseCaptured) {
        ShowCursor(TRUE);
        ClipCursor(NULL);
        g_mouseCaptured = false;
      }
      if (appFocused) {
        GetCursorPos(&g_prevMousePos);
        ScreenToClient(g_hwnd, &g_prevMousePos);
      }
    }
    // Movement: WASD (only when app is focused)
    float moveSpeed = g_camSpeed;
    if (appFocused) {
      if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
        moveSpeed *= 3.0f;
    }

    // Build forward vector for rendering (uses yaw/pitch) and also compute a
    // horizontal-only forward for FPS movement
    Vec3 camF = {g_cameraData.forward[0], g_cameraData.forward[1],
                 g_cameraData.forward[2]};
    Vec3 camU = {g_cameraData.up[0], g_cameraData.up[1], g_cameraData.up[2]};
    // rotate forward by yaw/pitch (used for view/rendering)
    {
      float cp = cosf(g_camPitch);
      float sp = sinf(g_camPitch);
      float cy = cosf(g_camYaw);
      float sy = sinf(g_camYaw);
      camF.x = cp * sy;
      camF.y = sp;
      camF.z = cp * -cy;
    }

    // Horizontal FPS movement basis (yaw-only forward)
    Vec3 worldUp = {0.0f, 1.0f, 0.0f};
    Vec3 moveF = {sinf(g_camYaw), 0.0f, -cosf(g_camYaw)};
    // Right vector = cross(moveF, worldUp)
    Vec3 moveR = {moveF.y * worldUp.z - moveF.z * worldUp.y,
                  moveF.z * worldUp.x - moveF.x * worldUp.z,
                  moveF.x * worldUp.y - moveF.y * worldUp.x};

    // normalize helper
    auto normalize3 = [](Vec3 &v) {
      float l = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
      if (l > 0.00001f) {
        v.x /= l;
        v.y /= l;
        v.z /= l;
      }
    };
    normalize3(camF);
    normalize3(moveR);
    normalize3(moveF);

    Vec3 move = {0, 0, 0};
    if (appFocused) {
      // W/S forward/back (horizontal)
      if (GetAsyncKeyState('W') & 0x8000) {
        move.x += moveF.x;
        move.y += moveF.y;
        move.z += moveF.z;
      }
      if (GetAsyncKeyState('S') & 0x8000) {
        move.x -= moveF.x;
        move.y -= moveF.y;
        move.z -= moveF.z;
      }
      // A/D strafing (standard FPS: A=left, D=right)
      if (GetAsyncKeyState('A') & 0x8000) {
        move.x -= moveR.x;
        move.y -= moveR.y;
        move.z -= moveR.z;
      }
      if (GetAsyncKeyState('D') & 0x8000) {
        move.x += moveR.x;
        move.y += moveR.y;
        move.z += moveR.z;
      }
      // Vertical movement: Q up, E down (world up)
      if (GetAsyncKeyState('Q') & 0x8000) {
        move.x += worldUp.x;
        move.y += worldUp.y;
        move.z += worldUp.z;
      }
      if (GetAsyncKeyState('E') & 0x8000) {
        move.x -= worldUp.x;
        move.y -= worldUp.y;
        move.z -= worldUp.z;
      }

      // TAB: Toggle between Raster and Raytracing modes
      static bool tabDown = false;
      if (GetAsyncKeyState(VK_TAB) & 0x8000) {
        if (!tabDown) {
          if (g_currentRenderMode == RenderMode::Raster) {
            g_currentRenderMode = RenderMode::DXR;
            WaitGPUIdle();
            DxrRenderer::CreateRayTracingPipeline(g_windowWidth,
                                                  g_windowHeight);
            fprintf(stderr, "Switched to DXR Mode (TAB)\n");
          } else {
            g_currentRenderMode = RenderMode::Raster;
            fprintf(stderr, "Switched to Raster Mode (TAB)\n");
          }
          tabDown = true;
        }
      } else {
        tabDown = false;
      }

      // Selection: LBUTTON
      static bool lbtnDown = false;
      if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
        if (!lbtnDown) {
          // Always run selection logic to allow selecting nodes
          int pickedMaterial = Scene::UpdateSelection((float)g_windowWidth,
                                                      (float)g_windowHeight);

          if (pickedMaterial != -1) {
            // Only switch the Material Editor's active material if the Picking
            // Tool is explicitly enabled
            if (MaterialEditor::IsPickingEnabled()) {
              MaterialEditor::SelectMaterial(pickedMaterial);
              MaterialEditor::SetPickingEnabled(false);
            }
          }
          lbtnDown = true;
        }
      } else {
        lbtnDown = false;
      }
    }

    if (move.x != 0 || move.y != 0 || move.z != 0) {
      normalize3(move);
      g_cameraData.pos[0] += move.x * moveSpeed * dt;
      g_cameraData.pos[1] += move.y * moveSpeed * dt;
      g_cameraData.pos[2] += move.z * moveSpeed * dt;

      // Reset accumulation immediately when the camera position changes via
      // input
      DxrRenderer::ResetAccumulation();
    }

    // Update camera forward from yaw/pitch
    g_cameraData.forward[0] = (cosf(g_camPitch) * sinf(g_camYaw));
    g_cameraData.forward[1] = sinf(g_camPitch);
    g_cameraData.forward[2] = (cosf(g_camPitch) * -cosf(g_camYaw));

    // Ensure aspect matches the window and update camera CB on GPU
    g_cameraData.aspect = (float)g_windowWidth / (float)g_windowHeight;
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
    g_cloudManager.Update(dt, g_frameIndex);

    // Start ImGui frame
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();

    // Main menu bar: Window menu + quick panel toggles on the bar for fast
    // access
    if (ImGui::BeginMainMenuBar()) {
      if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Save Scene...")) {
          std::wstring chosen;
          if (SaveSceneFileDialog(g_hwnd, chosen)) {
            std::string utf8 = WStringToUtf8(chosen);
            if (SceneIO::SaveScene(utf8)) {
              fprintf(stderr, "Scene saved to %s\n", utf8.c_str());
            } else {
              fprintf(stderr, "Failed to save scene to %s\n", utf8.c_str());
            }
          }
        }
        if (ImGui::MenuItem("Load Scene...")) {
          std::wstring chosen;
          if (OpenSceneFileDialog(g_hwnd, chosen)) {
            std::string utf8 = WStringToUtf8(chosen);
            if (SceneIO::LoadScene(utf8)) {
              fprintf(stderr, "Scene loaded from %s\n", utf8.c_str());
            } else {
              fprintf(stderr, "Failed to load scene from %s\n", utf8.c_str());
            }
          }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit", "Alt+F4")) {
          g_appClosing = true;
        }
        ImGui::EndMenu();
      }

      // Keep Window menu for non-toggle commands

      // Quick access toggles (side-by-side) for panels
      ImGui::SameLine();
      ImGui::Text("Panels:");
      ImGui::SameLine();
      // Use compact spacing for menu bar toggles
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 6));
      if (ImGui::BeginMenu("Clouds")) {
        CloudParams &cp = g_cloudManager.GetParams();
        bool changed = false;

        if (ImGui::Checkbox("Enable Cloud Rendering",
                            &g_cloudRenderingEnabled)) {
          changed = true;
        }
        ImGui::Separator();

        if (ImGui::Button("Reset to Defaults")) {
          g_cloudManager.ResetToDefaults();
          changed = true;
        }
        ImGui::Separator();

        changed |= ImGui::SliderFloat("Density", &cp.density, 0.0f, 5.0f);
        changed |= ImGui::SliderFloat("Absorption", &cp.absorption, 0.0f, 2.0f);
        changed |= ImGui::SliderFloat("Coverage", &cp.coverage, 0.0f, 1.0f);
        changed |=
            ImGui::SliderFloat("Scattering (g)", &cp.scattering, -0.99f, 0.99f);
        changed |= ImGui::SliderInt("Steps", &cp.steps, 16, 128);
        changed |=
            ImGui::SliderFloat("Sun Intensity", &cp.sunIntensity, 0.0f, 20.0f);
        changed |=
            ImGui::SliderFloat("Cloud Top", &cp.cloudTop, 200.0f, 1000.0f);
        changed |=
            ImGui::SliderFloat("Cloud Bottom", &cp.cloudBottom, 50.0f, 300.0f);
        changed |= ImGui::SliderFloat("Wind Speed", &cp.windSpeed, 0.0f, 50.0f);

        ImGui::Separator();
        changed |=
            ImGui::SliderFloat("Base Scale", &cp.baseScale, 0.0001f, 0.0020f,
                               "%.5f", ImGuiSliderFlags_Logarithmic);
        changed |=
            ImGui::SliderFloat("Detail Scale", &cp.detailScale, 0.0005f, 0.01f,
                               "%.5f", ImGuiSliderFlags_Logarithmic);
        changed |=
            ImGui::SliderFloat("Coverage Scale", &cp.coverageScale, 0.00005f,
                               0.0010f, "%.5f", ImGuiSliderFlags_Logarithmic);
        changed |= ImGui::SliderFloat("Erosion", &cp.erosion, 0.0f, 1.0f);
        changed |=
            ImGui::SliderFloat("Warp Strength", &cp.warpStrength, 0.0f, 2.0f);

        ImGui::Separator();
        changed |= ImGui::SliderInt("Shadow Steps", &cp.shadowSteps, 1, 16);
        changed |= ImGui::SliderFloat("Shadow Step Size", &cp.shadowStepSize,
                                      10.0f, 500.0f);
        changed |= ImGui::SliderFloat("Shadow LOD", &cp.shadowLod, 0.0f, 5.0f);
        changed |= ImGui::SliderInt("Max Ray Steps", &cp.maxSteps, 64, 2048);
        changed |= ImGui::SliderFloat("Vertical Step (m)",
                                      &cp.verticalStepMeters, 2.0f, 80.0f);
        changed |=
            ImGui::SliderInt("Shadow Every N Steps", &cp.shadowEvery, 1, 16);
        changed |= ImGui::SliderFloat("Shadow Density Threshold",
                                      &cp.shadowDensityThreshold, 0.0f, 0.5f);

        if (changed) {
          DxrRenderer::ResetAccumulation();
        }
        ImGui::EndMenu();
      }

      ImGui::Checkbox("##AssetsToggle", &g_showAssetsWindow);
      ImGui::SameLine();
      ImGui::Text("Assets");
      ImGui::SameLine();
      ImGui::Checkbox("##ControlsToggle", &g_showControlsWindow);
      ImGui::SameLine();
      ImGui::Text("Controls");
      ImGui::SameLine();
      ImGui::Checkbox("##RenderModeToggle", &g_showRenderModeWindow);
      ImGui::SameLine();
      ImGui::Text("Render Mode");
      ImGui::SameLine();
      ImGui::Checkbox("##MaterialEditorToggle", &g_showMaterialEditor);
      ImGui::SameLine();
      ImGui::Text("Material Editor");
      ImGui::PopStyleVar();

      ImGui::EndMainMenuBar();
    }

    // UI: Camera controls and debug info
    if (g_showControlsWindow) {
      if (ImGui::Begin("Controls", &g_showControlsWindow,
                       ImGuiWindowFlags_NoCollapse)) {
        bool uiChanged = false;

        // Camera Debug Info
        ImGui::Text("Camera Position: (%.2f, %.2f, %.2f)", g_cameraData.pos[0],
                    g_cameraData.pos[1], g_cameraData.pos[2]);
        ImGui::Text("Camera Forward: (%.2f, %.2f, %.2f)",
                    g_cameraData.forward[0], g_cameraData.forward[1],
                    g_cameraData.forward[2]);
        ImGui::Text("Camera Up: (%.2f, %.2f, %.2f)", g_cameraData.up[0],
                    g_cameraData.up[1], g_cameraData.up[2]);
        {
          float vFov = g_cameraData.fov;
          float aspect = g_cameraData.aspect;
          float vHalfRad = vFov * 0.5f * (3.14159265f / 180.0f);
          float hFov =
              2.0f * atanf(tanf(vHalfRad) * aspect) * (180.0f / 3.14159265f);
          ImGui::Text("FOV V/H: %.1f° / %.1f°, Aspect: %.2f", vFov, hFov,
                      aspect);
        }
        ImGui::Text("Near: %.2f, Far: %.2f", g_cameraData.nearZ,
                    g_cameraData.farZ);
        ImGui::Text("Intensity: %.2f", g_cameraData.intensity);

        ImGui::Separator();

        // Controls
        // Horizontal-FOV slider (UI shows H, shaders use V). Convert H -> V
        // before storing.
        {
          float aspect = g_cameraData.aspect;
          // compute current horizontal FOV from stored vertical FOV
          float curV = g_cameraData.fov;
          float curVHalf = curV * 0.5f * (3.14159265f / 180.0f);
          float curH =
              2.0f * atanf(tanf(curVHalf) * aspect) * (180.0f / 3.14159265f);
          float hFovSlider = curH;
          if (ImGui::SliderFloat("Horizontal FOV", &hFovSlider, 20.0f,
                                 160.0f)) {
            // convert slider H (degrees) back to vertical FOV in degrees
            float hHalfRad = hFovSlider * 0.5f * (3.14159265f / 180.0f);
            float vHalfRadNew = atanf(tanf(hHalfRad) / aspect);
            float vFovNew = 2.0f * vHalfRadNew * (180.0f / 3.14159265f);
            g_cameraData.fov = vFovNew;
            UpdateCameraCB();
            uiChanged = true;
          }
        }
        if (ImGui::SliderFloat("Intensity", &g_cameraData.intensity, 0.0f,
                               5.0f)) {
          UpdateCameraCB();
          uiChanged = true;
          // Debug: print camera params when intensity changes
          fprintf(stderr,
                  "Camera params after Intensity change: fov=%.3f "
                  "aspect=%.3f near=%.3f far=%.3f intensity=%.3f\n",
                  g_cameraData.fov, g_cameraData.aspect, g_cameraData.nearZ,
                  g_cameraData.farZ, g_cameraData.intensity);
        }
        if (ImGui::Button("Reset Camera")) {
          ResetCamera();
          UpdateCameraCB();
          uiChanged = true;
        }

        // Camera movement & mouse sensitivity controls
        ImGui::Spacing();
        if (ImGui::SliderFloat("Move Speed", &g_camSpeed, 0.1f, 20.0f)) {
          // no additional action required; movement uses g_camSpeed immediately
          uiChanged = true;
        }
        if (ImGui::SliderFloat("Mouse Sensitivity", &g_mouseSensitivity, 0.001f,
                               0.05f)) {
          // sensitivity applied next frame via g_mouseSensitivity
          uiChanged = true;
        }

        ImGui::Separator();

        // Manual Sun Control removed as Prague Model is default

        ImGui::Text("Environment / Sky Model");

        bool fileIblEnabled =
            (IBLManager::Get().GetIBLSource() == IBLManager::IBLSource::File);
        if (ImGui::Checkbox("Enable File IBL", &fileIblEnabled)) {
          if (fileIblEnabled) {
            IBLManager::Get().SetIBLSource(IBLManager::IBLSource::File);
          } else {
            IBLManager::Get().SetIBLSource(
                IBLManager::IBLSource::PragueSkyModel);
            IBLManager::Get().UpdateSkyModel();
          }
          uiChanged = true;
        }

        static int iblSource = 0;
        iblSource = (int)IBLManager::Get().GetIBLSource();
        if (ImGui::RadioButton("File IBL", &iblSource, 0)) {
          IBLManager::Get().SetIBLSource(IBLManager::IBLSource::File);
          uiChanged = true;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Prague Sky", &iblSource, 1)) {
          IBLManager::Get().SetIBLSource(IBLManager::IBLSource::PragueSkyModel);
          IBLManager::Get().UpdateSkyModel();
          uiChanged = true;
        }

        if (iblSource == 1) {
          bool uiParamChanged = false;

          float vis = IBLManager::Get().GetSkyVisibility();
          if (ImGui::SliderFloat("Visibility (km)", &vis, 10.0f, 120.0f)) {
            IBLManager::Get().SetSkyVisibility(vis);
            uiParamChanged = true;
            uiChanged = true;
          }
          float albedo = IBLManager::Get().GetSkyAlbedo();
          if (ImGui::SliderFloat("Earth Albedo", &albedo, 0.0f, 1.0f)) {
            IBLManager::Get().SetSkyAlbedo(albedo);
            uiParamChanged = true;
            uiChanged = true;
          }
          float altitude = IBLManager::Get().GetObserverAltitude();
          if (ImGui::SliderFloat("Altitude (m)", &altitude, 0.0f, 15000.0f)) {
            IBLManager::Get().SetObserverAltitude(altitude);
            uiParamChanged = true;
            uiChanged = true;
          }

          // Intensity Controls
          float skyInt = IBLManager::Get().GetSkyIntensity();
          if (ImGui::SliderFloat("Sky Intensity", &skyInt, 0.0f, 5.0f)) {
            IBLManager::Get().SetSkyIntensity(skyInt);
            uiParamChanged = true;
            uiChanged = true;
          }
          float sunInt = IBLManager::Get().GetSunIntensity();
          if (ImGui::SliderFloat("Sun Intensity", &sunInt, 0.0f, 5.0f)) {
            IBLManager::Get().SetSunIntensity(sunInt);
            // Changes analytic light intensity
            uiChanged = true;
          }
          float sunSize = IBLManager::Get().GetSunSize();
          if (ImGui::SliderFloat("Sun Size (deg)", &sunSize, 0.1f, 5.0f)) {
            IBLManager::Get().SetSunSize(sunSize);
            // Changes analytic light radius
            uiChanged = true;
          }

          // float elev = IBLManager::Get().GetSolarAltitude(); // Not used
          // directly, driven by Time

          // GUI State for Time/North (controlled by global static vars now)

          if (ImGui::SliderFloat("Time of Day", &g_timeOfDay, 6.0f, 18.0f)) {
            uiParamChanged = true;
            uiChanged = true;
          }
          if (ImGui::SliderFloat("North Offset", &g_northOffset, 0.0f,
                                 360.0f)) {
            uiParamChanged = true;
            uiChanged = true;
          }

          // Logic moved to PopulateCommandList to ensure update even when UI is
          // closed

          // If UI changed non-light parameters (texture content), force logical
          // reset
          if (uiParamChanged) {
            DxrRenderer::ResetAccumulation();
          }

          UpdateCameraCB(); // automatically resets accumulation if
                            // lightDir/Color changed
        }

        ImGui::Spacing();
        if (ImGui::ColorEdit3("Ambient Color", g_cameraData.ambientColor)) {
          UpdateCameraCB();
          uiChanged = true;
        }
        if (ImGui::SliderFloat("Ambient Weight", &g_cameraData.ambientColor[3],
                               0.0f, 1.0f)) {
          UpdateCameraCB();
          uiChanged = true;
        }
        ImGui::Checkbox("Show Grid", &g_drawGrid);

        ImGui::Separator();

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
          uiChanged = true;
        }
        if (ImGui::Checkbox("Verbose Render Logs", &g_verboseRenderLogs)) {
          fprintf(stderr, "Verbose Render Logs set=%d\n", g_verboseRenderLogs);
          uiChanged = true;
        }
      }
      ImGui::End();
    }

    // Render Mode Selector
    ImGui::SetNextWindowSize(ImVec2(300, 150), ImGuiCond_FirstUseEver);
    if (g_showRenderModeWindow) {
      if (ImGui::Begin("Render Mode", &g_showRenderModeWindow,
                       ImGuiWindowFlags_NoCollapse)) {
        bool uiChanged = false;
        ImGui::Text("Current Mode: %s",
                    g_currentRenderMode == RenderMode::Raster ? "Raster"
                                                              : "DXR");

        if (g_currentRenderMode == RenderMode::DXR) {
          ImGui::Text("Samples: %u", DxrRenderer::GetDisplayedSampleCount());

          float currentNoise = DxrRenderer::GetCurrentNoiseLevel();
          if (currentNoise > 0.0f) {
            ImGui::Text("Noise Level: %.2f%%", currentNoise * 100.0f);
          } else {
            ImGui::Text("Noise Level: Calculating...");
          }

          if (ImGui::SliderFloat("Reflection Bounces",
                                 &g_cameraData.maxSpecularBounces, 0.0f, 16.0f,
                                 "%.0f")) {
            UpdateCameraCB();
            uiChanged = true;
          }
          if (ImGui::SliderFloat("Refraction Bounces",
                                 &g_cameraData.maxRefractiveBounces, 0.0f,
                                 16.0f, "%.0f")) {
            UpdateCameraCB();
            uiChanged = true;
          }
          if (ImGui::SliderFloat("GI Bounces", &g_cameraData.maxGIBounces, 0.0f,
                                 16.0f, "%.0f")) {
            UpdateCameraCB();
            uiChanged = true;
          }

          int maxSpp = (int)g_cameraData.maxSPP;
          if (maxSpp < 10)
            maxSpp = 10;
          if (maxSpp > 1000)
            maxSpp = 1000;
          if (ImGui::SliderInt("Max SPP", &maxSpp, 10, 1000)) {
            g_cameraData.maxSPP = (float)maxSpp;
            UpdateCameraCB();
            uiChanged = true;
          }

          ImGui::Separator();
          ImGui::Text("Adaptive Sampling");
          bool adaptive = g_cameraData.useAdaptiveSampling > 0.5f;
          if (ImGui::Checkbox("Enable Adaptive Sampling", &adaptive)) {
            g_cameraData.useAdaptiveSampling = adaptive ? 1.0f : 0.0f;
            // Ensure threshold is valid when enabling
            if (g_cameraData.noiseThreshold <= 0.0f) {
              g_cameraData.noiseThreshold = 0.05f; // Default 5%
            }
            UpdateCameraCB();
            uiChanged = true;
          }
          if (adaptive) {
            // If adaptive is on, we might want to increase maxSPP effectively
            // to infinity or let user control it. User said "Max SPP or Noise,
            // whichever first". So we keep Max SPP control.

            float nVal = g_cameraData.noiseThreshold * 100.0f;
            if (ImGui::SliderFloat("Target Noise %", &nVal, 1.0f, 30.0f,
                                   "%.1f%%")) {
              g_cameraData.noiseThreshold = nVal / 100.0f;
              UpdateCameraCB();
              uiChanged = true;
            }

            #ifdef _DEBUG
            bool viz = g_cameraData.debugVisualizationMode > 0.5f;
            if (ImGui::Checkbox("Show Noise Map (Debug)", &viz)) {
              g_cameraData.debugVisualizationMode = viz ? 1.0f : 0.0f;
              UpdateCameraCB();
              uiChanged = true;
            }

            if (ImGui::IsItemHovered())
              ImGui::SetTooltip("White = High Noise (10%+), Black = Low Noise");
            #else
            g_cameraData.debugVisualizationMode = 0.0f;
            #endif
          }

          ImGui::Separator();
          ImGui::Text("Streamline / DLSS");
          bool dlssEnabled = g_streamline.IsEnabled();
          if (ImGui::Checkbox("Enable", &dlssEnabled)) {
            g_streamline.SetEnabled(dlssEnabled);
            DxrRenderer::ResetStreamlineHistory();
            // DLSS uses a different internal render resolution; recreate
            // resources.
            WaitGPUIdle();
            DxrRenderer::CreateRayTracingPipeline(g_windowWidth,
                                                  g_windowHeight);
            uiChanged = true;
          }

          const char *dlssModes[] = {"Off", "DLSS Super Resolution",
                                     "DLSS Ray Reconstruction"};
          int modeIdx = 0;
          switch (g_streamline.GetMode()) {
          case StreamlineManager::Mode::Off:
            modeIdx = 0;
            break;
          case StreamlineManager::Mode::DLSS_SuperResolution:
            modeIdx = 1;
            break;
          case StreamlineManager::Mode::DLSS_RayReconstruction:
            modeIdx = 2;
            break;
          }
          if (ImGui::Combo("Mode", &modeIdx, dlssModes,
                           IM_ARRAYSIZE(dlssModes))) {
            StreamlineManager::Mode newMode = StreamlineManager::Mode::Off;
            if (modeIdx == 1)
              newMode = StreamlineManager::Mode::DLSS_SuperResolution;
            if (modeIdx == 2)
              newMode = StreamlineManager::Mode::DLSS_RayReconstruction;
            g_streamline.SetMode(newMode);
            if (newMode == StreamlineManager::Mode::DLSS_RayReconstruction) {
              // RR shimmer is often worst at silhouettes/screen edges.
              // Default to a more stable jitter amplitude when entering RR.
              DxrRenderer::SetRrJitterScale(0.5f);
            }
            DxrRenderer::ResetStreamlineHistory();
            WaitGPUIdle();
            DxrRenderer::CreateRayTracingPipeline(g_windowWidth,
                                                  g_windowHeight);
            uiChanged = true;
          }

          const char *qualities[] = {"Max Performance", "Balanced",
                                     "Max Quality", "Ultra Performance",
                                     "DLAA"};
          int qIdx = 1;
          switch (g_streamline.GetQuality()) {
          case StreamlineManager::Quality::MaxPerformance:
            qIdx = 0;
            break;
          case StreamlineManager::Quality::Balanced:
            qIdx = 1;
            break;
          case StreamlineManager::Quality::MaxQuality:
            qIdx = 2;
            break;
          case StreamlineManager::Quality::UltraPerformance:
            qIdx = 3;
            break;
          case StreamlineManager::Quality::DLAA:
            qIdx = 4;
            break;
          }
          if (ImGui::Combo("Quality", &qIdx, qualities,
                           IM_ARRAYSIZE(qualities))) {
            StreamlineManager::Quality newQ =
                StreamlineManager::Quality::Balanced;
            if (qIdx == 0)
              newQ = StreamlineManager::Quality::MaxPerformance;
            if (qIdx == 1)
              newQ = StreamlineManager::Quality::Balanced;
            if (qIdx == 2)
              newQ = StreamlineManager::Quality::MaxQuality;
            if (qIdx == 3)
              newQ = StreamlineManager::Quality::UltraPerformance;
            if (qIdx == 4)
              newQ = StreamlineManager::Quality::DLAA;
            g_streamline.SetQuality(newQ);
            DxrRenderer::ResetStreamlineHistory();
            WaitGPUIdle();
            DxrRenderer::CreateRayTracingPipeline(g_windowWidth,
                                                  g_windowHeight);
          }

          if (ImGui::Button("Reset DLSS History")) {
            DxrRenderer::ResetStreamlineHistory();
          }
          ImGui::SameLine();
          ImGui::Text("SL: %s / %s",
                      g_streamline.IsInitialized() ? "Init" : "Off",
                      g_streamline.IsDeviceSet() ? "Device" : "NoDevice");

          // Show recommended render (input) size and output (swapchain) size
          {
            auto rec = g_streamline.GetRecommendedRenderSize(g_windowWidth,
                                                             g_windowHeight);
            ImGui::Text("Render (in): %u x %u    Output (out): %u x %u",
                        (unsigned)rec.renderWidth, (unsigned)rec.renderHeight,
                        (unsigned)g_windowWidth, (unsigned)g_windowHeight);
          }

          if (g_streamline.GetMode() ==
              StreamlineManager::Mode::DLSS_RayReconstruction) {
            float rrJitterScale = DxrRenderer::GetRrJitterScale();
            if (ImGui::SliderFloat("RR Jitter Scale", &rrJitterScale, 0.0f,
                                   1.0f, "%.2f")) {
              DxrRenderer::SetRrJitterScale(rrJitterScale);
              DxrRenderer::ResetStreamlineHistory();
              uiChanged = true;
            }
            ImGui::TextWrapped("Lowering jitter can reduce edge/silhouette "
                               "shimmer (especially near screen borders) but "
                               "may reduce DLSS-RR reconstruction/AA quality.");
          }

          // Denoiser selection
          const char *denoisers[] = {"Off", "OIDN (CPU)", "OIDN (GPU)"};
          int denoiserIdx = 0;
          switch (DxrRenderer::GetDenoiserMode()) {
          case DxrRenderer::DenoiserMode::Off:
            denoiserIdx = 0;
            break;
          case DxrRenderer::DenoiserMode::OIDN_CPU:
            denoiserIdx = 1;
            break;
          case DxrRenderer::DenoiserMode::OIDN_GPU:
            denoiserIdx = 2;
            break;
          }
          if (ImGui::Combo("Denoiser", &denoiserIdx, denoisers,
                           IM_ARRAYSIZE(denoisers))) {
            DxrRenderer::DenoiserMode newMode = DxrRenderer::DenoiserMode::Off;
            if (denoiserIdx == 1)
              newMode = DxrRenderer::DenoiserMode::OIDN_CPU;
            if (denoiserIdx == 2)
              newMode = DxrRenderer::DenoiserMode::OIDN_GPU;
            DxrRenderer::SetDenoiserMode(newMode);
            // Recreate pipeline/resources to account for any mode-specific
            // resources and reset accumulation for stable rendering.
            DxrRenderer::ResetAccumulation();
            WaitGPUIdle();
            DxrRenderer::CreateRayTracingPipeline(g_windowWidth,
                                                  g_windowHeight);
          }

          if (DxrRenderer::GetDenoiserMode() !=
              DxrRenderer::DenoiserMode::Off) {
            const char *oidnQualities[] = {"Fast", "Balanced", "High"};
            int qualIdx = (int)DxrRenderer::GetOidnQuality();
            if (ImGui::Combo("OIDN Quality", &qualIdx, oidnQualities,
                             IM_ARRAYSIZE(oidnQualities))) {
              DxrRenderer::SetOidnQuality((OidnDenoiser::Quality)qualIdx);
              uiChanged = true;
            }
          }

          // Denoising is automatically triggered once when Max SPP is reached
          // (only if a denoiser is selected).

          ImGui::Text("NGX AppId: %u", g_streamline.GetApplicationId());
          if (g_streamline.IsEnabled() &&
              g_streamline.GetMode() != StreamlineManager::Mode::Off &&
              (!g_streamline.IsInitialized() || !g_streamline.IsDeviceSet() ||
               !g_streamline.AreFeatureFunctionsReady())) {
            ImGui::TextWrapped("DLSS plugins may be disabled. If you see "
                               "'Missing NGX context', "
                               "set env SL_APPLICATION_ID (or create "
                               "sl_appid.txt next to the exe) "
                               "to your NVIDIA-provided NGX application id.");
          }

          ImGui::Separator();
          ImGui::Text("Streamline logging (restart required)");
          bool slLogToFile = g_streamline.GetLogToFile();
          if (ImGui::Checkbox("Write sl.log to file", &slLogToFile)) {
            g_streamline.SetLogToFile(slLogToFile);
          }
          bool slMirror = g_streamline.GetMirrorLogsToStderr();
          if (ImGui::Checkbox("Mirror SL logs to console", &slMirror)) {
            g_streamline.SetMirrorLogsToStderr(slMirror);
          }
          if (g_streamline.GetLogToFile()) {
            ImGui::TextWrapped("SL log dir: %ls",
                               g_streamline.GetLogDirectory().c_str());
          }
        }

        // Debug Render Pass Dropdown
        const char *debugModes[] = {"None",
                                    "Albedo",
                                    "Normal",
                                    "Emissive",
                                    "Roughness/Glossiness",
                                    "Refl. Color",
                                    "Metalness",
                                    "AO",
                                    "Motion Vectors",
                                    "Spec Hit Distance",
                                    "Spec Motion Vectors",
                                    "Cloud: Slab Mask",
                                    "Cloud: CB Sanity",
                                    "Cloud: Noise Sanity",
                                    "Cloud: Density Sanity",
                                    "Cloud: Opacity (1-T)",
                                    "Cloud: BaseShape Sanity",
                                    "Debug: Accum Samples (N)",
                                    "Debug: History Validity",
                                    "Debug: Per-Pixel Noise",
                                    "Debug: Sample Deficit",
                                    "Debug: Recent Reset Mask"};
      #ifdef _DEBUG
        if (ImGui::Combo("Debug View", &g_debugMode, debugModes,
             IM_ARRAYSIZE(debugModes))) {
          // Keep history when switching diagnostics so comparisons are from the
          // same accumulated frame state.
        }
      #else
        g_debugMode = 0;
      #endif

        // Reset accumulation once per window when any UI widget changed
        if (uiChanged) {
          DxrRenderer::ResetAccumulation();
        }

        if (ImGui::RadioButton("Fast Raster",
                               g_currentRenderMode == RenderMode::Raster)) {
          g_currentRenderMode = RenderMode::Raster;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("DXR", g_currentRenderMode == RenderMode::DXR)) {
          g_currentRenderMode = RenderMode::DXR;
          // Recreate raytracing pipeline to ensure output texture matches
          // current size
          WaitGPUIdle();
          DxrRenderer::CreateRayTracingPipeline(g_windowWidth, g_windowHeight);
        }
#ifdef _DEBUG
        // DXR debug: show UV output from RayGen
        if (ImGui::Checkbox("DXR: Show RayGen UV (debug)", &g_dxrDebugUV)) {
          // Recreate pipeline with debug define; reinitializing RT pipeline
          WaitGPUIdle();
          DxrRenderer::CreateRayTracingPipeline(g_windowWidth, g_windowHeight);
        }
        if (ImGui::Checkbox("Raster: Show UV (debug)", &g_rasterDebugUV)) {
          fprintf(stderr, "Raster: ShowUV set=%d\n", g_rasterDebugUV);
          RasterRenderer::RecreateMeshPipeline(g_device.Get(),
                                               g_rootSignature.Get());
        }

        if (ImGui::Checkbox("Raster: Wireframe / No Cull (debug)",
                            &g_rasterWireframe)) {
          fprintf(stderr, "Raster: Wireframe set=%d\n", g_rasterWireframe);
          RasterRenderer::RecreateMeshPipeline(g_device.Get(),
                                               g_rootSignature.Get());
        }

        if (ImGui::Checkbox("Raster: Debug Depth (shader)",
                            &g_rasterDebugDepth)) {
          fprintf(stderr, "Raster: DebugDepth set=%d\n", g_rasterDebugDepth);
          RasterRenderer::RecreateMeshPipeline(g_device.Get(),
                                               g_rootSignature.Get());
        }
#else
        g_dxrDebugUV = false;
        g_rasterDebugUV = false;
        g_rasterWireframe = false;
        g_rasterDebugDepth = false;
#endif

        ImGui::Separator();
        ImGui::TextWrapped(
            "Raster: Fast scene traversal\nDXR: Unified Ray/Path Tracing");
        ImGui::Separator();
        // Display smoothed FPS computed each frame
        if (g_fps > 0.0f) {
          ImGui::Text("FPS: %.1f (%.2f ms)", g_fps, 1000.0f / g_fps);
        } else {
          ImGui::Text("FPS: N/A");
        }

        // Display profiling info
        ImGui::Separator();
        ImGui::Text("Profiling");
        ImGui::Text("Frame Time: %.2f ms", DxrRenderer::GetFrameTimeMs());
        ImGui::Text("FPS: %.1f", DxrRenderer::GetFPS());
        ImGui::Text("SPP/s: %.1f", DxrRenderer::GetSPPPerSec());
        
        float restirTime, dispatchTime, denoiseTime, noiseTime;
        DxrRenderer::GetGPUTimes(restirTime, dispatchTime, denoiseTime, noiseTime);
        ImGui::Text("GPU Times:");
        ImGui::Text("  ReSTIR: %.2f ms", restirTime);
        ImGui::Text("  DispatchRays: %.2f ms", dispatchTime);
        ImGui::Text("  Denoising: %.2f ms", denoiseTime);
        ImGui::Text("  Noise Calc: %.2f ms", noiseTime);

        // Shader instrumentation counters (if available)
        {
          UINT shaderCounters[16] = {0};
          DxrRenderer::GetShaderCounters(shaderCounters, _countof(shaderCounters));
          ImGui::Text("Shader counters (last frame):");
          ImGui::Text("  TraceRays=%u  Shadow=%u  Spec=%u", shaderCounters[0], shaderCounters[1], shaderCounters[2]);
          ImGui::Text("  TexSamples=%u  VertexFetches=%u  ResReads=%u  ResWrites=%u", shaderCounters[5], shaderCounters[4], shaderCounters[6], shaderCounters[7]);
        }
      }
      ImGui::End();
    }
    // (default ground plane will be added at startup)

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

    if (ImGui::IsKeyPressed(ImGuiKey_M, false)) {
      g_showMaterialEditor = !g_showMaterialEditor;
    }

    if (g_showAssetsWindow) {
      // Scene panel handled by Scene module
      Scene::DrawScenePanel(g_hwnd, g_showAssetsWindow);
    }
    if (g_showMaterialEditor) {
      MaterialEditor::Draw(g_hwnd, g_showMaterialEditor);
    }

    Scene::DrawGizmo();

    // fprintf(stderr, "MainLoop: ImGui::Render start\n");
    ImGui::Render();
    // fprintf(stderr, "MainLoop: ImGui::Render done\n");

    // fprintf(stderr, "MainLoop: PopulateCommandList start\n");
    PopulateCommandList();
    // fprintf(stderr, "MainLoop: PopulateCommandList done\n");

    ID3D12CommandList *ppCommandLists[] = {g_commandList.Get()};
    g_commandQueue->ExecuteCommandLists(_countof(ppCommandLists),
                                        ppCommandLists);
    // fprintf(stderr, "MainLoop: ExecuteCommandLists done\n");

    // fprintf(stderr, "MainLoop: Present start\n");
    ThrowIfFailed(g_swapChain->Present(1, 0));
    // fprintf(stderr, "MainLoop: Present done\n");
    //  Signal and increment the fence value.
    const UINT64 currentFenceValue = g_fenceValues[g_frameIndex];
    ThrowIfFailed(g_commandQueue->Signal(g_fence.Get(), currentFenceValue));
    g_fenceValues[g_frameIndex]++;

    // Wait for previous frame
    WaitForPreviousFrame();

    // Check if the GPU was removed (TDR) during the last frame
    if (CheckDeviceRemoved()) {
      fprintf(stderr, "MainLoop: Device was removed. Re-initializing...\n");
      continue; // Start fresh next iteration
    }

    // fprintf(stderr, "MainLoop: end iteration\n");
  }

  // Shutdown ImGui and cleanup
  ImGui_ImplDX12_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();

  // Cleanup fence event
  CloseHandle(g_fenceEvent);

  return 0;
}
