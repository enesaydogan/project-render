#include "volumetric_renderer.h"

#include "asset_library/cooked_payload.h"
#include "asset_library/global_registry.h"
#include "asset_library/pack_mounts.h"
#include "d3d12_helpers.h"

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

ComPtr<ID3D12Resource> s_densityTex;
ComPtr<ID3D12Resource> s_uploadBuffer; // kept alive across frames
D3D12_RESOURCE_STATES s_densityState = D3D12_RESOURCE_STATE_COMMON;

// Build the dense R32F field from the sparse bricks (dequantizing each brick).
void BuildDenseField(const assetlib::CookedVolume &vol) {
  s_dim[0] = vol.dim[0];
  s_dim[1] = vol.dim[1];
  s_dim[2] = vol.dim[2];
  s_boundsMin = {vol.boundsMin[0], vol.boundsMin[1], vol.boundsMin[2]};
  s_boundsMax = {vol.boundsMax[0], vol.boundsMax[1], vol.boundsMax[2]};
  const size_t total = static_cast<size_t>(vol.dim[0]) * vol.dim[1] * vol.dim[2];
  s_density.assign(total, 0.0f);
  const int B = static_cast<int>(vol.brickSize);
  for (const auto &brick : vol.bricks) {
    const float range = brick.maxVal - brick.minVal;
    for (int lz = 0; lz < B; ++lz)
      for (int ly = 0; ly < B; ++ly)
        for (int lx = 0; lx < B; ++lx) {
          const uint32_t gx = brick.bx * B + lx;
          const uint32_t gy = brick.by * B + ly;
          const uint32_t gz = brick.bz * B + lz;
          if (gx >= s_dim[0] || gy >= s_dim[1] || gz >= s_dim[2])
            continue;
          const uint8_t q = brick.data[static_cast<size_t>((lz * B + ly) * B + lx)];
          const float density = brick.minVal + (q / 255.0f) * range;
          s_density[(static_cast<size_t>(gz) * s_dim[1] + gy) * s_dim[0] + gx] =
              density;
        }
  }
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
  BuildDenseField(vol);
  s_activeId = id;
  s_needsUpload = true;
  s_densityTex.Reset(); // force recreate at new dims
  return true;
}

void ClearActiveVolume() {
  s_activeId = {};
  s_density.clear();
  s_dim[0] = s_dim[1] = s_dim[2] = 0;
  s_needsUpload = false;
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

  // Create the 3D texture.
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
  desc.Width = s_dim[0];
  desc.Height = s_dim[1];
  desc.DepthOrArraySize = static_cast<UINT16>(s_dim[2]);
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_R32_FLOAT;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  D3D12_HEAP_PROPERTIES defaultProp = {D3D12_HEAP_TYPE_DEFAULT};
  ThrowIfFailed(device->CreateCommittedResource(
      &defaultProp, D3D12_HEAP_FLAG_NONE, &desc,
      D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&s_densityTex)));
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
  ThrowIfFailed(device->CreateCommittedResource(
      &uploadProp, D3D12_HEAP_FLAG_NONE, &bufDesc,
      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
      IID_PPV_ARGS(&s_uploadBuffer)));

  // Copy dense data into the (row-padded) upload buffer.
  uint8_t *mapped = nullptr;
  D3D12_RANGE noRead = {0, 0};
  ThrowIfFailed(s_uploadBuffer->Map(0, &noRead, reinterpret_cast<void **>(&mapped)));
  const size_t srcRowBytes = static_cast<size_t>(s_dim[0]) * sizeof(float);
  const UINT64 dstRowPitch = footprint.Footprint.RowPitch;
  const UINT64 dstSlicePitch = dstRowPitch * numRows;
  for (uint32_t z = 0; z < s_dim[2]; ++z) {
    for (uint32_t y = 0; y < s_dim[1]; ++y) {
      const float *src =
          &s_density[(static_cast<size_t>(z) * s_dim[1] + y) * s_dim[0]];
      uint8_t *dst = mapped + footprint.Offset + z * dstSlicePitch +
                     static_cast<UINT64>(y) * dstRowPitch;
      memcpy(dst, src, srcRowBytes);
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

} // namespace VolumetricRenderer
