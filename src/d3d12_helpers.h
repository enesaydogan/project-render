#pragma once

#include <cstdio>
#include <d3d12.h>
#include <iostream>
#include <vector>
#include <windows.h>
#include <wrl.h>


using Microsoft::WRL::ComPtr;

struct DescriptorAllocation {
  D3D12_CPU_DESCRIPTOR_HANDLE cpu;
  D3D12_GPU_DESCRIPTOR_HANDLE gpu;
  UINT offset; // descriptor index
};

class DescriptorHeapAllocator {
public:
  DescriptorHeapAllocator() = default;

  void Init(ID3D12Device *device, D3D12_DESCRIPTOR_HEAP_TYPE type,
            UINT numDescriptors, UINT frameCount) {
    m_device = device;
    m_type = type;
    m_numDescriptors = numDescriptors;
    m_frameCount = frameCount;

    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.NumDescriptors = numDescriptors;
    desc.Type = type;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    desc.NodeMask = 0;
    device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_heap));

    m_descriptorSize = device->GetDescriptorHandleIncrementSize(type);
    m_nextOffset.assign(frameCount, 0);
  }

  void ResetFrame(UINT frameIndex) {
    if (frameIndex < m_nextOffset.size())
      m_nextOffset[frameIndex] = 0;
  }

  // allocate 'count' descriptors for the given frame, returning CPU/GPU handles
  DescriptorAllocation Allocate(UINT frameIndex, UINT count) {
    DescriptorAllocation alloc = {};
    UINT offset = m_nextOffset[frameIndex];
    if (offset + count > m_numDescriptors) {
      // simple wrap-around fallback (not ideal)
      offset = 0;
      m_nextOffset[frameIndex] = count;
    } else {
      m_nextOffset[frameIndex] += count;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE cpuStart =
        m_heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE gpuStart =
        m_heap->GetGPUDescriptorHandleForHeapStart();

    alloc.offset = offset;
    alloc.cpu.ptr = cpuStart.ptr + (SIZE_T)offset * m_descriptorSize;
    alloc.gpu.ptr = gpuStart.ptr + (UINT64)offset * m_descriptorSize;
    return alloc;
  }

  ID3D12DescriptorHeap *Heap() const { return m_heap.Get(); }

private:
  ComPtr<ID3D12DescriptorHeap> m_heap;
  ComPtr<ID3D12Device> m_device;
  D3D12_DESCRIPTOR_HEAP_TYPE m_type;
  UINT m_numDescriptors = 0;
  UINT m_descriptorSize = 0;
  UINT m_frameCount = 0;
  std::vector<UINT> m_nextOffset;
};

struct FrameResource {
  ComPtr<ID3D12CommandAllocator> commandAllocator;
  UINT64 fenceValue = 0;
  UINT transientDescriptorOffset = 0; // per-frame descriptor offset
  std::vector<ComPtr<ID3D12Resource>>
      transientResources; // keep transient upload buffers alive per-frame
};

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
