#include "volumetric_renderer.h"

#include "asset_library/cooked_payload.h"
#include "asset_library/asset_cooker.h"
#include "asset_library/global_registry.h"
#include "asset_library/import_hook.h"
#include "asset_library/pack_mounts.h"
#include "camera.h"
#include "d3d12_helpers.h"
#include "dxc_wrapper.h"
#include "scene.h"

#include <DirectXPackedVector.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <future>
#include <limits>
#include <new>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

using Microsoft::WRL::ComPtr;
using json = nlohmann::json;

namespace VolumetricRenderer {
namespace {

Params s_params;
EmissionLightStats s_emissionLightStats;

struct HeatVoxel {
  uint32_t x = 0;
  uint32_t y = 0;
  uint32_t z = 0;
  float value = 0.0f;
};

struct CpuVolumeFrame {
  std::vector<float> density;
  std::vector<float> temperature;
  std::vector<HeatVoxel> heatVoxels;
  float densityMax = 0.0f;
  uint32_t dim[3] = {0, 0, 0};
  float temperatureMin = 0.0f;
  float temperatureInvRange = 0.0f;
  DirectX::XMFLOAT3 boundsMin{0, 0, 0};
  DirectX::XMFLOAT3 boundsMax{0, 0, 0};
};

struct RetiredVolumeGpuFrame {
  ComPtr<ID3D12Resource> texture;
  ComPtr<ID3D12Resource> uploadBuffer;
  ComPtr<ID3D12DescriptorHeap> descriptorHeap;
  uint32_t age = 0;
};

struct RuntimeVolume {
  assetlib::AssetId id;
  std::vector<float> density;
  std::vector<float> temperature;
  std::vector<HeatVoxel> heatVoxels;
  float densityMax = 0.0f;
  uint32_t dim[3] = {0, 0, 0};
  float temperatureMin = 0.0f;
  float temperatureInvRange = 0.0f;
  DirectX::XMFLOAT3 boundsMin{0, 0, 0};
  DirectX::XMFLOAT3 boundsMax{0, 0, 0};
  bool needsUpload = false;
  bool uploadFailed = false;
  ComPtr<ID3D12Resource> texture;
  ComPtr<ID3D12Resource> uploadBuffer;
  ComPtr<ID3D12DescriptorHeap> descriptorHeap;
  std::vector<RetiredVolumeGpuFrame> retiredGpuFrames;
  bool sequence = false;
  uint32_t sequenceFrameCount = 1;
  float sequenceFps = 30.0f;
  uint32_t currentFrame = 0;
  uint32_t requestedFrame = 0;
  uint32_t pendingFrame = 0;
  std::future<std::pair<bool, CpuVolumeFrame>> pendingLoad;
};

std::unordered_map<std::string, RuntimeVolume> s_volumes;
assetlib::AssetId s_lastResolvedId;

ComPtr<ID3D12RootSignature> s_compositeRootSig;
ComPtr<ID3D12PipelineState> s_rasterCompositePSO;
ComPtr<ID3D12PipelineState> s_dxrCompositePSO;
DxcHelper s_dxcHelper;
uint32_t s_frameIndex = 0;

DirectX::XMFLOAT3 FireColor(float normalizedHeat) {
  const float k =
      (1000.0f +
       11000.0f * (std::clamp)(normalizedHeat, 0.0f, 1.0f)) /
      100.0f;
  DirectX::XMFLOAT3 rgb;
  rgb.x = k <= 66.0f
              ? 1.0f
              : (std::clamp)(1.2929362f * std::pow(k - 60.0f, -0.13320476f),
                             0.0f, 1.0f);
  rgb.y = k <= 66.0f
              ? (std::clamp)(0.39008158f * std::log((std::max)(k, 1.0f)) -
                                 0.63184144f,
                             0.0f, 1.0f)
              : (std::clamp)(1.1298909f * std::pow(k - 60.0f, -0.07551485f),
                             0.0f, 1.0f);
  rgb.z =
      k >= 66.0f
          ? 1.0f
          : (k <= 19.0f
                 ? 0.0f
                 : (std::clamp)(0.5432068f * std::log(k - 10.0f) -
                                    1.1962541f,
                                0.0f, 1.0f));
  return rgb;
}

bool EnsureCompositePipeline(ID3D12Device *device) {
  if (s_compositeRootSig && s_rasterCompositePSO && s_dxrCompositePSO) {
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
    params[1].Constants.Num32BitValues = 36;
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

    return true;
  } catch (const std::exception &e) {
    fprintf(stderr, "Volumetric composite pipeline creation failed: %s\n",
            e.what());
    s_compositeRootSig.Reset();
    s_rasterCompositePSO.Reset();
    s_dxrCompositePSO.Reset();
    return false;
  }
}

constexpr uint64_t kMaxRuntimeDensityBytes = 64ull * 1024ull * 1024ull;
constexpr uint64_t kMaxRuntimeVoxels =
    kMaxRuntimeDensityBytes / (2ull * sizeof(DirectX::PackedVector::HALF));

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
bool BuildDenseField(const assetlib::CookedVolume &vol, RuntimeVolume &runtime) {
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
            "(%.1f MiB R16G16F)\n",
            vol.dim[0], vol.dim[1], vol.dim[2], runtimeDim[0], runtimeDim[1],
            runtimeDim[2],
            static_cast<double>(runtimeVoxelCount * 2 * sizeof(uint16_t)) /
                (1024.0 * 1024.0));
  }

  std::vector<float> temperature;
  if (!vol.temperatureBricks.empty()) {
    try {
      temperature.assign(static_cast<size_t>(runtimeVoxelCount), 0.0f);
    } catch (const std::bad_alloc &) {
      return false;
    }
    for (const auto &brick : vol.temperatureBricks) {
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
                (std::max)(0.0f, brick.minVal + (q / 255.0f) * range);
            if (value == 0.0f)
              continue;
            const uint32_t tx = static_cast<uint32_t>(
                (static_cast<uint64_t>(gx) * runtimeDim[0]) / vol.dim[0]);
            const uint32_t ty = static_cast<uint32_t>(
                (static_cast<uint64_t>(gy) * runtimeDim[1]) / vol.dim[1]);
            const uint32_t tz = static_cast<uint32_t>(
                (static_cast<uint64_t>(gz) * runtimeDim[2]) / vol.dim[2]);
            temperature[(static_cast<size_t>(tz) * runtimeDim[1] + ty) *
                            runtimeDim[0] +
                        tx] += value;
          }
    }
    if (runtimeDim[0] != vol.dim[0] || runtimeDim[1] != vol.dim[1] ||
        runtimeDim[2] != vol.dim[2]) {
      for (uint32_t z = 0; z < runtimeDim[2]; ++z)
        for (uint32_t y = 0; y < runtimeDim[1]; ++y)
          for (uint32_t x = 0; x < runtimeDim[0]; ++x)
            temperature[(static_cast<size_t>(z) * runtimeDim[1] + y) *
                            runtimeDim[0] +
                        x] /=
                static_cast<float>(countX[x]) *
                static_cast<float>(countY[y]) *
                static_cast<float>(countZ[z]);
    }
  }

  runtime.density = std::move(density);
  runtime.densityMax = 0.0f;
  for (const float value : runtime.density)
    runtime.densityMax = (std::max)(runtime.densityMax, value);
  runtime.temperature = std::move(temperature);
  runtime.heatVoxels.clear();
  if (!runtime.temperature.empty()) {
    runtime.heatVoxels.reserve(vol.activeVoxels);
    for (uint32_t z = 0; z < runtimeDim[2]; ++z) {
      for (uint32_t y = 0; y < runtimeDim[1]; ++y) {
        for (uint32_t x = 0; x < runtimeDim[0]; ++x) {
          const float value =
              runtime.temperature[(static_cast<size_t>(z) * runtimeDim[1] + y) *
                                      runtimeDim[0] +
                                  x];
          if (value > 0.0f)
            runtime.heatVoxels.push_back({x, y, z, value});
        }
      }
    }
  }
  runtime.temperatureMin = vol.temperatureMin;
  const float temperatureRange = vol.temperatureMax - vol.temperatureMin;
  runtime.temperatureInvRange =
      temperatureRange > 1.0e-6f ? 1.0f / temperatureRange : 0.0f;
  runtime.dim[0] = runtimeDim[0];
  runtime.dim[1] = runtimeDim[1];
  runtime.dim[2] = runtimeDim[2];
  runtime.boundsMin = {vol.boundsMin[0], vol.boundsMin[1], vol.boundsMin[2]};
  runtime.boundsMax = {vol.boundsMax[0], vol.boundsMax[1], vol.boundsMax[2]};
  return true;
}

CpuVolumeFrame TakeCpuFrame(RuntimeVolume &runtime) {
  CpuVolumeFrame frame;
  frame.density = std::move(runtime.density);
  frame.temperature = std::move(runtime.temperature);
  frame.heatVoxels = std::move(runtime.heatVoxels);
  frame.densityMax = runtime.densityMax;
  std::copy(std::begin(runtime.dim), std::end(runtime.dim),
            std::begin(frame.dim));
  frame.temperatureMin = runtime.temperatureMin;
  frame.temperatureInvRange = runtime.temperatureInvRange;
  frame.boundsMin = runtime.boundsMin;
  frame.boundsMax = runtime.boundsMax;
  return frame;
}

void ApplyCpuFrame(RuntimeVolume &runtime, CpuVolumeFrame frame,
                   uint32_t frameIndex) {
  runtime.density = std::move(frame.density);
  runtime.temperature = std::move(frame.temperature);
  runtime.heatVoxels = std::move(frame.heatVoxels);
  runtime.densityMax = frame.densityMax;
  std::copy(std::begin(frame.dim), std::end(frame.dim),
            std::begin(runtime.dim));
  runtime.temperatureMin = frame.temperatureMin;
  runtime.temperatureInvRange = frame.temperatureInvRange;
  runtime.boundsMin = frame.boundsMin;
  runtime.boundsMax = frame.boundsMax;
  runtime.currentFrame = frameIndex;
  if (runtime.texture || runtime.uploadBuffer || runtime.descriptorHeap) {
    RetiredVolumeGpuFrame retired;
    retired.texture = std::move(runtime.texture);
    retired.uploadBuffer = std::move(runtime.uploadBuffer);
    retired.descriptorHeap = std::move(runtime.descriptorHeap);
    runtime.retiredGpuFrames.push_back(std::move(retired));
  }
  runtime.needsUpload = true;
  runtime.uploadFailed = false;
}

RuntimeVolume *ResolveVolume(const assetlib::AssetId &id) {
  if (!id.valid())
    return nullptr;
  const std::string key = id.ToString();
  if (auto it = s_volumes.find(key); it != s_volumes.end())
    return &it->second;

  assetlib::AssetRegistry *reg = assetlib::GlobalRegistry();
  std::vector<uint8_t> blob;
  if (reg) {
    if (!assetlib::EnsureVdbSequenceCooked(id))
      return nullptr;
    if (!assetlib::HasCurrentCookedVolume(*reg, reg->paths(), id))
      assetlib::RecookVolumeFromSource(*reg, reg->paths(), id);
    assetlib::ResolveCookedPayload(reg->paths(), id,
                                   assetlib::PayloadKind::Volume, blob);
  }
  if (blob.empty()) {
    const std::string idString = id.ToString();
    for (const Scene::Node &node : Scene::GetNodes()) {
      if (node.volumeAssetId == idString && !node.volumePayload.empty()) {
        blob = node.volumePayload;
        break;
      }
    }
  }
  if (blob.empty())
    return nullptr;
  assetlib::CookedVolume cooked;
  if (!assetlib::DeserializeCookedVolume(blob.data(), blob.size(), cooked) ||
      cooked.dim[0] == 0 || cooked.dim[1] == 0 || cooked.dim[2] == 0)
    return nullptr;

  RuntimeVolume runtime;
  runtime.id = id;
  if (!BuildDenseField(cooked, runtime))
    return nullptr;
  if (reg) {
    if (const assetlib::AssetMetadata *metadata = reg->Get(id)) {
      try {
        const json settings = json::parse(metadata->importSettingsJson);
        if (settings.contains("sequence") &&
            settings["sequence"].is_object()) {
          const json &sequence = settings["sequence"];
          runtime.sequenceFrameCount =
              sequence.value("frameCount", 1u);
          runtime.sequence = runtime.sequenceFrameCount > 1;
          runtime.sequenceFps = sequence.value("fps", 24.0f);
        }
      } catch (...) {
      }
    }
  }
  runtime.needsUpload = true;
  auto [it, inserted] = s_volumes.emplace(key, std::move(runtime));
  return inserted ? &it->second : nullptr;
}

bool EnsureUploaded(RuntimeVolume &volume, ID3D12Device *device,
                    ID3D12GraphicsCommandList *cmdList) {
  if (volume.density.empty())
    return false;
  if (!volume.needsUpload && volume.texture)
    return true;
  if (volume.uploadFailed)
    return false;

  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
  desc.Width = volume.dim[0];
  desc.Height = volume.dim[1];
  desc.DepthOrArraySize = static_cast<UINT16>(volume.dim[2]);
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_R16G16_FLOAT;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  D3D12_HEAP_PROPERTIES defaultProp = {D3D12_HEAP_TYPE_DEFAULT};
  HRESULT hr = device->CreateCommittedResource(
      &defaultProp, D3D12_HEAP_FLAG_NONE, &desc,
      D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&volume.texture));
  if (FAILED(hr)) {
    fprintf(stderr,
            "Volume texture allocation failed for %ux%ux%u R16G16F "
            "(HRESULT 0x%08X)\n",
            volume.dim[0], volume.dim[1], volume.dim[2],
            static_cast<unsigned>(hr));
    volume.uploadFailed = true;
    volume.needsUpload = false;
    return false;
  }

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
      IID_PPV_ARGS(&volume.uploadBuffer));
  if (FAILED(hr)) {
    volume.texture.Reset();
    volume.uploadFailed = true;
    volume.needsUpload = false;
    return false;
  }

  uint8_t *mapped = nullptr;
  D3D12_RANGE noRead = {0, 0};
  hr = volume.uploadBuffer->Map(0, &noRead,
                                reinterpret_cast<void **>(&mapped));
  if (FAILED(hr)) {
    volume.texture.Reset();
    volume.uploadBuffer.Reset();
    volume.uploadFailed = true;
    volume.needsUpload = false;
    return false;
  }
  const UINT64 dstRowPitch = footprint.Footprint.RowPitch;
  const UINT64 dstSlicePitch = dstRowPitch * numRows;
  for (uint32_t z = 0; z < volume.dim[2]; ++z) {
    for (uint32_t y = 0; y < volume.dim[1]; ++y) {
      const float *src =
          &volume.density[(static_cast<size_t>(z) * volume.dim[1] + y) *
                          volume.dim[0]];
      auto *dst = reinterpret_cast<DirectX::PackedVector::HALF *>(
          mapped + footprint.Offset + z * dstSlicePitch +
          static_cast<UINT64>(y) * dstRowPitch);
      DirectX::PackedVector::XMConvertFloatToHalfStream(
          dst, 2 * sizeof(DirectX::PackedVector::HALF), src, sizeof(float),
          volume.dim[0]);
      if (volume.temperature.empty()) {
        for (uint32_t x = 0; x < volume.dim[0]; ++x)
          dst[x * 2 + 1] = 0;
      } else {
        const float *temperatureSrc =
            &volume.temperature[(static_cast<size_t>(z) * volume.dim[1] + y) *
                                volume.dim[0]];
        DirectX::PackedVector::XMConvertFloatToHalfStream(
            dst + 1, 2 * sizeof(DirectX::PackedVector::HALF), temperatureSrc,
            sizeof(float), volume.dim[0]);
      }
    }
  }
  volume.uploadBuffer->Unmap(0, nullptr);

  D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
  dstLoc.pResource = volume.texture.Get();
  dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
  srcLoc.pResource = volume.uploadBuffer.Get();
  srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  srcLoc.PlacedFootprint = footprint;
  cmdList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = volume.texture.Get();
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  cmdList->ResourceBarrier(1, &barrier);
  volume.needsUpload = false;
  return true;
}

} // namespace

bool SetActiveVolume(const assetlib::AssetId &id) {
  RuntimeVolume *volume = ResolveVolume(id);
  if (!volume)
    return false;
  s_lastResolvedId = id;
  return true;
}

void ClearActiveVolume() {
  s_lastResolvedId = {};
  s_volumes.clear();
}

bool HasActiveVolume() {
  for (const Scene::Node &node : Scene::GetNodes()) {
    if (node.visible && !node.volumeAssetId.empty())
      return true;
  }
  return false;
}
const assetlib::AssetId &ActiveVolumeId() { return s_lastResolvedId; }
Params &GetParams() { return s_params; }

SequenceInfo GetSequenceInfo(const assetlib::AssetId &id) {
  SequenceInfo info;
  RuntimeVolume *volume = ResolveVolume(id);
  if (!volume)
    return info;
  info.animated = volume->sequence;
  info.frameCount = volume->sequenceFrameCount;
  info.currentFrame = volume->currentFrame;
  info.pendingFrame = volume->pendingFrame;
  info.loading = volume->pendingLoad.valid();
  info.sourceFps = volume->sequenceFps;
  return info;
}

VolumeStats GetVolumeStats(const assetlib::AssetId &id) {
  VolumeStats stats;
  RuntimeVolume *volume = ResolveVolume(id);
  if (!volume)
    return stats;
  stats.hasTemperature = !volume->temperature.empty();
  stats.densityMax = volume->densityMax;
  return stats;
}

bool UpdateSequenceFrame(const assetlib::AssetId &id, uint32_t frameIndex) {
  RuntimeVolume *volume = ResolveVolume(id);
  if (!volume || !volume->sequence || volume->sequenceFrameCount < 2)
    return false;

  volume->requestedFrame =
      (std::min)(frameIndex, volume->sequenceFrameCount - 1);
  bool changed = false;
  if (volume->pendingLoad.valid() &&
      volume->pendingLoad.wait_for(std::chrono::seconds(0)) ==
          std::future_status::ready) {
    auto [loaded, frame] = volume->pendingLoad.get();
    if (loaded) {
      ApplyCpuFrame(*volume, std::move(frame), volume->pendingFrame);
      changed = true;
    } else {
      fprintf(stderr, "VDB sequence frame %u failed to load for %s\n",
              volume->pendingFrame, id.ToString().c_str());
    }
  }

  if (!volume->pendingLoad.valid() &&
      volume->requestedFrame != volume->currentFrame) {
    const uint32_t requested = volume->requestedFrame;
    assetlib::AssetRegistry *registry = assetlib::GlobalRegistry();
    if (!registry)
      return changed;
    const std::filesystem::path cachePath =
        requested == 0
            ? registry->paths().cookedVolumePath(id)
            : registry->paths().cookedVolumeFramePath(id, requested);
    volume->pendingFrame = requested;
    volume->pendingLoad = std::async(
        std::launch::async, [cachePath]() {
          assetlib::CookedVolume cooked;
          std::vector<uint8_t> blob;
          const bool loaded =
              assetlib::ReadCookedFile(cachePath, blob) &&
              assetlib::DeserializeCookedVolume(blob.data(), blob.size(),
                                                cooked);
          RuntimeVolume temporary;
          if (!loaded || !BuildDenseField(cooked, temporary))
            return std::make_pair(false, CpuVolumeFrame{});
          return std::make_pair(true, TakeCpuFrame(temporary));
        });
  }
  return changed;
}

void AppendEmissionLights(std::vector<Light> &lights) {
  constexpr uint32_t kClustersPerAxis = 4;
  constexpr uint32_t kClusterCount =
      kClustersPerAxis * kClustersPerAxis * kClustersPerAxis;
  struct ClusterAccum {
    double weight = 0.0;
    double colorWeight[3] = {};
    double px = 0.0;
    double py = 0.0;
    double pz = 0.0;
    uint64_t hotVoxelCount = 0;
  };
  s_emissionLightStats = {};
  for (const Scene::Node &node : Scene::GetNodes()) {
    const Scene::VolumeMaterial &material = node.volumeMaterial;
    if (!node.visible || node.volumeAssetId.empty() ||
        material.emissionStrength <= 0.0f ||
        material.lightingStrength <= 0.0f)
      continue;
    assetlib::AssetId id;
    if (!assetlib::AssetId::FromString(node.volumeAssetId, id))
      continue;
    RuntimeVolume *volume = ResolveVolume(id);
    if (!volume || volume->heatVoxels.empty() ||
        volume->temperatureInvRange <= 0.0f)
      continue;

    const double voxelCountTotal =
        static_cast<double>(volume->dim[0]) * volume->dim[1] * volume->dim[2];
    if (voxelCountTotal <= 0.0)
      continue;
    const float determinant =
        node.transform[0] *
            (node.transform[5] * node.transform[10] -
             node.transform[9] * node.transform[6]) -
        node.transform[4] *
            (node.transform[1] * node.transform[10] -
             node.transform[9] * node.transform[2]) +
        node.transform[8] *
            (node.transform[1] * node.transform[6] -
             node.transform[5] * node.transform[2]);
    const double worldVoxelVolume =
        std::abs(static_cast<double>(determinant)) / voxelCountTotal;
    if (worldVoxelVolume <= 1.0e-12)
      continue;

    bool emittedVolumeLight = false;
    const float low = (std::clamp)(material.temperatureLow, 0.0f, 1.0f);
    const float high =
        (std::clamp)(material.temperatureHigh, low + 1.0e-4f, 1.0f);
    std::array<ClusterAccum, kClusterCount> clusters;
    for (const HeatVoxel &voxel : volume->heatVoxels) {
      const float raw = (voxel.value - volume->temperatureMin) *
                        volume->temperatureInvRange;
      const float heat =
          (std::clamp)((raw - low) / (high - low), 0.0f, 1.0f);
      const float w =
          std::pow(heat, (std::max)(material.temperatureGamma, 0.05f));
      if (w <= 1.0e-8f)
        continue;
      const uint32_t cx =
          (std::min)(voxel.x * kClustersPerAxis / volume->dim[0],
                     kClustersPerAxis - 1);
      const uint32_t cy =
          (std::min)(voxel.y * kClustersPerAxis / volume->dim[1],
                     kClustersPerAxis - 1);
      const uint32_t cz =
          (std::min)(voxel.z * kClustersPerAxis / volume->dim[2],
                     kClustersPerAxis - 1);
      ClusterAccum &cluster =
          clusters[(cz * kClustersPerAxis + cy) * kClustersPerAxis + cx];
      const DirectX::XMFLOAT3 fireColor = FireColor(heat);
      cluster.weight += w;
      cluster.colorWeight[0] += static_cast<double>(fireColor.x) * w;
      cluster.colorWeight[1] += static_cast<double>(fireColor.y) * w;
      cluster.colorWeight[2] += static_cast<double>(fireColor.z) * w;
      cluster.px += (static_cast<double>(voxel.x) + 0.5) * w;
      cluster.py += (static_cast<double>(voxel.y) + 0.5) * w;
      cluster.pz += (static_cast<double>(voxel.z) + 0.5) * w;
      ++cluster.hotVoxelCount;
    }

    for (uint32_t cz = 0; cz < kClustersPerAxis; ++cz) {
      for (uint32_t cy = 0; cy < kClustersPerAxis; ++cy) {
        for (uint32_t cx = 0; cx < kClustersPerAxis; ++cx) {
          const ClusterAccum &cluster =
              clusters[(cz * kClustersPerAxis + cy) * kClustersPerAxis + cx];
          if (cluster.weight <= 1.0e-6)
            continue;

          const float local[3] = {
              static_cast<float>(cluster.px / cluster.weight / volume->dim[0]),
              static_cast<float>(cluster.py / cluster.weight / volume->dim[1]),
              static_cast<float>(cluster.pz / cluster.weight / volume->dim[2])};
          Light light = {};
          light.type = static_cast<uint32_t>(LightType::Omni);
          light.position[0] = local[0] * node.transform[0] +
                              local[1] * node.transform[4] +
                              local[2] * node.transform[8] + node.transform[12];
          light.position[1] = local[0] * node.transform[1] +
                              local[1] * node.transform[5] +
                              local[2] * node.transform[9] + node.transform[13];
          light.position[2] = local[0] * node.transform[2] +
                              local[1] * node.transform[6] +
                              local[2] * node.transform[10] + node.transform[14];
          // The volume shader stores an emission coefficient integrated over
          // world distance. Integrating that coefficient over world volume
          // produces the radiant intensity represented by this point proxy.
          const double clusterScale =
              static_cast<double>(material.emissionStrength) *
              material.lightingStrength * worldVoxelVolume;
          for (int channel = 0; channel < 3; ++channel)
            light.emission[channel] =
                static_cast<float>(
                    (std::max)(material.emissionColor[channel], 0.0f) *
                    cluster.colorWeight[channel] * clusterScale);
          const double occupiedWorldVolume =
              static_cast<double>(cluster.hotVoxelCount) * worldVoxelVolume;
          light.radius = static_cast<float>(
              (std::max)(0.05, 0.5 * std::cbrt(occupiedWorldVolume)));
          light.iesAtlasIndex = -1;
          lights.push_back(light);
          const float intensity =
              (light.emission[0] + light.emission[1] + light.emission[2]) /
              3.0f;
          s_emissionLightStats.totalIntensity += intensity;
          s_emissionLightStats.maxIntensity =
              (std::max)(s_emissionLightStats.maxIntensity, intensity);
          ++s_emissionLightStats.lightCount;
          emittedVolumeLight = true;
        }
      }
    }
    if (emittedVolumeLight)
      ++s_emissionLightStats.volumeCount;
  }
}

EmissionLightStats GetEmissionLightStats() { return s_emissionLightStats; }

ID3D12Resource *GetDensityTexture() {
  auto it = s_volumes.find(s_lastResolvedId.ToString());
  return it == s_volumes.end() ? nullptr : it->second.texture.Get();
}
DirectX::XMFLOAT3 BoundsMin() {
  auto it = s_volumes.find(s_lastResolvedId.ToString());
  return it == s_volumes.end() ? DirectX::XMFLOAT3{} : it->second.boundsMin;
}
DirectX::XMFLOAT3 BoundsMax() {
  auto it = s_volumes.find(s_lastResolvedId.ToString());
  return it == s_volumes.end() ? DirectX::XMFLOAT3{} : it->second.boundsMax;
}

bool EnsureUploaded(ID3D12Device *device, ID3D12GraphicsCommandList *cmdList) {
  RuntimeVolume *volume = ResolveVolume(s_lastResolvedId);
  return volume && EnsureUploaded(*volume, device, cmdList);
}

bool Composite(ID3D12Device *device, ID3D12GraphicsCommandList *cmdList,
               ID3D12Resource *cameraCB, ID3D12Resource *colorTarget,
               ID3D12Resource *depthTexture, UINT width, UINT height,
               DepthEncoding depthEncoding) {
  if (!device || !cmdList || !cameraCB || !colorTarget || !depthTexture ||
      width == 0 || height == 0 || !HasActiveVolume()) {
    return false;
  }
  if (!EnsureCompositePipeline(device)) {
    return false;
  }

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
    float color[3];
    float emissionStrength;
    float emissionColor[3];
    float temperatureMin;
    float temperatureInvRange;
    float temperatureLow;
    float temperatureHigh;
    float temperatureGamma;
  };
  static_assert(sizeof(CompositeConstants) == 36 * sizeof(uint32_t));

  const UINT descriptorSize = device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  cmdList->SetPipelineState(
      depthEncoding == DepthEncoding::LinearViewDepth
          ? s_dxrCompositePSO.Get()
          : s_rasterCompositePSO.Get());
  cmdList->SetComputeRootSignature(s_compositeRootSig.Get());
  cmdList->SetComputeRootConstantBufferView(
      0, cameraCB->GetGPUVirtualAddress());
  std::vector<const Scene::Node *> visibleVolumes;
  for (const Scene::Node &node : Scene::GetNodes())
    if (node.visible && !node.volumeAssetId.empty())
      visibleVolumes.push_back(&node);
  std::stable_sort(
      visibleVolumes.begin(), visibleVolumes.end(),
      [](const Scene::Node *a, const Scene::Node *b) {
        const auto distanceSquared = [](const Scene::Node *node) {
          const float x = node->transform[12] + 0.5f * (node->transform[0] +
                                                        node->transform[4] +
                                                        node->transform[8]) -
                          g_cameraData.pos[0];
          const float y = node->transform[13] + 0.5f * (node->transform[1] +
                                                        node->transform[5] +
                                                        node->transform[9]) -
                          g_cameraData.pos[1];
          const float z = node->transform[14] + 0.5f * (node->transform[2] +
                                                        node->transform[6] +
                                                        node->transform[10]) -
                          g_cameraData.pos[2];
          return x * x + y * y + z * z;
        };
        return distanceSquared(a) > distanceSquared(b);
      });

  bool rendered = false;
  for (auto &[key, volume] : s_volumes) {
    (void)key;
    for (RetiredVolumeGpuFrame &retired : volume.retiredGpuFrames)
      ++retired.age;
    volume.retiredGpuFrames.erase(
        std::remove_if(volume.retiredGpuFrames.begin(),
                       volume.retiredGpuFrames.end(),
                       [](const RetiredVolumeGpuFrame &retired) {
                         return retired.age > 4;
                       }),
        volume.retiredGpuFrames.end());
  }
  for (const Scene::Node *nodePtr : visibleVolumes) {
    const Scene::Node &node = *nodePtr;
    assetlib::AssetId id;
    if (!assetlib::AssetId::FromString(node.volumeAssetId, id))
      continue;
    RuntimeVolume *volume = ResolveVolume(id);
    if (!volume || !EnsureUploaded(*volume, device, cmdList))
      continue;
    if (!volume->descriptorHeap) {
      D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
      heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
      heapDesc.NumDescriptors = 3;
      heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
      if (FAILED(device->CreateDescriptorHeap(
              &heapDesc, IID_PPV_ARGS(&volume->descriptorHeap)))) {
        continue;
      }
    }

    const DirectX::XMMATRIX localToWorld = DirectX::XMLoadFloat4x4(
        reinterpret_cast<const DirectX::XMFLOAT4X4 *>(node.transform));
    DirectX::XMVECTOR determinant = DirectX::XMMatrixDeterminant(localToWorld);
    if (std::abs(DirectX::XMVectorGetX(determinant)) < 1.0e-8f)
      continue;
    DirectX::XMFLOAT4X4 worldToLocalData;
    DirectX::XMStoreFloat4x4(
        &worldToLocalData, DirectX::XMMatrixInverse(&determinant, localToWorld));

    CompositeConstants constants = {};
    std::memcpy(constants.worldToLocal, &worldToLocalData,
                sizeof(constants.worldToLocal));
    const Scene::VolumeMaterial &material = node.volumeMaterial;
    constants.densityScale = material.densityScale;
    constants.absorption = material.absorption;
    constants.scatterG = material.scattering;
    constants.ambient = material.ambient;
    constants.stepJitter = material.stepJitter;
    constants.marchSteps =
        static_cast<uint32_t>((std::clamp)(material.marchSteps, 1, 1024));
    constants.frameSeed = static_cast<float>(s_frameIndex++ & 1023u);
    constants.lightSteps =
        static_cast<uint32_t>((std::clamp)(material.lightSteps, 1, 32));
    std::memcpy(constants.color, material.color, sizeof(constants.color));
    constants.emissionStrength = (std::max)(material.emissionStrength, 0.0f);
    std::memcpy(constants.emissionColor, material.emissionColor,
                sizeof(constants.emissionColor));
    constants.temperatureMin = volume->temperatureMin;
    constants.temperatureInvRange = volume->temperatureInvRange;
    constants.temperatureLow =
        (std::clamp)(material.temperatureLow, 0.0f, 1.0f);
    constants.temperatureHigh =
        (std::clamp)(material.temperatureHigh,
                     constants.temperatureLow + 1.0e-4f, 1.0f);
    constants.temperatureGamma =
        (std::clamp)(material.temperatureGamma, 0.05f, 8.0f);

    D3D12_SHADER_RESOURCE_VIEW_DESC densitySrv = {};
    densitySrv.Format = DXGI_FORMAT_R16G16_FLOAT;
    densitySrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
    densitySrv.Shader4ComponentMapping =
        D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    densitySrv.Texture3D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu =
        volume->descriptorHeap->GetCPUDescriptorHandleForHeapStart();
    device->CreateShaderResourceView(volume->texture.Get(), &densitySrv, cpu);

    D3D12_SHADER_RESOURCE_VIEW_DESC depthSrv = {};
    depthSrv.Format = DXGI_FORMAT_R32_FLOAT;
    depthSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    depthSrv.Shader4ComponentMapping =
        D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    depthSrv.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE depthCpu = cpu;
    depthCpu.ptr += descriptorSize;
    device->CreateShaderResourceView(depthTexture, &depthSrv, depthCpu);

    D3D12_UNORDERED_ACCESS_VIEW_DESC colorUav = {};
    colorUav.Format = colorTarget->GetDesc().Format;
    colorUav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    D3D12_CPU_DESCRIPTOR_HANDLE colorCpu = depthCpu;
    colorCpu.ptr += descriptorSize;
    device->CreateUnorderedAccessView(colorTarget, nullptr, &colorUav,
                                      colorCpu);

    ID3D12DescriptorHeap *heaps[] = {volume->descriptorHeap.Get()};
    cmdList->SetDescriptorHeaps(_countof(heaps), heaps);
    D3D12_GPU_DESCRIPTOR_HANDLE gpu =
        volume->descriptorHeap->GetGPUDescriptorHandleForHeapStart();
    cmdList->SetComputeRootDescriptorTable(2, gpu);
    gpu.ptr += 2ull * descriptorSize;
    cmdList->SetComputeRootDescriptorTable(3, gpu);
    cmdList->SetComputeRoot32BitConstants(1, 36, &constants, 0);
    cmdList->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = colorTarget;
    cmdList->ResourceBarrier(1, &barrier);
    rendered = true;
  }
  return rendered;
}

} // namespace VolumetricRenderer
