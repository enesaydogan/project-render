#include "dx12_context.h"

#include "dxr_renderer.h"
#include "streamline_manager.h" // Needed for Streamline init

// Top-level exception handler and driver hints can remain in main.cpp,
// or we can put the hints here. We'll leave them in main.cpp for now.

namespace DX12Context {

// Definitions of globals
ComPtr<ID3D12Device> g_device;
ComPtr<ID3D12CommandQueue> g_commandQueue;
ComPtr<IDXGISwapChain3> g_swapChain;
ComPtr<ID3D12GraphicsCommandList4> g_commandList;
ComPtr<IDXGIFactory4> g_factory;

StreamlineManager g_streamline;

ComPtr<ID3D12Resource> g_renderTargets[FrameCount];
ComPtr<ID3D12DescriptorHeap> g_rtvHeap;
UINT g_rtvDescriptorSize = 0;

ComPtr<ID3D12Resource> g_depthBuffer;
ComPtr<ID3D12DescriptorHeap> g_dsvHeap;

ComPtr<ID3D12Fence> g_fence;
UINT64 g_fenceValues[FrameCount] = {};
HANDLE g_fenceEvent = nullptr;
UINT g_frameIndex = 0;
FrameResource g_frameResources[FrameCount] = {};

UINT g_windowWidth = 1280;
UINT g_windowHeight = 720;

static bool g_hasPendingResize = false;
static UINT g_pendingResizeWidth = 0;
static UINT g_pendingResizeHeight = 0;

// Need external reference to StreamlineManager to initialize early

static void EnableD3D12DebugLayer() {
#ifdef _DEBUG
  ComPtr<ID3D12Debug> debugController;
  if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
    debugController->EnableDebugLayer();
  }
#endif
}

void GetHardwareAdapter(IDXGIFactory4 *pFactory, IDXGIAdapter1 **ppAdapter,
                        bool streamlineReady) {
  *ppAdapter = nullptr;
  ComPtr<IDXGIAdapter1> adapter;
  SIZE_T maxDedicatedMem = 0;
  ComPtr<IDXGIAdapter1> bestAdapter;

  // Probe adapters through the Streamline interposer when available. The
  // Programming Guide expects every D3D12CreateDevice call after slInit to
  // go through the interposer; using the global ::D3D12CreateDevice for
  // the probe is technically a violation and could trip future SL
  // tightening (e.g. interposer-only feature checks).
  auto probeCreateDevice = [&](IUnknown *a, REFIID riid,
                               void **ppDev) -> HRESULT {
    if (streamlineReady) {
      HRESULT hrSL = g_streamline.D3D12CreateDevice(
          a, D3D_FEATURE_LEVEL_11_0, riid, ppDev);
      if (SUCCEEDED(hrSL))
        return hrSL;
    }
    return ::D3D12CreateDevice(a, D3D_FEATURE_LEVEL_11_0, riid, ppDev);
  };

  for (UINT adapterIndex = 0;; ++adapterIndex) {
    if (DXGI_ERROR_NOT_FOUND == pFactory->EnumAdapters1(adapterIndex, &adapter))
      break;

    DXGI_ADAPTER_DESC1 desc;
    adapter->GetDesc1(&desc);

    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
      continue; // skip software adapters

    // Check D3D12 support
    ComPtr<ID3D12Device> testDevice;
    if (SUCCEEDED(probeCreateDevice(adapter.Get(),
                                    IID_PPV_ARGS(&testDevice)))) {
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

bool InitD3D12(HWND hwnd) {
  UINT dxgiFactoryFlags = 0;

  const bool streamlineReady = g_streamline.InitializeEarly();

  EnableD3D12DebugLayer();

  {
    HRESULT hrFactory = E_NOINTERFACE;
    if (streamlineReady) {
      hrFactory = g_streamline.CreateDXGIFactory2(dxgiFactoryFlags,
                                                  IID_PPV_ARGS(&g_factory));
    }
    if (FAILED(hrFactory)) {
      ThrowIfFailed(
          ::CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&g_factory)));
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

  ComPtr<IDXGIAdapter1> hardwareAdapter;
  GetHardwareAdapter(g_factory.Get(), &hardwareAdapter, streamlineReady);

  HRESULT hr = E_FAIL;
  if (hardwareAdapter) {
    hr = CreateDevice(hardwareAdapter.Get(), IID_PPV_ARGS(&g_device));
  }

  if (SUCCEEDED(hr) && streamlineReady) {
    g_streamline.OnD3D12DeviceCreated(g_device.Get());
  }

  // Checking DXR support is in main.cpp, but can stay there afterInitD3D12.
  // We'll let main check it.

  if (FAILED(hr)) {
    hr = CreateDevice(nullptr, IID_PPV_ARGS(&g_device));
    if (SUCCEEDED(hr) && streamlineReady) {
      g_streamline.OnD3D12DeviceCreated(g_device.Get());
    }
    if (FAILED(hr)) {
      ComPtr<IDXGIAdapter> warpAdapter;
      ThrowIfFailed(g_factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));
      ThrowIfFailed(CreateDevice(warpAdapter.Get(), IID_PPV_ARGS(&g_device)));
      if (streamlineReady) {
        g_streamline.OnD3D12DeviceCreated(g_device.Get());
      }
    }
  }

  D3D12_COMMAND_QUEUE_DESC queueDesc = {};
  queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

  hr = g_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&g_commandQueue));
  if (FAILED(hr))
    return false;

  RECT clientRect;
  GetClientRect(hwnd, &clientRect);
  g_windowWidth = clientRect.right - clientRect.left;
  g_windowHeight = clientRect.bottom - clientRect.top;

  DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
  swapChainDesc.BufferCount = FrameCount;
  swapChainDesc.Width = g_windowWidth;
  swapChainDesc.Height = g_windowHeight;
  swapChainDesc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
  swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  swapChainDesc.SampleDesc.Count = 1;

  ComPtr<IDXGISwapChain1> swapChain1;
  HRESULT hrSwap = g_factory->CreateSwapChainForHwnd(g_commandQueue.Get(), hwnd,
                                                     &swapChainDesc, nullptr,
                                                     nullptr, &swapChain1);
  if (FAILED(hrSwap))
    return false;

  HRESULT hrAs = swapChain1.As(&g_swapChain);
  if (FAILED(hrAs))
    return false;

  g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();

  D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
  rtvHeapDesc.NumDescriptors = FrameCount;
  rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  ThrowIfFailed(
      g_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&g_rtvHeap)));
  g_rtvDescriptorSize = g_device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

  // Creates DSV heap
  D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
  dsvHeapDesc.NumDescriptors = 1;
  dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
  dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  ThrowIfFailed(
      g_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&g_dsvHeap)));

  // Create depth buffer
  D3D12_RESOURCE_DESC depthBufferDesc = {};
  depthBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  depthBufferDesc.Width = g_windowWidth;
  depthBufferDesc.Height = g_windowHeight;
  depthBufferDesc.DepthOrArraySize = 1;
  depthBufferDesc.MipLevels = 1;
  depthBufferDesc.Format = DXGI_FORMAT_R32_TYPELESS;
  depthBufferDesc.SampleDesc.Count = 1;
  depthBufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

  D3D12_CLEAR_VALUE depthClear = {};
  depthClear.Format = DXGI_FORMAT_D32_FLOAT;
  depthClear.DepthStencil.Depth = 1.0f;
  depthClear.DepthStencil.Stencil = 0;

  D3D12_HEAP_PROPERTIES depthHeapProps = {};
  depthHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

  ThrowIfFailed(g_device->CreateCommittedResource(
      &depthHeapProps, D3D12_HEAP_FLAG_NONE, &depthBufferDesc,
      D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear,
      IID_PPV_ARGS(&g_depthBuffer)));

  D3D12_DEPTH_STENCIL_VIEW_DESC dsvView = {};
  dsvView.Format = DXGI_FORMAT_D32_FLOAT;
  dsvView.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
  g_device->CreateDepthStencilView(
      g_depthBuffer.Get(), &dsvView,
      g_dsvHeap->GetCPUDescriptorHandleForHeapStart());

  D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
      g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
  for (UINT i = 0; i < FrameCount; ++i) {
    ThrowIfFailed(g_swapChain->GetBuffer(i, IID_PPV_ARGS(&g_renderTargets[i])));
    g_device->CreateRenderTargetView(g_renderTargets[i].Get(), nullptr,
                                     rtvHandle);
    rtvHandle.ptr += g_rtvDescriptorSize;
  }

  for (UINT i = 0; i < FrameCount; ++i) {
    ThrowIfFailed(g_device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&g_frameResources[i].commandAllocator)));
  }

  // Cast pointer to ID3D12GraphicsCommandList, then QueryInterface or
  // CreateCommandList using the ComPtr<ID3D12GraphicsCommandList4> type
  ComPtr<ID3D12GraphicsCommandList> tempCmdList;
  ThrowIfFailed(g_device->CreateCommandList(
      0, D3D12_COMMAND_LIST_TYPE_DIRECT,
      g_frameResources[g_frameIndex].commandAllocator.Get(), nullptr,
      IID_PPV_ARGS(&tempCmdList)));
  tempCmdList.As(&g_commandList);
  ThrowIfFailed(g_commandList->Close());

  ThrowIfFailed(g_device->CreateFence(g_fenceValues[g_frameIndex],
                                      D3D12_FENCE_FLAG_NONE,
                                      IID_PPV_ARGS(&g_fence)));
  g_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  if (g_fenceEvent == nullptr) {
    return false;
  }

  for (UINT i = 0; i < FrameCount; ++i)
    g_fenceValues[i] = 0;

  return true;
}

void WaitForPreviousFrame() {
  const UINT64 currentFenceValue = g_fenceValues[g_frameIndex];
  ThrowIfFailed(g_commandQueue->Signal(g_fence.Get(), currentFenceValue));

  g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();

  if (g_fence->GetCompletedValue() < g_fenceValues[g_frameIndex]) {
    ThrowIfFailed(g_fence->SetEventOnCompletion(g_fenceValues[g_frameIndex],
                                                g_fenceEvent));
    WaitForSingleObject(g_fenceEvent, INFINITE);
  }
  g_fenceValues[g_frameIndex] = currentFenceValue + 1;
}

void WaitGPUIdle() {
  if (!g_commandQueue || !g_fence || !g_fenceEvent)
    return;
  const UINT64 waitValue = g_fenceValues[g_frameIndex] + 100;
  HRESULT hr = g_commandQueue->Signal(g_fence.Get(), waitValue);
  if (FAILED(hr))
    return;

  g_fence->SetEventOnCompletion(waitValue, g_fenceEvent);
  WaitForSingleObject(g_fenceEvent, 5000);

  for (UINT i = 0; i < FrameCount; ++i) {
    g_fenceValues[i] = waitValue + 1;
  }
}

void QueueResize(UINT width, UINT height) {
  if (width == 0 || height == 0)
    return;
  if (!g_hasPendingResize && width == g_windowWidth &&
      height == g_windowHeight)
    return;
  g_pendingResizeWidth = width;
  g_pendingResizeHeight = height;
  g_hasPendingResize = true;
}

bool ConsumePendingResize(UINT &width, UINT &height) {
  if (!g_hasPendingResize)
    return false;
  width = g_pendingResizeWidth;
  height = g_pendingResizeHeight;
  g_hasPendingResize = false;
  return true;
}

void ResizeSwapChain(UINT width, UINT height) {
  if (width == 0 || height == 0)
    return;

  WaitGPUIdle();

  for (UINT i = 0; i < FrameCount; ++i) {
    g_renderTargets[i].Reset();
  }
  g_depthBuffer.Reset();

  DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
  g_swapChain->GetDesc(&swapChainDesc);
  HRESULT hrResize = g_swapChain->ResizeBuffers(FrameCount, width, height,
                                                swapChainDesc.BufferDesc.Format,
                                                swapChainDesc.Flags);
  if (FAILED(hrResize)) {
    HRESULT removedReason = g_device ? g_device->GetDeviceRemovedReason() : S_OK;
    fprintf(stderr,
            "DX12Context::ResizeSwapChain failed (hr=0x%08x, "
            "deviceRemovedReason=0x%08x, size=%ux%u)\n",
            (unsigned)hrResize, (unsigned)removedReason, width, height);
    return;
  }

  g_windowWidth = width;
  g_windowHeight = height;

  D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
      g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
  for (UINT i = 0; i < FrameCount; ++i) {
    ThrowIfFailed(g_swapChain->GetBuffer(i, IID_PPV_ARGS(&g_renderTargets[i])));
    g_device->CreateRenderTargetView(g_renderTargets[i].Get(), nullptr,
                                     rtvHandle);
    rtvHandle.ptr += g_rtvDescriptorSize;
  }

  DxrRenderer::CreateRayTracingPipeline(width, height);

  D3D12_RESOURCE_DESC depthDesc = {};
  depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  depthDesc.Width = width;
  depthDesc.Height = height;
  depthDesc.DepthOrArraySize = 1;
  depthDesc.MipLevels = 1;
  depthDesc.Format = DXGI_FORMAT_R32_TYPELESS;
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

} // namespace DX12Context
