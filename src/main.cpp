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
#include "dxr_renderer.h"
#include "file_import.h"
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"
#include "raster_renderer.h"
#include "scene.h"
#include "light.h"
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

// Render Mode System
enum class RenderMode {
  Raster,     // Fast rasterization for scene traversal
  Raytracing, // Current DXR implementation
  PathTracing // Full path tracing with ReSTIR (future)
};

static RenderMode g_currentRenderMode = RenderMode::Raster;
static bool g_showRenderModeWindow = true;
// Debug toggles for DXR
bool g_dxrDebugUV = false;
bool g_dxrDumpPixels = false;
bool g_dxrHitDebug = false; // encode primitive ID in hit shader for debugging
bool g_dxrDumpD3D12Messages = false; // dump D3D12 InfoQueue messages to stderr
bool g_rasterDebugUV = false; // show raster UVs in mesh pixel shader (debug)
bool g_verboseRenderLogs =
    false; // when true, prints render-loop diagnostics (off by default)

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

// Loaded meshes from Asset loader
std::vector<Asset::GpuMesh> g_loadedMeshes;
std::vector<Asset::Material> g_loadedMaterials;
std::vector<Asset::Texture> g_loadedTextures;
D3D12_GPU_DESCRIPTOR_HANDLE g_texturesGpuStart = {0};
static ComPtr<ID3D12Resource>
    g_materialBuffer; // Persistent material constant buffer
UINT g_textureDescriptorCount = 0;
static ComPtr<ID3D12Resource> g_materialConstantBuffer;
static bool g_showAssetsWindow =
    true; // Controls visibility of the Assets panel (can be closed/reopened)
static bool g_showControlsWindow =
    true; // Controls visibility of the Controls panel (can be closed/reopened)
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
ComPtr<ID3D12RootSignature> g_rootSignature;
static ComPtr<ID3D12PipelineState> g_pipelineState;
static ComPtr<ID3D12PipelineState> g_meshPipelineState;
static ComPtr<ID3D12Resource> g_vertexBuffer;
static D3D12_VERTEX_BUFFER_VIEW g_vertexBufferView = {};
static ComPtr<ID3D12Resource> g_constantBuffer;
static float g_offsetX = 0.2f;

// Grid rendering resources
static ComPtr<ID3D12Resource> g_gridVertexBuffer;
static D3D12_VERTEX_BUFFER_VIEW g_gridVBView = {};
static UINT g_gridVertexCount = 0;
static ComPtr<ID3D12PipelineState> g_gridPipelineState;
// Grid line thickness in world units (used to expand lines into thin quads)
static float g_gridThickness = 0.005f; // increase to make lines thicker

static bool g_drawGrid = true; // toggle grid rendering

// Small camera module is defined in src/camera.h/.cpp
#include "camera.h"

// Simple Vec3 helper for CPU-side math
struct Vec3 {
  float x, y, z;
};

// --- DXR Globals ---
// DXR implementation moved to DxrRenderer module
#include "dxr_renderer.h"

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

  EnableD3D12DebugLayer();

  ComPtr<IDXGIFactory4> factory;
  ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

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
    hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                           IID_PPV_ARGS(&g_device));

    if (FAILED(hr)) {
      ComPtr<IDXGIAdapter> warpAdapter;
      ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));
      ThrowIfFailed(D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                      IID_PPV_ARGS(&g_device)));
    }
  }

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
  swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
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
                         1024, FrameCount);
  // Log: CBV/SRV allocator initialized (stderr only)
  fprintf(stderr, "InitD3D12: CBV/SRV allocator initialized\n");

  // Create descriptor heap for ImGui fonts (1 descriptor)
  D3D12_DESCRIPTOR_HEAP_DESC imguiHeapDesc = {};
  imguiHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  imguiHeapDesc.NumDescriptors = 1;
  imguiHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  HRESULT hrImg = g_device->CreateDescriptorHeap(&imguiHeapDesc,
                                                 IID_PPV_ARGS(&g_imguiHeap));
  if (FAILED(hrImg)) {
    // Log to stderr only
    fprintf(stderr, "InitD3D12: CreateDescriptorHeap for ImGui failed 0x%08x\n",
            (unsigned)hrImg);
    return false;
  }
  // Log: ImGui descriptor heap created (stderr only)
  fprintf(stderr, "InitD3D12: ImGui descriptor heap created\n");

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
  psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
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
  meshPsoDesc.DepthStencilState.DepthEnable = FALSE;

  {
    HRESULT hrMesh = g_device->CreateGraphicsPipelineState(&meshPsoDesc, IID_PPV_ARGS(&g_meshPipelineState));
    if (FAILED(hrMesh)) {
      fprintf(stderr, "InitD3D12: CreateGraphicsPipelineState (mesh) failed: 0x%08x\n", (unsigned)hrMesh);
#ifdef _DEBUG
      ComPtr<ID3D12InfoQueue> infoQueue;
      if (SUCCEEDED(g_device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
        UINT64 num = infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();
        for (UINT64 mi = 0; mi < num; ++mi) {
          SIZE_T messageLength = 0;
          infoQueue->GetMessage(mi, nullptr, &messageLength);
          std::vector<char> message(messageLength);
          D3D12_MESSAGE* pMsg = reinterpret_cast<D3D12_MESSAGE*>(message.data());
          infoQueue->GetMessage(mi, pMsg, &messageLength);
          fprintf(stderr, "D3D12 INFO (PSO create): Category=%d Severity=%d ID=%d: %s\n",
                  (int)pMsg->Category, (int)pMsg->Severity, (int)pMsg->ID, pMsg->pDescription);
        }
      }
#endif
    }
    ThrowIfFailed(hrMesh);
  }

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
  struct MaterialCB {
    float baseColorFactor[4];
    float params1[4];
    float specular[4];
    float emissiveFactor[4];
    int textureIndices[4];
    int emissiveAndPad[4]; // x=emissiveTexIndex, yzw=padding
    float lightDir[4];
    float lightColor[4];
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
    WaitForSingleObject(g_fenceEvent, INFINITE);
  }

  // Set the fence value for the next frame.
  g_fenceValues[g_frameIndex] = currentFenceValue + 1;
}

void ResizeSwapChain(UINT width, UINT height) {
  if (width == 0 || height == 0)
    return;

  // Wait for GPU to finish
  WaitForPreviousFrame();

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
  case WM_SIZE:
    if (g_swapChain && wParam != SIZE_MINIMIZED) {
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

  // Log that we showed the window (stderr only)
  fprintf(stderr, "ShowWindow called\n");

  if (!InitD3D12(hwnd)) {
    MessageBoxA(nullptr, "Failed to initialize D3D12", "Error",
                MB_OK | MB_ICONERROR);
    return -1;
  }

  // Log successful D3D12 initialization (stderr only)
  fprintf(stderr, "InitD3D12 returned OK\n");

  // Auto-load sample glTF at startup (for automated DXR testing)
  // Use command line argument if provided, otherwise use default
  std::string autoLoadPath =
      customGltfPath.empty() ? "assets/DamagedHelmet.glb" : customGltfPath;
  try {
        if (fs::exists(autoLoadPath)) {
      if (!Scene::ImportGltf(autoLoadPath)) {
        fprintf(stderr, "AutoLoad: failed to import %s\n",
                autoLoadPath.c_str());
      } else {
        fprintf(stderr, "AutoLoad: imported %s\n", autoLoadPath.c_str());
        // Reset camera to default view after auto-import
        ResetCamera();
      }
    } else {
      // Log to stderr only
      fprintf(stderr, "AutoLoad: %s not found - creating synthetic test mesh\n",
              autoLoadPath.c_str());

      // Create a cube mesh (centered at origin, size 1.0) to exercise
      // TLAS/DispatchRays
      try {
        Asset::GpuMesh gm;
        Asset::Vertex cubeVerts[8] = {
            {{-0.5f, -0.5f, -0.5f}, {0, 0, -1}, {1, 0, 0, 1}, {0.0f, 0.0f}},
            {{0.5f, -0.5f, -0.5f}, {0, 0, -1}, {1, 0, 0, 1}, {1.0f, 0.0f}},
            {{0.5f, 0.5f, -0.5f}, {0, 0, -1}, {1, 0, 0, 1}, {1.0f, 1.0f}},
            {{-0.5f, 0.5f, -0.5f}, {0, 0, -1}, {1, 0, 0, 1}, {0.0f, 1.0f}},

            {{-0.5f, -0.5f, 0.5f}, {0, 0, 1}, {1, 0, 0, 1}, {0.0f, 0.0f}},
            {{0.5f, -0.5f, 0.5f}, {0, 0, 1}, {1, 0, 0, 1}, {1.0f, 0.0f}},
            {{0.5f, 0.5f, 0.5f}, {0, 0, 1}, {1, 0, 0, 1}, {1.0f, 1.0f}},
            {{-0.5f, 0.5f, 0.5f}, {0, 0, 1}, {1, 0, 0, 1}, {0.0f, 1.0f}},
        };
        // 12 triangles (36 indices)
        UINT indices[36] = {// back face
                            0, 1, 2, 0, 2, 3,
                            // front face
                            4, 6, 5, 4, 7, 6,
                            // left
                            4, 0, 3, 4, 3, 7,
                            // right
                            1, 5, 6, 1, 6, 2,
                            // bottom
                            4, 5, 1, 4, 1, 0,
                            // top
                            3, 2, 6, 3, 6, 7};

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

        ThrowIfFailed(g_device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &vbDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&gm.vertexBuffer)));

        // copy vertex data
        UINT8 *pData = nullptr;
        D3D12_RANGE readRange = {0, 0};
        ThrowIfFailed(gm.vertexBuffer->Map(0, &readRange,
                                           reinterpret_cast<void **>(&pData)));
        memcpy(pData, cubeVerts, sizeof(cubeVerts));
        gm.vertexBuffer->Unmap(0, nullptr);

        gm.vbView.BufferLocation = gm.vertexBuffer->GetGPUVirtualAddress();
        gm.vbView.StrideInBytes = sizeof(Asset::Vertex);
        gm.vbView.SizeInBytes = sizeof(cubeVerts);

        D3D12_RESOURCE_DESC ibDesc = vbDesc;
        ibDesc.Width = sizeof(indices);
        ThrowIfFailed(g_device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &ibDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&gm.indexBuffer)));
        pData = nullptr;
        ThrowIfFailed(gm.indexBuffer->Map(0, &readRange,
                                          reinterpret_cast<void **>(&pData)));
        memcpy(pData, indices, sizeof(indices));
        gm.indexBuffer->Unmap(0, nullptr);

        gm.ibView.BufferLocation = gm.indexBuffer->GetGPUVirtualAddress();
        gm.ibView.Format = DXGI_FORMAT_R32_UINT;
        gm.ibView.SizeInBytes = sizeof(indices);

        gm.vertexCount = (UINT)std::size(cubeVerts);
        gm.indexCount = (UINT)std::size(indices);
        gm.materialIndex = -1;

        g_loadedMeshes.push_back(gm);

        // Log to stderr only
        fprintf(stderr, "AutoLoad: Added synthetic cube mesh\n");

        // Build AS for synthetic mesh
        Scene::RebuildAccelerationStructures();

      } catch (const std::exception &e2) {
        // Log to stderr only
        fprintf(stderr, "AutoLoad: exception creating synthetic mesh: %s\n",
                e2.what());
      }
    }
  } catch (const std::exception &e) {
    // Log to stderr only
    fprintf(stderr, "AutoLoad: exception: %s\n", e.what());
  }

    // Add a default ground plane (10x10)
    AddDefaultPlane();
    Scene::RebuildAccelerationStructures();

  // Basic message loop + simple render
  MSG msg = {};

  auto PopulateCommandList = [&]() {
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
    case RenderMode::Raytracing: {
      // Use DXR module to perform ray dispatch and copy to backbuffer
      if (DxrRenderer::IsReady()) {
        if (!DxrRenderer::RenderFrame(
                g_commandList.Get(), g_frameIndex,
                g_renderTargets[g_frameIndex].Get(), rtvHandle,
                g_cameraConstantBuffer.Get(), g_materialConstantBuffer.Get(),
                g_texturesGpuStart, g_textureDescriptorCount,
                Scene::GetActiveMeshes())) {
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

      FLOAT clearColor[] = {0.2f, 0.3f, 0.4f, 1.0f};
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

      // Draw ground grid (optional) via raster module
      if (g_drawGrid) {
        RasterRenderer::DrawGrid(g_commandList.Get(),
                                 g_cameraConstantBuffer.Get());
      }

      // Draw loaded meshes
      if (!g_loadedMeshes.empty() && g_meshPipelineState) {
        // Log to stderr only (controlled by verbose flag)
        if (g_verboseRenderLogs)
          fprintf(stderr, "Drawing %zu meshes\n", g_loadedMeshes.size());
        g_commandList->SetPipelineState(g_meshPipelineState.Get());
        g_commandList->IASetPrimitiveTopology(
            D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12DescriptorHeap *heaps[] = {g_cbvSrvAllocator.Heap()};
        g_commandList->SetDescriptorHeaps(_countof(heaps), heaps);
        // Ensure render targets are set for mesh rendering
        g_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
        // Use camera constant buffer for mesh rendering
        if (g_cameraConstantBuffer) {
          g_commandList->SetGraphicsRootConstantBufferView(
              0, g_cameraConstantBuffer->GetGPUVirtualAddress());
        }

        // Draw all meshes (not just the first one)
        for (size_t i = 0; i < g_loadedMeshes.size(); ++i) {
          const auto &gm = g_loadedMeshes[i];
          // Skip meshes that have been deleted or not properly initialized
          if (!gm.vertexBuffer || !gm.indexBuffer || gm.ibView.SizeInBytes == 0)
            continue;

          g_commandList->IASetVertexBuffers(0, 1, &gm.vbView);
          g_commandList->IASetIndexBuffer(&gm.ibView);

          if (gm.materialIndex >= 0 &&
              gm.materialIndex < (int)g_loadedMaterials.size()) {
            // Upload material data for this mesh into the persistent material CB
            struct MaterialCB {
              float baseColorFactor[4];
              float params1[4];
              float specular[4];
              float emissiveFactor[4];
              int textureIndices[4];
              int emissiveAndPad[4];
              float lightDir[4];
              float lightColor[4];
            } matCB;

            const auto &srcMat = g_loadedMaterials[gm.materialIndex];
            matCB.baseColorFactor[0] = srcMat.baseColorFactor[0];
            matCB.baseColorFactor[1] = srcMat.baseColorFactor[1];
            matCB.baseColorFactor[2] = srcMat.baseColorFactor[2];
            matCB.baseColorFactor[3] = srcMat.baseColorFactor[3];
            matCB.params1[0] = srcMat.metallicFactor;
            matCB.params1[1] = srcMat.roughnessFactor;
            matCB.params1[2] = (float)srcMat.workflow;
            matCB.params1[3] = 0.0f;
            matCB.specular[0] = srcMat.specularFactor[0];
            matCB.specular[1] = srcMat.specularFactor[1];
            matCB.specular[2] = srcMat.specularFactor[2];
            matCB.specular[3] = srcMat.glossinessFactor;
            matCB.emissiveFactor[0] = 0.0f; matCB.emissiveFactor[1] = 0.0f; matCB.emissiveFactor[2] = 0.0f; matCB.emissiveFactor[3] = 0.0f;
            matCB.textureIndices[0] = srcMat.baseColorTexture;
            matCB.textureIndices[1] = srcMat.metallicRoughnessTexture;
            matCB.textureIndices[2] = srcMat.normalTexture;
            matCB.textureIndices[3] = srcMat.occlusionTexture;
            matCB.emissiveAndPad[0] = srcMat.emissiveTexture;
            matCB.emissiveAndPad[1] = matCB.emissiveAndPad[2] = matCB.emissiveAndPad[3] = 0;

            // Copy global default light into material CB so both raster and DXR shaders can read it
            extern DirectionalLight g_defaultLight;
            matCB.lightDir[0] = g_defaultLight.dir[0];
            matCB.lightDir[1] = g_defaultLight.dir[1];
            matCB.lightDir[2] = g_defaultLight.dir[2];
            matCB.lightDir[3] = g_defaultLight.dir[3];
            matCB.lightColor[0] = g_defaultLight.color[0];
            matCB.lightColor[1] = g_defaultLight.color[1];
            matCB.lightColor[2] = g_defaultLight.color[2];
            matCB.lightColor[3] = g_defaultLight.color[3];

            if (g_materialConstantBuffer) {
              UINT8 *pMat = nullptr;
              D3D12_RANGE readRange = {0,0};
              if (SUCCEEDED(g_materialConstantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pMat)))) {
                memcpy(pMat, &matCB, sizeof(matCB));
                g_materialConstantBuffer->Unmap(0, nullptr);
              }
              g_commandList->SetGraphicsRootConstantBufferView(2, g_materialConstantBuffer->GetGPUVirtualAddress());
            }
            if (g_textureDescriptorCount > 0)
              g_commandList->SetGraphicsRootDescriptorTable(1, g_texturesGpuStart);
          }
          if (gm.ibView.SizeInBytes > 0)
            g_commandList->DrawIndexedInstanced(gm.ibView.SizeInBytes / 4, 1, 0,
                                                0, 0);
        }
      }
      break;
    }

    case RenderMode::PathTracing: {
      // TODO: Implement full path tracing with ReSTIR
      TR(g_commandList.Get(), g_renderTargets[g_frameIndex].Get(),
         D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

      FLOAT clearColor[] = {0.1f, 0.1f, 0.1f, 1.0f}; // Dark to indicate WIP
      g_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
      g_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
      break;
    }
    }

    // Render ImGui (Overlay on top of whatever was drawn)
    ID3D12DescriptorHeap *ppHeaps[] = {g_imguiHeap.Get()};
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

  // Log entering main loop (stderr only)
  fprintf(stderr, "Entering main loop\n");
  fflush(stderr);

  // Setup timing for camera movement
  static auto prevTime = std::chrono::high_resolution_clock::now();

  // Enter main loop (simple, no extra SEH wrappers)
  while (msg.message != WM_QUIT) {
    //fprintf(stderr, "MainLoop: start iteration\n");
    fflush(stderr);
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
      // Update yaw/pitch directly (FPS-style mouse look) - reversed axes
      g_camYaw += dx * sensitivity;
      g_camPitch += dy * sensitivity;

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
      // Vertical movement: E up, Q down (world up)
      if (GetAsyncKeyState('E') & 0x8000) {
        move.x += worldUp.x;
        move.y += worldUp.y;
        move.z += worldUp.z;
      }
      if (GetAsyncKeyState('Q') & 0x8000) {
        move.x -= worldUp.x;
        move.y -= worldUp.y;
        move.z -= worldUp.z;
      }
    }

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

    // Ensure aspect matches the window and update camera CB on GPU
    g_cameraData.params[1] = (float)g_windowWidth / (float)g_windowHeight;
    if (g_cameraConstantBuffer) {
      UINT8 *pCam = nullptr;
      D3D12_RANGE readRange = {0, 0};
      if (SUCCEEDED(g_cameraConstantBuffer->Map(
              0, &readRange, reinterpret_cast<void **>(&pCam)))) {
        memcpy(pCam, &g_cameraData, sizeof(g_cameraData));
        g_cameraConstantBuffer->Unmap(0, nullptr);
      }
    }

    // Start ImGui frame
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // Main menu bar: Window menu + quick panel toggles on the bar for fast
    // access
    if (ImGui::BeginMainMenuBar()) {
      // Keep Window menu for non-toggle commands
      if (ImGui::BeginMenu("Window")) {
        if (ImGui::MenuItem("Reset Layout")) {
          g_showAssetsWindow = true;
          g_showControlsWindow = true;
          g_showRenderModeWindow = true;
          g_forceUncollapse = true;
        }
        ImGui::EndMenu();
      }

      // Quick access toggles (side-by-side) for panels
      ImGui::SameLine();
      ImGui::Text("Panels:");
      ImGui::SameLine();
      // Use compact spacing for menu bar toggles
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 6));
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
      ImGui::PopStyleVar();

      ImGui::EndMainMenuBar();
    }

    // UI: Camera controls and debug info
    if (g_showControlsWindow) {
      if (ImGui::Begin("Controls", &g_showControlsWindow,
                       ImGuiWindowFlags_NoCollapse)) {

        // Camera Debug Info
        ImGui::Text("Camera Position: (%.2f, %.2f, %.2f)", g_cameraData.pos[0],
                    g_cameraData.pos[1], g_cameraData.pos[2]);
        ImGui::Text("Camera Forward: (%.2f, %.2f, %.2f)",
                    g_cameraData.forward[0], g_cameraData.forward[1],
                    g_cameraData.forward[2]);
        ImGui::Text("Camera Up: (%.2f, %.2f, %.2f)", g_cameraData.up[0],
                    g_cameraData.up[1], g_cameraData.up[2]);
        {
          float vFov = g_cameraData.params[0];
          float aspect = g_cameraData.params[1];
          float vHalfRad = vFov * 0.5f * (3.14159265f / 180.0f);
          float hFov =
              2.0f * atanf(tanf(vHalfRad) * aspect) * (180.0f / 3.14159265f);
          ImGui::Text("FOV V/H: %.1f° / %.1f°, Aspect: %.2f", vFov, hFov,
                      aspect);
        }
        ImGui::Text("Near: %.2f, Far: %.2f", g_cameraData.params[2],
                    g_cameraData.params[3]);
        ImGui::Text("Intensity: %.2f", g_cameraData.params[4]);

        ImGui::Separator();

        // Controls
        // Horizontal-FOV slider (UI shows H, shaders use V). Convert H -> V
        // before storing.
        {
          float aspect = g_cameraData.params[1];
          // compute current horizontal FOV from stored vertical FOV
          float curV = g_cameraData.params[0];
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
            g_cameraData.params[0] = vFovNew;
            // Update camera CB
            if (g_cameraConstantBuffer) {
              UINT8 *pCam = nullptr;
              D3D12_RANGE readRange = {0, 0};
              if (SUCCEEDED(g_cameraConstantBuffer->Map(
                      0, &readRange, reinterpret_cast<void **>(&pCam)))) {
                memcpy(pCam, &g_cameraData, sizeof(g_cameraData));
                g_cameraConstantBuffer->Unmap(0, nullptr);
              }
            }
          }
        }
        if (ImGui::SliderFloat("Intensity", &g_cameraData.params[4], 0.0f,
                               5.0f)) {
          // Update camera CB
          if (g_cameraConstantBuffer) {
            UINT8 *pCam = nullptr;
            D3D12_RANGE readRange = {0, 0};
            if (SUCCEEDED(g_cameraConstantBuffer->Map(
                    0, &readRange, reinterpret_cast<void **>(&pCam)))) {
              memcpy(pCam, &g_cameraData, sizeof(g_cameraData));
              g_cameraConstantBuffer->Unmap(0, nullptr);
              // Debug: print camera params when intensity changes
              fprintf(stderr,
                      "Camera params after Intensity change: fov=%.3f "
                      "aspect=%.3f near=%.3f far=%.3f intensity=%.3f\n",
                      g_cameraData.params[0], g_cameraData.params[1],
                      g_cameraData.params[2], g_cameraData.params[3],
                      g_cameraData.params[4]);
            }
          }
        }
        if (ImGui::Button("Reset Camera")) {
          ResetCamera();
          // Update camera CB
          if (g_cameraConstantBuffer) {
            UINT8 *pCam = nullptr;
            D3D12_RANGE readRange = {0, 0};
            if (SUCCEEDED(g_cameraConstantBuffer->Map(
                    0, &readRange, reinterpret_cast<void **>(&pCam)))) {
              memcpy(pCam, &g_cameraData, sizeof(g_cameraData));
              g_cameraConstantBuffer->Unmap(0, nullptr);
            }
          }
        }

        // Camera movement & mouse sensitivity controls
        ImGui::Spacing();
        if (ImGui::SliderFloat("Move Speed", &g_camSpeed, 0.1f, 20.0f)) {
          // no additional action required; movement uses g_camSpeed immediately
        }
        if (ImGui::SliderFloat("Mouse Sensitivity", &g_mouseSensitivity, 0.001f,
                               0.05f)) {
          // sensitivity applied next frame via g_mouseSensitivity
        }

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
        }
        if (ImGui::Checkbox("Verbose Render Logs", &g_verboseRenderLogs)) {
          fprintf(stderr, "Verbose Render Logs set=%d\n", g_verboseRenderLogs);
        }
      }
      ImGui::End();
    }

    // Render Mode Selector
    ImGui::SetNextWindowSize(ImVec2(300, 150), ImGuiCond_FirstUseEver);
    if (g_showRenderModeWindow) {
      if (ImGui::Begin("Render Mode", &g_showRenderModeWindow,
                       ImGuiWindowFlags_NoCollapse)) {
        ImGui::Text("Current Mode: %s",
                    g_currentRenderMode == RenderMode::Raster ? "Raster"
                    : g_currentRenderMode == RenderMode::Raytracing
                        ? "Raytracing"
                        : "Path Tracing");

        if (ImGui::RadioButton("Fast Raster",
                               g_currentRenderMode == RenderMode::Raster)) {
          g_currentRenderMode = RenderMode::Raster;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Raytracing",
                               g_currentRenderMode == RenderMode::Raytracing)) {
          g_currentRenderMode = RenderMode::Raytracing;
          // Recreate raytracing pipeline to ensure output texture matches current size
          DxrRenderer::CreateRayTracingPipeline(g_windowWidth, g_windowHeight);
        }
        // DXR debug: show UV output from RayGen
        if (ImGui::Checkbox("DXR: Show RayGen UV (debug)", &g_dxrDebugUV)) {
          // Recreate pipeline with debug define; reinitializing RT pipeline
          DxrRenderer::CreateRayTracingPipeline(g_windowWidth, g_windowHeight);
        }
        if (ImGui::Checkbox("DXR: Dump Output Pixels (debug, stalls)",
                            &g_dxrDumpPixels)) {
          fprintf(stderr, "DXR: DumpOutputPixels set=%d\n", g_dxrDumpPixels);
        }
        if (ImGui::Checkbox("DXR: Encode Hit PrimID (debug)", &g_dxrHitDebug)) {
          fprintf(stderr, "DXR: HitDebug set=%d\n", g_dxrHitDebug);
          DxrRenderer::CreateRayTracingPipeline(g_windowWidth, g_windowHeight);
        }
        if (ImGui::Checkbox("DXR: Dump D3D12 Messages (debug)",
                            &g_dxrDumpD3D12Messages)) {
          fprintf(stderr, "DXR: DumpD3D12Messages set=%d\n",
                  g_dxrDumpD3D12Messages);
        }
        if (ImGui::Checkbox("Raster: Show UV (debug)", &g_rasterDebugUV)) {
          fprintf(stderr, "Raster: ShowUV set=%d\n", g_rasterDebugUV);
          RasterRenderer::RecreateMeshPipeline(g_device.Get(),
                                               g_rootSignature.Get());
        }

        if (ImGui::RadioButton("Path Tracing (WIP)",
                               g_currentRenderMode ==
                                   RenderMode::PathTracing)) {
          g_currentRenderMode = RenderMode::PathTracing;
          // TODO: Implement path tracing
        }

        ImGui::Separator();
        ImGui::TextWrapped(
            "Raster: Fast scene traversal\nRaytracing: Current DXR\nPath "
            "Tracing: Advanced ReSTIR (future)");
        ImGui::Separator();
        // Display smoothed FPS computed each frame
        if (g_fps > 0.0f) {
          ImGui::Text("FPS: %.1f (%.2f ms)", g_fps, 1000.0f / g_fps);
        } else {
          ImGui::Text("FPS: N/A");
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
    if (g_showAssetsWindow) {
      // Scene panel handled by Scene module
      Scene::DrawScenePanel(g_hwnd, g_showAssetsWindow);
    }

    //fprintf(stderr, "MainLoop: ImGui::Render start\n");
    ImGui::Render();
    //fprintf(stderr, "MainLoop: ImGui::Render done\n");

    //fprintf(stderr, "MainLoop: PopulateCommandList start\n");
    PopulateCommandList();
    //fprintf(stderr, "MainLoop: PopulateCommandList done\n");

    ID3D12CommandList *ppCommandLists[] = {g_commandList.Get()};
    g_commandQueue->ExecuteCommandLists(_countof(ppCommandLists),
                      ppCommandLists);
    //fprintf(stderr, "MainLoop: ExecuteCommandLists done\n");

    //fprintf(stderr, "MainLoop: Present start\n");
    ThrowIfFailed(g_swapChain->Present(1, 0));
    //fprintf(stderr, "MainLoop: Present done\n");
    // Signal and increment the fence value.
    const UINT64 currentFenceValue = g_fenceValues[g_frameIndex];
    ThrowIfFailed(g_commandQueue->Signal(g_fence.Get(), currentFenceValue));
    g_fenceValues[g_frameIndex]++;

    // Wait for previous frame
    WaitForPreviousFrame();
    //fprintf(stderr, "MainLoop: end iteration\n");
  }

  // Shutdown ImGui and cleanup
  ImGui_ImplDX12_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();

  // Cleanup fence event
  CloseHandle(g_fenceEvent);

  return 0;
}
