#pragma once

#include <cstdio>
#include <d3d12.h>
#include <iostream>
#include <vector>
#include <windows.h>
#include <wrl.h>

using Microsoft::WRL::ComPtr;

#define ThrowIfFailed(hr) ThrowIfFailedEx(hr, __FILE__, __LINE__)

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
    m_persistentOffset = 0;
  }

  void ResetFrame(UINT frameIndex) {
    if (frameIndex < m_nextOffset.size())
      m_nextOffset[frameIndex] = m_persistentOffset;
  }

  // Allocate descriptors that persist across frames
  DescriptorAllocation AllocatePersistent(UINT count) {
    DescriptorAllocation alloc = {};
    if (m_persistentOffset + count > m_numDescriptors) {
      ThrowIfFailed(
          E_OUTOFMEMORY); // Actually probably should use a real HRESULT
    }
    UINT offset = m_persistentOffset;
    m_persistentOffset += count;

    // Push all transient starts forward
    for (UINT i = 0; i < (UINT)m_nextOffset.size(); ++i) {
      if (m_nextOffset[i] < m_persistentOffset)
        m_nextOffset[i] = m_persistentOffset;
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
  UINT m_persistentOffset = 0;
  std::vector<UINT> m_nextOffset;
};

struct FrameResource {
  ComPtr<ID3D12CommandAllocator> commandAllocator;
  UINT64 fenceValue = 0;
  UINT transientDescriptorOffset = 0; // per-frame descriptor offset
  std::vector<ComPtr<ID3D12Resource>>
      transientResources; // keep transient upload buffers alive per-frame
};

// --- D3DX12 Replacements for missing headeers ---

inline UINT64 GetRequiredIntermediateSize(ID3D12Resource *pDestinationResource,
                                          UINT FirstSubresource,
                                          UINT NumSubresources) {
  D3D12_RESOURCE_DESC Desc = pDestinationResource->GetDesc();
  UINT64 RequiredSize = 0;
  ID3D12Device *pDevice;
  pDestinationResource->GetDevice(IID_PPV_ARGS(&pDevice));
  pDevice->GetCopyableFootprints(&Desc, FirstSubresource, NumSubresources, 0,
                                 nullptr, nullptr, nullptr, &RequiredSize);
  pDevice->Release();
  return RequiredSize;
}

inline void UpdateSubresources(ID3D12GraphicsCommandList *pCmdList,
                               ID3D12Resource *pDestinationResource,
                               ID3D12Resource *pIntermediate,
                               UINT64 IntermediateOffset, UINT FirstSubresource,
                               UINT NumSubresources,
                               D3D12_SUBRESOURCE_DATA *pSrcData) {
  D3D12_RESOURCE_DESC Desc = pDestinationResource->GetDesc();
  ID3D12Device *pDevice;
  pDestinationResource->GetDevice(IID_PPV_ARGS(&pDevice));

  std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> Layouts(NumSubresources);
  std::vector<UINT> NumRows(NumSubresources);
  std::vector<UINT64> RowSizes(NumSubresources);
  UINT64 RequiredSize = 0;

  pDevice->GetCopyableFootprints(
      &Desc, FirstSubresource, NumSubresources, IntermediateOffset,
      Layouts.data(), NumRows.data(), RowSizes.data(), &RequiredSize);
  pDevice->Release();

  BYTE *pData;
  HRESULT hr =
      pIntermediate->Map(0, nullptr, reinterpret_cast<void **>(&pData));
  if (FAILED(hr))
    return;

  for (UINT i = 0; i < NumSubresources; ++i) {
    D3D12_SUBRESOURCE_DATA src = pSrcData[i];
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout = Layouts[i];

    for (UINT z = 0; z < layout.Footprint.Depth; ++z) {
      BYTE *pDestSliceStart =
          pData + layout.Offset +
          (z * layout.Footprint.Height * layout.Footprint.RowPitch);
      const BYTE *pSrcSliceStart =
          reinterpret_cast<const BYTE *>(src.pData) + (z * src.SlicePitch);

      for (UINT y = 0; y < NumRows[i]; ++y) {
        BYTE *pDestRow = pDestSliceStart + (y * layout.Footprint.RowPitch);
        const BYTE *pSrcRow = pSrcSliceStart + (y * src.RowPitch);
        memcpy(pDestRow, pSrcRow, RowSizes[i]);
      }
    }
  }
  pIntermediate->Unmap(0, nullptr);

  for (UINT i = 0; i < NumSubresources; ++i) {
    D3D12_TEXTURE_COPY_LOCATION Dst = {};
    Dst.pResource = pDestinationResource;
    Dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    Dst.SubresourceIndex = FirstSubresource + i;

    D3D12_TEXTURE_COPY_LOCATION Src = {};
    Src.pResource = pIntermediate;
    Src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    Src.PlacedFootprint = Layouts[i];

    pCmdList->CopyTextureRegion(&Dst, 0, 0, 0, &Src, nullptr);
  }
}
