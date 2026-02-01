#pragma once

#include "d3d12_helpers.h"
#include <algorithm>
#include <d3d12.h>
#include <stdexcept>
#include <vector>
#include <wrl.h>

// D3D12 Raytracing Fallback - ensure struct definitions exist if using older
// SDK, though we target Windows 10 RS5+ (10.0.17763.0) SDK ideally. Assuming
// modern SDK is present.

using Microsoft::WRL::ComPtr;

struct AccelerationStructureBuffers {
  ComPtr<ID3D12Resource> scratch;
  ComPtr<ID3D12Resource> result;
  ComPtr<ID3D12Resource> instanceDesc; // Used only for TLAS
  UINT64 resultSizeInBytes = 0;
  UINT64 scratchSizeInBytes = 0;
  UINT64 instanceDescSizeInBytes = 0;
};

// Start Alignment at 256 bytes for good measure
inline UINT64 Align(UINT64 size, UINT64 alignment) {
  return (size + (alignment - 1)) & ~(alignment - 1);
}

inline void AllocateUAVBuffer(
    ID3D12Device *device, UINT64 bufferSize, ID3D12Resource **ppResource,
    D3D12_RESOURCE_STATES initialResourceState = D3D12_RESOURCE_STATE_COMMON,
    const wchar_t *resourceName = nullptr) {
  D3D12_HEAP_PROPERTIES heapProps = {};
  heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

  D3D12_RESOURCE_DESC bufferDesc = {};
  bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  bufferDesc.Width = bufferSize;
  bufferDesc.Height = 1;
  bufferDesc.DepthOrArraySize = 1;
  bufferDesc.MipLevels = 1;
  bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
  bufferDesc.SampleDesc.Count = 1;
  bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  bufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

  if (FAILED(device->CreateCommittedResource(
          &heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc, initialResourceState,
          nullptr, IID_PPV_ARGS(ppResource)))) {
    // Handle error normally, but here we assume caller handles exceptions or we
    // crash
    throw std::runtime_error("Failed to allocate UAV buffer");
  }

  if (resourceName && *ppResource) {
    (*ppResource)->SetName(resourceName);
  }
}

inline void AllocateUploadBuffer(ID3D12Device *device, void *pData,
                                 UINT64 datasize, ID3D12Resource **ppResource,
                                 const wchar_t *resourceName = nullptr) {
  D3D12_HEAP_PROPERTIES heapProps = {};
  heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

  D3D12_RESOURCE_DESC bufferDesc = {};
  bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  bufferDesc.Width = datasize;
  bufferDesc.Height = 1;
  bufferDesc.DepthOrArraySize = 1;
  bufferDesc.MipLevels = 1;
  bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
  bufferDesc.SampleDesc.Count = 1;
  bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

  if (FAILED(device->CreateCommittedResource(
          &heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
          D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
          IID_PPV_ARGS(ppResource)))) {
    throw std::runtime_error("Failed to allocate upload buffer");
  }

  if (resourceName && *ppResource) {
    (*ppResource)->SetName(resourceName);
  }

  if (pData) {
    void *pMappedData;
    (*ppResource)->Map(0, nullptr, &pMappedData);
    memcpy(pMappedData, pData, datasize);
    (*ppResource)->Unmap(0, nullptr);
  } else {
    // No initial data provided; leave the upload buffer unmapped until caller
    // writes to it explicitly. This avoids reading from a null source pointer.
  }
}

// Helper to build a BLAS from a list of vertex/index buffers
inline AccelerationStructureBuffers
BuildBLAS(ID3D12Device5 *device, ID3D12GraphicsCommandList4 *commandList,
          D3D12_GPU_VIRTUAL_ADDRESS vbPtr, UINT vertexCount, UINT vertexStride,
          D3D12_GPU_VIRTUAL_ADDRESS ibPtr, UINT indexCount,
          bool isOpaque = true) {
  D3D12_RAYTRACING_GEOMETRY_DESC geomDesc = {};
  geomDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
  geomDesc.Triangles.VertexBuffer.StartAddress = vbPtr;
  geomDesc.Triangles.VertexBuffer.StrideInBytes = vertexStride;
  geomDesc.Triangles.VertexCount = vertexCount;
  geomDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
  geomDesc.Triangles.IndexBuffer = ibPtr;
  geomDesc.Triangles.IndexCount = indexCount;
  geomDesc.Triangles.IndexFormat =
      DXGI_FORMAT_R32_UINT; // Assuming 32-bit indices from GLTF usually
  geomDesc.Triangles.Transform3x4 = 0;
  geomDesc.Flags = isOpaque ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE
                            : D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;

  // Get prebuild info
  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
  inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
  inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  // Use PREFER_FAST_BUILD for faster load times.
  // PREFER_FAST_TRACE is better for runtime performance but much slower to build.
  // For models taking "ages" to load, FAST_BUILD is the right trade-off.
  inputs.Flags =
      D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE; //PREFER_FAST_BUILD; but testing trace for now
  inputs.NumDescs = 1;
  inputs.pGeometryDescs = &geomDesc;

  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
  device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

  AccelerationStructureBuffers buffers;
  buffers.scratchSizeInBytes =
      Align(info.ScratchDataSizeInBytes,
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
  buffers.resultSizeInBytes =
      Align(info.ResultDataMaxSizeInBytes,
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);

  AllocateUAVBuffer(device, buffers.scratchSizeInBytes, &buffers.scratch,
                    D3D12_RESOURCE_STATE_COMMON, L"BLAS Scratch");
  AllocateUAVBuffer(device, buffers.resultSizeInBytes, &buffers.result,
                    D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                    L"BLAS Result");

  // Build
  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
  buildDesc.Inputs = inputs;
  buildDesc.DestAccelerationStructureData =
      buffers.result->GetGPUVirtualAddress();
  buildDesc.ScratchAccelerationStructureData =
      buffers.scratch->GetGPUVirtualAddress();

  commandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

  // UAV barrier
  D3D12_RESOURCE_BARRIER uavBarrier = {};
  uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  uavBarrier.UAV.pResource = buffers.result.Get();
  commandList->ResourceBarrier(1, &uavBarrier);

  return buffers;
}

// Simple TLAS builder assuming 1 instance pointing to BLAS
// In a real engine, we'd pass a list of instances
inline AccelerationStructureBuffers
BuildTLAS(ID3D12Device5 *device, ID3D12GraphicsCommandList4 *commandList,
          D3D12_GPU_VIRTUAL_ADDRESS blasAddress, UINT instanceCount,
          UINT rayContributionToHitGroupIndex = 0) {
  // Create Instance Desc
  D3D12_RAYTRACING_INSTANCE_DESC instanceDesc = {};
  instanceDesc.Transform[0][0] = instanceDesc.Transform[1][1] =
      instanceDesc.Transform[2][2] = 1; // Identity
  instanceDesc.InstanceMask = 1;
  instanceDesc.InstanceID = 0;
  instanceDesc.InstanceContributionToHitGroupIndex =
      rayContributionToHitGroupIndex;
  instanceDesc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
  instanceDesc.AccelerationStructure = blasAddress;

  AccelerationStructureBuffers buffers;
  buffers.instanceDescSizeInBytes = Align(
      sizeof(instanceDesc), D3D12_RAYTRACING_INSTANCE_DESCS_BYTE_ALIGNMENT);
  AllocateUploadBuffer(device, &instanceDesc, buffers.instanceDescSizeInBytes,
                       &buffers.instanceDesc, L"TLAS Instance Desc");

  // Get prebuild info
  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
  inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
  inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
  inputs.Flags =
      D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
  inputs.NumDescs = instanceCount;
  inputs.InstanceDescs = buffers.instanceDesc->GetGPUVirtualAddress();

  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info = {};
  device->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

  buffers.scratchSizeInBytes =
      Align(info.ScratchDataSizeInBytes,
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
  buffers.resultSizeInBytes =
      Align(info.ResultDataMaxSizeInBytes,
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);

  AllocateUAVBuffer(device, buffers.scratchSizeInBytes, &buffers.scratch,
                    D3D12_RESOURCE_STATE_COMMON, L"TLAS Scratch");
  AllocateUAVBuffer(device, buffers.resultSizeInBytes, &buffers.result,
                    D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                    L"TLAS Result");

  // Build
  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
  buildDesc.Inputs = inputs;
  buildDesc.DestAccelerationStructureData =
      buffers.result->GetGPUVirtualAddress();
  buildDesc.ScratchAccelerationStructureData =
      buffers.scratch->GetGPUVirtualAddress();

  commandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

  // UAV barrier
  D3D12_RESOURCE_BARRIER uavBarrier = {};
  uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  uavBarrier.UAV.pResource = buffers.result.Get();
  commandList->ResourceBarrier(1, &uavBarrier);

  return buffers;
}
