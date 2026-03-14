#pragma once

#include "d3d12_helpers.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

using Microsoft::WRL::ComPtr;

const UINT FrameCount = 3;

class StreamlineManager;

namespace DX12Context {

// Device and primary interfaces
extern ComPtr<ID3D12Device> g_device;
extern ComPtr<ID3D12CommandQueue> g_commandQueue;
extern ComPtr<IDXGISwapChain3> g_swapChain;
extern ComPtr<ID3D12GraphicsCommandList4> g_commandList;
extern ComPtr<IDXGIFactory4> g_factory;

// Render Targets / Swapchain resources
extern ComPtr<ID3D12Resource> g_renderTargets[FrameCount];
extern ComPtr<ID3D12DescriptorHeap> g_rtvHeap;
extern UINT g_rtvDescriptorSize;

// Depth Buffer (Main)
extern ComPtr<ID3D12Resource> g_depthBuffer;
extern ComPtr<ID3D12DescriptorHeap> g_dsvHeap;

// Fences and Frame Sync
extern ComPtr<ID3D12Fence> g_fence;
extern UINT64 g_fenceValues[FrameCount];
extern HANDLE g_fenceEvent;
extern UINT g_frameIndex;
extern FrameResource g_frameResources[FrameCount];

extern UINT g_windowWidth;
extern UINT g_windowHeight;

extern StreamlineManager g_streamline;

// Initialization and Lifecycle
bool InitD3D12(HWND hwnd);
void ResizeSwapChain(UINT width, UINT height);
void WaitForPreviousFrame();
void WaitGPUIdle();
void QueueResize(UINT width, UINT height);
bool ConsumePendingResize(UINT &width, UINT &height);

// GPU Adapter query
void GetHardwareAdapter(IDXGIFactory4 *pFactory, IDXGIAdapter1 **ppAdapter);

} // namespace DX12Context
