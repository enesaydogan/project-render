#include "volumetric_renderer.h"

#include "asset_library/cooked_payload.h"
#include "asset_library/global_registry.h"
#include "asset_library/pack_mounts.h"
#include "d3d12_helpers.h"
#include "dxc_wrapper.h"
#include "scene.h"

#include <DirectXPackedVector.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace VolumetricRenderer {
namespace {

assetlib::AssetId s_activeId;
Params s_params;

// CPU dense density field (R32F), built when a volume is set; uploaded lazily.
std::vector<float> s_density;
uint32_t s_dim[3] = {0, 0, 0};
DirectX::XMFLOAT3 s_boundsMin{0, 0, 0};
DirectX::XMFLOAT3 s_boundsMax{0, 0, 0};
bool s_needsUpload = false;
bool s_uploadFailed = false;

ComPtr<ID3D12Resource> s_densityTex;
ComPtr<ID3D12Resource> s_uploadBuffer; // kept alive across frames
D3D12_RESOURCE_STATES s_densityState = D3D12_RESOURCE_STATE_COMMON;

ComPtr<ID3D12RootSignature> s_compositeRootSig;
ComPtr<ID3D12PipelineState> s_rasterCompositePSO;
ComPtr<ID3D12PipelineState> s_dxrCompositePSO;
ComPtr<ID3D12DescriptorHeap> s_compositeHeap;
DxcHelper s_dxcHelper;
uint32_t s_frameIndex = 0;

bool EnsureCompositePipeline(ID3D12Device *device) {
  if (s_compositeRootSig && s_rasterCompositePSO && s_dxrCompositePSO &&
      s_compositeHeap) {
    return true;
  }

  try {
    D3D12_DESCRIPTOR_RANGE ranges[2] = {};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 2;
    ranges[0].BaseShaderRegister = 0;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 1;
    ranges[1].BaseShaderRegister = 0;

    D3D12_ROOT_PARAMETER params[4] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[1].Constants.ShaderRegister = 1;
    params[1].Constants.Num32BitValues = 24;
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges = &ranges[0];
    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[3].DescriptorTable.NumDescriptorRanges = 1;
    params[3].DescriptorTable.pDescriptorRanges = &ranges[1];

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = _countof(params);
    rootDesc.pParameters = params;
    rootDesc.NumStaticSamplers = 1;
    rootDesc.pStaticSamplers = &sampler;

    ComPtr<ID3DBlob> rootBlob;
    ComPtr<ID3DBlob> rootError;
    ThrowIfFailed(D3D12SerializeRootSignature(
        &rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rootBlob, &rootError));
    ThrowIfFailed(device->CreateRootSignature(
        0, rootBlob->GetBufferPointer(), rootBlob->GetBufferSize(),
        IID_PPV_ARGS(&s_compositeRootSig)));

    auto CreatePipeline = [&](const wchar_t *entry,
                              ComPtr<ID3D12PipelineState> &pipeline) {
      ComPtr<IDxcBlob> shader = s_dxcHelper.Compile(
          L"shaders/volumetric_cs.hlsl", entry, L"cs_6_5", {});
      D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
      desc.pRootSignature = s_compositeRootSig.Get();
      desc.CS = {shader->GetBufferPointer(), shader->GetBufferSize()};
      ThrowIfFailed(
          device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pipeline)));
    };
    CreatePipeline(L"CSMain", s_rasterCompositePSO);
    CreatePipeline(L"CSMainDXR", s_dxrCompositePSO);

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = 3;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(
        device->CreateDescriptorHeap(&heapDesc,
                                     IID_PPV_ARGS(&s_compositeHeap)));
    return true;
  } catch (const std::exception &e) {
    fprintf(stderr, "Volumetric composite pipeline creation failed: %s\n",
            e.what());
    s_compositeRootSig.Reset();
    s_rasterCompositePSO.Reset();
    s_dxrCompositePSO.Reset();
    s_compositeHeap.Reset();
    return false;
  }
}

constexpr uint64_t kMaxRuntimeDensityBytes = 64ull * 1024ull * 1024ull;
constexpr uint64_t kMaxRuntimeVoxels =
    kMaxRuntimeDensityBytes / sizeof(DirectX::PackedVector::HALF);

std::array<uint32_t, 3>
ChooseRuntimeDimensions(const uint32_t sourceDim[3]) {
  const long double sourceVoxels =
      static_cast<long double>(sourceDim[0]) * sourceDim[1] * sourceDim[2];
  long double scale = 1.0L;
  if (sourceVoxels > static_cast<long double>(kMaxRuntimeVoxels)) {
    scale = std::cbrt(static_cast<long double>(kMaxRuntimeVoxels) /
                      sourceVoxels);
  }
  const uint32_t sourceMax =
      (std::max)({sourceDim[0], sourceDim[1], sourceDim[2]});
  if (sourceMax > D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION) {
    scale = (std::min)(
        scale, static_cast<long double>(
                   D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION) /
                   static_cast<long double>(sourceMax));
  }

  std::array<uint32_t, 3> result = {
      (std::max)(1u, static_cast<uint32_t>(std::floor(sourceDim[0] * scale))),
      (std::max)(1u, static_cast<uint32_t>(std::floor(sourceDim[1] * scale))),
      (std::max)(1u, static_cast<uint32_t>(std::floor(sourceDim[2] * scale)))};
  return result;
}

std::vector<uint32_t> SourceBinCounts(uint32_t sourceDim,
                                      uint32_t targetDim) {
  std::vector<uint32_t> counts(targetDim, 0);
  for (uint32_t source = 0; source < sourceDim; ++source) {
    const uint32_t target = static_cast<uint32_t>(
        (static_cast<uint64_t>(source) * targetDim) / sourceDim);
    ++counts[(std::min)(target, targetDim - 1)];
  }
  return counts;
}

// Dequantize sparse bricks directly into a bounded dense runtime grid. When
// reduction is required, each target voxel stores the box average of all source
// voxels it represents, including empty voxels, preserving integrated density.
bool BuildDenseField(const assetlib::CookedVolume &vol) {
  if (vol.brickSize == 0 || vol.brickSize > 64) {
    return false;
  }

  const std::array<uint32_t, 3> runtimeDim =
      ChooseRuntimeDimensions(vol.dim);
  const uint64_t runtimeVoxelCount =
      static_cast<uint64_t>(runtimeDim[0]) * runtimeDim[1] * runtimeDim[2];
  if (runtimeVoxelCount == 0 || runtimeVoxelCount > kMaxRuntimeVoxels ||
      runtimeVoxelCount >
          static_cast<uint64_t>((std::numeric_limits<size_t>::max)())) {
    return false;
  }

  std::vector<float> density;
  try {
    density.assign(static_cast<size_t>(runtimeVoxelCount), 0.0f);
  } catch (const std::bad_alloc &) {
    fprintf(stderr,
            "Volume activation failed: unable to allocate %.1f MiB CPU grid\n",
            static_cast<double>(runtimeVoxelCount * sizeof(float)) /
                (1024.0 * 1024.0));
    return false;
  }

  const std::vector<uint32_t> countX =
      SourceBinCounts(vol.dim[0], runtimeDim[0]);
  const std::vector<uint32_t> countY =
      SourceBinCounts(vol.dim[1], runtimeDim[1]);
  const std::vector<uint32_t> countZ =
      SourceBinCounts(vol.dim[2], runtimeDim[2]);
  const int B = static_cast<int>(vol.brickSize);
  const size_t expectedBrickBytes =
      static_cast<size_t>(B) * static_cast<size_t>(B) * static_cast<size_t>(B);
  for (const auto &brick : vol.bricks) {
    if (brick.data.size() != expectedBrickBytes)
      return false;
    const float range = brick.maxVal - brick.minVal;
    for (int lz = 0; lz < B; ++lz)
      for (int ly = 0; ly < B; ++ly)
        for (int lx = 0; lx < B; ++lx) {
          const uint64_t gx64 =
              static_cast<uint64_t>(brick.bx) * vol.brickSize + lx;
          const uint64_t gy64 =
              static_cast<uint64_t>(brick.by) * vol.brickSize + ly;
          const uint64_t gz64 =
              static_cast<uint64_t>(brick.bz) * vol.brickSize + lz;
          if (gx64 >= vol.dim[0] || gy64 >= vol.dim[1] ||
              gz64 >= vol.dim[2])
            continue;
          const uint32_t gx = static_cast<uint32_t>(gx64);
          const uint32_t gy = static_cast<uint32_t>(gy64);
          const uint32_t gz = static_cast<uint32_t>(gz64);
          const uint8_t q =
              brick.data[static_cast<size_t>((lz * B + ly) * B + lx)];
          const float value =
              (std::clamp)(brick.minVal + (q / 255.0f) * range, 0.0f,
                           65504.0f);
          if (value == 0.0f)
            continue;
          const uint32_t tx = static_cast<uint32_t>(
              (static_cast<uint64_t>(gx) * runtimeDim[0]) / vol.dim[0]);
          const uint32_t ty = static_cast<uint32_t>(
              (static_cast<uint64_t>(gy) * runtimeDim[1]) / vol.dim[1]);
          const uint32_t tz = static_cast<uint32_t>(
              (static_cast<uint64_t>(gz) * runtimeDim[2]) / vol.dim[2]);
          density[(static_cast<size_t>(tz) * runtimeDim[1] + ty) *
                      runtimeDim[0] +
                  tx] += value;
        }
  }

  if (runtimeDim[0] != vol.dim[0] || runtimeDim[1] != vol.dim[1] ||
      runtimeDim[2] != vol.dim[2]) {
    for (uint32_t z = 0; z < runtimeDim[2]; ++z) {
      for (uint32_t y = 0; y < runtimeDim[1]; ++y) {
        const float yzDenominator =
            static_cast<float>(countY[y]) * static_cast<float>(countZ[z]);
        for (uint32_t x = 0; x < runtimeDim[0]; ++x) {
          density[(static_cast<size_t>(z) * runtimeDim[1] + y) *
                      runtimeDim[0] +
                  x] /= yzDenominator * static_cast<float>(countX[x]);
        }
      }
    }
    fprintf(stderr,
            "Volume runtime grid reduced from %ux%ux%u to %ux%ux%u "
            "(%.1f MiB R16F)\n",
            vol.dim[0], vol.dim[1], vol.dim[2], runtimeDim[0], runtimeDim[1],
            runtimeDim[2],
            static_cast<double>(runtimeVoxelCount * sizeof(uint16_t)) /
                (1024.0 * 1024.0));
  }

  s_density = std::move(density);
  s_dim[0] = runtimeDim[0];
  s_dim[1] = runtimeDim[1];
  s_dim[2] = runtimeDim[2];
  s_boundsMin = {vol.boundsMin[0], vol.boundsMin[1], vol.boundsMin[2]};
  s_boundsMax = {vol.boundsMax[0], vol.boundsMax[1], vol.boundsMax[2]};
  return true;
}

} // namespace

bool SetActiveVolume(const assetlib::AssetId &id) {
  assetlib::AssetRegistry *reg = assetlib::GlobalRegistry();
  if (!reg)
    return false;
  std::vector<uint8_t> blob;
  if (!assetlib::ResolveCookedPayload(reg->paths(), id,
                                      assetlib::PayloadKind::Volume, blob))
    return false;
  assetlib::CookedVolume vol;
  if (!assetlib::DeserializeCookedVolume(blob.data(), blob.size(), vol))
    return false;
  if (vol.dim[0] == 0 || vol.dim[1] == 0 || vol.dim[2] == 0)
    return false;
  if (!BuildDenseField(vol))
    return false;
  s_activeId = id;
  s_needsUpload = true;
  s_uploadFailed = false;
  s_densityTex.Reset(); // force recreate at new dims
  s_uploadBuffer.Reset();
  return true;
}

void ClearActiveVolume() {
  s_activeId = {};
  s_density.clear();
  s_dim[0] = s_dim[1] = s_dim[2] = 0;
  s_needsUpload = false;
  s_uploadFailed = false;
  s_densityTex.Reset();
  s_uploadBuffer.Reset();
}

bool HasActiveVolume() { return s_activeId.valid() && !s_density.empty(); }
const assetlib::AssetId &ActiveVolumeId() { return s_activeId; }
Params &GetParams() { return s_params; }
ID3D12Resource *GetDensityTexture() { return s_densityTex.Get(); }
DirectX::XMFLOAT3 BoundsMin() { return s_boundsMin; }
DirectX::XMFLOAT3 BoundsMax() { return s_boundsMax; }

bool EnsureUploaded(ID3D12Device *device, ID3D12GraphicsCommandList *cmdList) {
  if (!HasActiveVolume())
    return false;
  if (!s_needsUpload && s_densityTex)
    return true;
  if (s_uploadFailed)
    return false;

  // Create the 3D texture.
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
  desc.Width = s_dim[0];
  desc.Height = s_dim[1];
  desc.DepthOrArraySize = static_cast<UINT16>(s_dim[2]);
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_R16_FLOAT;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  D3D12_HEAP_PROPERTIES defaultProp = {D3D12_HEAP_TYPE_DEFAULT};
  HRESULT hr = device->CreateCommittedResource(
      &defaultProp, D3D12_HEAP_FLAG_NONE, &desc,
      D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&s_densityTex));
  if (FAILED(hr)) {
    fprintf(stderr,
            "Volume texture allocation failed for %ux%ux%u R16F "
            "(HRESULT 0x%08X)\n",
            s_dim[0], s_dim[1], s_dim[2], static_cast<unsigned>(hr));
    s_uploadFailed = true;
    s_needsUpload = false;
    return false;
  }
  s_densityState = D3D12_RESOURCE_STATE_COPY_DEST;

  // Upload buffer sized from the copyable footprint (256-aligned row pitch).
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
  UINT numRows = 0;
  UINT64 rowSizeBytes = 0, totalBytes = 0;
  device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &numRows,
                                &rowSizeBytes, &totalBytes);
  D3D12_HEAP_PROPERTIES uploadProp = {D3D12_HEAP_TYPE_UPLOAD};
  D3D12_RESOURCE_DESC bufDesc = {};
  bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  bufDesc.Width = totalBytes;
  bufDesc.Height = 1;
  bufDesc.DepthOrArraySize = 1;
  bufDesc.MipLevels = 1;
  bufDesc.Format = DXGI_FORMAT_UNKNOWN;
  bufDesc.SampleDesc.Count = 1;
  bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  hr = device->CreateCommittedResource(
      &uploadProp, D3D12_HEAP_FLAG_NONE, &bufDesc,
      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
      IID_PPV_ARGS(&s_uploadBuffer));
  if (FAILED(hr)) {
    fprintf(stderr,
            "Volume upload allocation failed for %llu bytes "
            "(HRESULT 0x%08X)\n",
            static_cast<unsigned long long>(totalBytes),
            static_cast<unsigned>(hr));
    s_densityTex.Reset();
    s_uploadFailed = true;
    s_needsUpload = false;
    return false;
  }

  // Copy dense data into the (row-padded) upload buffer.
  uint8_t *mapped = nullptr;
  D3D12_RANGE noRead = {0, 0};
  hr = s_uploadBuffer->Map(0, &noRead, reinterpret_cast<void **>(&mapped));
  if (FAILED(hr)) {
    fprintf(stderr, "Volume upload map failed (HRESULT 0x%08X)\n",
            static_cast<unsigned>(hr));
    s_densityTex.Reset();
    s_uploadBuffer.Reset();
    s_uploadFailed = true;
    s_needsUpload = false;
    return false;
  }
  const UINT64 dstRowPitch = footprint.Footprint.RowPitch;
  const UINT64 dstSlicePitch = dstRowPitch * numRows;
  for (uint32_t z = 0; z < s_dim[2]; ++z) {
    for (uint32_t y = 0; y < s_dim[1]; ++y) {
      const float *src =
          &s_density[(static_cast<size_t>(z) * s_dim[1] + y) * s_dim[0]];
      auto *dst = reinterpret_cast<DirectX::PackedVector::HALF *>(
          mapped + footprint.Offset + z * dstSlicePitch +
          static_cast<UINT64>(y) * dstRowPitch);
      DirectX::PackedVector::XMConvertFloatToHalfStream(
          dst, sizeof(DirectX::PackedVector::HALF), src, sizeof(float),
          s_dim[0]);
    }
  }
  s_uploadBuffer->Unmap(0, nullptr);

  D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
  dstLoc.pResource = s_densityTex.Get();
  dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  dstLoc.SubresourceIndex = 0;
  D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
  srcLoc.pResource = s_uploadBuffer.Get();
  srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  srcLoc.PlacedFootprint = footprint;
  cmdList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = s_densityTex.Get();
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  cmdList->ResourceBarrier(1, &barrier);
  s_densityState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  s_needsUpload = false;
  return true;
}

bool Composite(ID3D12Device *device, ID3D12GraphicsCommandList *cmdList,
               ID3D12Resource *cameraCB, ID3D12Resource *colorTarget,
               ID3D12Resource *depthTexture, UINT width, UINT height,
               DepthEncoding depthEncoding) {
  if (!device || !cmdList || !cameraCB || !colorTarget || !depthTexture ||
      width == 0 || height == 0 || !HasActiveVolume()) {
    return false;
  }
  if (!EnsureUploaded(device, cmdList) || !EnsureCompositePipeline(device)) {
    return false;
  }

  float transform[16];
  if (!Scene::FindVolumeNodeTransform(ActiveVolumeId(), transform)) {
    return false;
  }

  const DirectX::XMMATRIX localToWorld = DirectX::XMLoadFloat4x4(
      reinterpret_cast<const DirectX::XMFLOAT4X4 *>(transform));
  DirectX::XMVECTOR determinant = DirectX::XMMatrixDeterminant(localToWorld);
  if (std::abs(DirectX::XMVectorGetX(determinant)) < 1.0e-8f) {
    return false;
  }
  const DirectX::XMMATRIX worldToLocal =
      DirectX::XMMatrixInverse(&determinant, localToWorld);
  DirectX::XMFLOAT4X4 worldToLocalData;
  DirectX::XMStoreFloat4x4(&worldToLocalData, worldToLocal);

  const Params &params = GetParams();
  struct CompositeConstants {
    float worldToLocal[16];
    float densityScale;
    float absorption;
    float scatterG;
    float ambient;
    float stepJitter;
    uint32_t marchSteps;
    float frameSeed;
    uint32_t lightSteps;
  } constants = {};
  std::memcpy(constants.worldToLocal, &worldToLocalData,
              sizeof(constants.worldToLocal));
  constants.densityScale = params.densityScale;
  constants.absorption = params.absorption;
  constants.scatterG = params.scatter;
  constants.ambient = params.ambient;
  constants.stepJitter = params.stepJitter;
  constants.marchSteps =
      static_cast<uint32_t>((std::max)(1, params.marchSteps));
  constants.frameSeed = static_cast<float>(s_frameIndex++ & 1023u);
  constants.lightSteps =
      static_cast<uint32_t>((std::clamp)(params.lightSteps, 1, 32));

  const UINT descriptorSize = device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  D3D12_CPU_DESCRIPTOR_HANDLE cpu =
      s_compositeHeap->GetCPUDescriptorHandleForHeapStart();

  D3D12_SHADER_RESOURCE_VIEW_DESC densitySrv = {};
  densitySrv.Format = DXGI_FORMAT_R16_FLOAT;
  densitySrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
  densitySrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  densitySrv.Texture3D.MipLevels = 1;
  device->CreateShaderResourceView(s_densityTex.Get(), &densitySrv, cpu);
  cpu.ptr += descriptorSize;

  D3D12_SHADER_RESOURCE_VIEW_DESC depthSrv = {};
  depthSrv.Format = DXGI_FORMAT_R32_FLOAT;
  depthSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  depthSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  depthSrv.Texture2D.MipLevels = 1;
  device->CreateShaderResourceView(depthTexture, &depthSrv, cpu);
  cpu.ptr += descriptorSize;

  D3D12_UNORDERED_ACCESS_VIEW_DESC colorUav = {};
  colorUav.Format = colorTarget->GetDesc().Format;
  colorUav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  device->CreateUnorderedAccessView(colorTarget, nullptr, &colorUav, cpu);

  ID3D12DescriptorHeap *heaps[] = {s_compositeHeap.Get()};
  cmdList->SetDescriptorHeaps(_countof(heaps), heaps);
  cmdList->SetPipelineState(
      depthEncoding == DepthEncoding::LinearViewDepth
          ? s_dxrCompositePSO.Get()
          : s_rasterCompositePSO.Get());
  cmdList->SetComputeRootSignature(s_compositeRootSig.Get());
  cmdList->SetComputeRootConstantBufferView(
      0, cameraCB->GetGPUVirtualAddress());
  cmdList->SetComputeRoot32BitConstants(
      1, 24, &constants, 0);

  D3D12_GPU_DESCRIPTOR_HANDLE gpu =
      s_compositeHeap->GetGPUDescriptorHandleForHeapStart();
  cmdList->SetComputeRootDescriptorTable(2, gpu);
  gpu.ptr += 2ull * descriptorSize;
  cmdList->SetComputeRootDescriptorTable(3, gpu);
  cmdList->Dispatch((width + 7) / 8, (height + 7) / 8, 1);

  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  barrier.UAV.pResource = colorTarget;
  cmdList->ResourceBarrier(1, &barrier);
  return true;
}

} // namespace VolumetricRenderer
