#include "saved_views.h"

#include "camera.h"
#include "dx12_context.h"
#include "dxr_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace SavedViews {
namespace {

constexpr uint32_t kThumbnailWidth = 192;
constexpr uint32_t kThumbnailHeight = 108;

std::vector<SavedView> g_savedViews;
std::string g_lastStatus;
int g_selectedViewIndex = -1;

int FindExternalViewIndex(const std::string &sessionId,
                          const std::string &objectId) {
  for (size_t index = 0; index < g_savedViews.size(); ++index) {
    const SavedView &view = g_savedViews[index];
    if (!view.external || view.sourceSessionId != sessionId ||
        view.sourceObjectId != objectId) {
      continue;
    }
    return static_cast<int>(index);
  }
  return -1;
}

void AdjustSelectionAfterErase(size_t removedIndex) {
  if (g_savedViews.empty()) {
    g_selectedViewIndex = -1;
    return;
  }
  if (g_selectedViewIndex == static_cast<int>(removedIndex)) {
    if (removedIndex >= g_savedViews.size()) {
      g_selectedViewIndex = static_cast<int>(g_savedViews.size()) - 1;
    }
    return;
  }
  if (g_selectedViewIndex > static_cast<int>(removedIndex)) {
    --g_selectedViewIndex;
  }
}

void TransitionResourceForReadback(ID3D12GraphicsCommandList *commandList,
                                   ID3D12Resource *resource,
                                   D3D12_RESOURCE_STATES before,
                                   D3D12_RESOURCE_STATES after) {
  if (!commandList || !resource || before == after) {
    return;
  }

  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = resource;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = before;
  barrier.Transition.StateAfter = after;
  commandList->ResourceBarrier(1, &barrier);
}

std::string MakeDefaultViewName() {
  int candidate = 1;
  for (;;) {
    std::ostringstream stream;
    stream << "View " << candidate;
    const std::string name = stream.str();
    const bool exists = std::any_of(
        g_savedViews.begin(), g_savedViews.end(),
        [&name](const SavedView &view) { return view.name == name; });
    if (!exists) {
      return name;
    }
    ++candidate;
  }
}

void CaptureCurrentCameraState(SavedView &view) {
  std::memcpy(view.pos, g_cameraData.pos, sizeof(view.pos));
  std::memcpy(view.forward, g_cameraData.forward, sizeof(view.forward));
  std::memcpy(view.up, g_cameraData.up, sizeof(view.up));
  view.fov = g_cameraData.fov;
  view.nearZ = g_cameraData.nearZ;
  view.farZ = g_cameraData.farZ;
  view.intensity = g_cameraData.intensity;
  view.maxSpecularBounces = g_cameraData.maxSpecularBounces;
  view.maxRefractiveBounces = g_cameraData.maxRefractiveBounces;
  view.maxGIBounces = g_cameraData.maxGIBounces;
  view.maxSPP = g_cameraData.maxSPP;
  view.yaw = g_camYaw;
  view.pitch = g_camPitch;
  view.useAdaptiveSampling = g_cameraData.useAdaptiveSampling;
  view.noiseThreshold = g_cameraData.noiseThreshold;
  view.debugVisualizationMode = g_cameraData.debugVisualizationMode;
  view.sampleEnvSolidAngle = g_cameraData.sampleEnvSolidAngle;
  view.autoExposure = DxrRenderer::GetAutoExposure();
  view.physicalCameraExposure = DxrRenderer::GetPhysicalCameraExposure();
  view.safeFrameEnabled = g_safeFrameEnabled;
  view.exposureCompensation = DxrRenderer::GetExposureCompensation();
  DxrRenderer::GetPhysicalCameraSettings(view.iso, view.shutterSeconds,
                                         view.aperture);
  view.tonemapAoIntensity = DxrRenderer::GetTonemapAmbientOcclusionIntensity();
  view.tonemapAoLengthMm = DxrRenderer::GetTonemapAmbientOcclusionLengthMm();
  view.tonemapAoMode =
      static_cast<int>(DxrRenderer::GetTonemapAmbientOcclusionMode());
}

void ApplyCameraState(const SavedView &view) {
  std::memcpy(g_cameraData.pos, view.pos, sizeof(view.pos));
  std::memcpy(g_cameraData.forward, view.forward, sizeof(view.forward));
  std::memcpy(g_cameraData.up, view.up, sizeof(view.up));
  g_cameraData.fov = view.fov;
  g_cameraData.nearZ = view.nearZ;
  g_cameraData.farZ = view.farZ;
  g_cameraData.intensity = view.intensity;
  g_cameraData.maxSpecularBounces = view.maxSpecularBounces;
  g_cameraData.maxRefractiveBounces = view.maxRefractiveBounces;
  g_cameraData.maxGIBounces = view.maxGIBounces;
  g_cameraData.maxSPP = view.maxSPP;
  g_cameraData.useAdaptiveSampling = view.useAdaptiveSampling;
  g_cameraData.noiseThreshold = view.noiseThreshold;
  g_cameraData.debugVisualizationMode = view.debugVisualizationMode;
  g_cameraData.sampleEnvSolidAngle = view.sampleEnvSolidAngle;
  g_camYaw = view.yaw;
  g_camPitch = view.pitch;
  g_safeFrameEnabled = view.safeFrameEnabled;
  DxrRenderer::SetAutoExposure(view.autoExposure);
  DxrRenderer::SetPhysicalCameraExposure(view.physicalCameraExposure);
  DxrRenderer::SetExposureCompensation(view.exposureCompensation);
  DxrRenderer::SetPhysicalCameraSettings(view.iso, view.shutterSeconds,
                                         view.aperture);
  DxrRenderer::SetTonemapAmbientOcclusionIntensity(view.tonemapAoIntensity);
  DxrRenderer::SetTonemapAmbientOcclusionLengthMm(view.tonemapAoLengthMm);
  DxrRenderer::SetTonemapAmbientOcclusionMode(
      static_cast<DxrRenderer::TonemapAmbientOcclusionMode>(
          std::clamp(view.tonemapAoMode, 0, 2)));
  UpdateCameraCB();
  DxrRenderer::ResetAccumulation();
}

bool ReadBackCurrentBackbuffer(std::vector<uint8_t> &rgba, uint32_t &width,
                               uint32_t &height) {
  if (!DX12Context::g_device || !DX12Context::g_commandQueue ||
      !DX12Context::g_fence || !DX12Context::g_fenceEvent ||
      !DX12Context::g_swapChain) {
    return false;
  }

  ID3D12Resource *source =
      DX12Context::g_renderTargets[DX12Context::g_frameIndex].Get();
  if (!source) {
    return false;
  }

  DX12Context::WaitGPUIdle();

  const D3D12_RESOURCE_DESC srcDesc = source->GetDesc();
  if (srcDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
      srcDesc.DepthOrArraySize != 1 || srcDesc.MipLevels != 1) {
    return false;
  }

  width = static_cast<uint32_t>(srcDesc.Width);
  height = srcDesc.Height;
  if (width == 0 || height == 0) {
    return false;
  }

  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
  UINT numRows = 0;
  UINT64 rowSizeInBytes = 0;
  UINT64 totalBytes = 0;
  DX12Context::g_device->GetCopyableFootprints(&srcDesc, 0, 1, 0, &footprint,
                                               &numRows, &rowSizeInBytes,
                                               &totalBytes);
  if (numRows == 0 || totalBytes == 0) {
    return false;
  }

  D3D12_HEAP_PROPERTIES readbackHeap = {};
  readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;

  D3D12_RESOURCE_DESC readbackDesc = {};
  readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  readbackDesc.Width = totalBytes;
  readbackDesc.Height = 1;
  readbackDesc.DepthOrArraySize = 1;
  readbackDesc.MipLevels = 1;
  readbackDesc.SampleDesc.Count = 1;
  readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

  ComPtr<ID3D12Resource> readback;
  if (FAILED(DX12Context::g_device->CreateCommittedResource(
          &readbackHeap, D3D12_HEAP_FLAG_NONE, &readbackDesc,
          D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
          IID_PPV_ARGS(&readback)))) {
    return false;
  }

  ComPtr<ID3D12CommandAllocator> commandAllocator;
  if (FAILED(DX12Context::g_device->CreateCommandAllocator(
          D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)))) {
    return false;
  }

  ComPtr<ID3D12GraphicsCommandList> commandList;
  if (FAILED(DX12Context::g_device->CreateCommandList(
          0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr,
          IID_PPV_ARGS(&commandList)))) {
    return false;
  }

  TransitionResourceForReadback(commandList.Get(), source,
                                D3D12_RESOURCE_STATE_PRESENT,
                                D3D12_RESOURCE_STATE_COPY_SOURCE);

  D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
  srcLoc.pResource = source;
  srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  srcLoc.SubresourceIndex = 0;

  D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
  dstLoc.pResource = readback.Get();
  dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  dstLoc.PlacedFootprint = footprint;

  commandList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

  TransitionResourceForReadback(commandList.Get(), source,
                                D3D12_RESOURCE_STATE_COPY_SOURCE,
                                D3D12_RESOURCE_STATE_PRESENT);

  if (FAILED(commandList->Close())) {
    return false;
  }

  ID3D12CommandList *lists[] = {commandList.Get()};
  DX12Context::g_commandQueue->ExecuteCommandLists(1, lists);
  DX12Context::WaitGPUIdle();

  uint8_t *mapped = nullptr;
  if (FAILED(readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped))) ||
      !mapped) {
    return false;
  }

  rgba.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
  const UINT srcPitch = footprint.Footprint.RowPitch;
  for (uint32_t y = 0; y < height; ++y) {
    const uint8_t *srcRow = mapped + footprint.Offset + y * srcPitch;
    uint8_t *dstRow = rgba.data() + static_cast<size_t>(y) * width * 4u;
    if (srcDesc.Format == DXGI_FORMAT_R10G10B10A2_UNORM) {
      const auto *srcPixels = reinterpret_cast<const uint32_t *>(srcRow);
      for (uint32_t x = 0; x < width; ++x) {
        const uint32_t packed = srcPixels[x];
        const uint32_t r10 = packed & 0x3FFu;
        const uint32_t g10 = (packed >> 10) & 0x3FFu;
        const uint32_t b10 = (packed >> 20) & 0x3FFu;
        const uint32_t a2 = (packed >> 30) & 0x3u;
        dstRow[x * 4 + 0] = static_cast<uint8_t>((r10 * 255u + 511u) / 1023u);
        dstRow[x * 4 + 1] = static_cast<uint8_t>((g10 * 255u + 511u) / 1023u);
        dstRow[x * 4 + 2] = static_cast<uint8_t>((b10 * 255u + 511u) / 1023u);
        dstRow[x * 4 + 3] = static_cast<uint8_t>((a2 * 255u + 1u) / 3u);
      }
    } else if (srcDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM ||
               srcDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
      std::memcpy(dstRow, srcRow, static_cast<size_t>(width) * 4u);
    } else if (srcDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
               srcDesc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) {
      for (uint32_t x = 0; x < width; ++x) {
        dstRow[x * 4 + 0] = srcRow[x * 4 + 2];
        dstRow[x * 4 + 1] = srcRow[x * 4 + 1];
        dstRow[x * 4 + 2] = srcRow[x * 4 + 0];
        dstRow[x * 4 + 3] = srcRow[x * 4 + 3];
      }
    } else {
      readback->Unmap(0, nullptr);
      rgba.clear();
      return false;
    }
  }

  readback->Unmap(0, nullptr);
  return true;
}

std::vector<uint8_t> ResizeRgbaNearest(const std::vector<uint8_t> &source,
                                       uint32_t sourceWidth,
                                       uint32_t sourceHeight,
                                       uint32_t targetWidth,
                                       uint32_t targetHeight) {
  std::vector<uint8_t> result(static_cast<size_t>(targetWidth) *
                              static_cast<size_t>(targetHeight) * 4u,
                              0u);
  if (source.empty() || sourceWidth == 0 || sourceHeight == 0 ||
      targetWidth == 0 || targetHeight == 0) {
    return result;
  }

  for (uint32_t y = 0; y < targetHeight; ++y) {
    const uint32_t srcY = (std::min)(sourceHeight - 1,
                                     (y * sourceHeight) / targetHeight);
    for (uint32_t x = 0; x < targetWidth; ++x) {
      const uint32_t srcX =
          (std::min)(sourceWidth - 1, (x * sourceWidth) / targetWidth);
      const size_t srcOffset =
          (static_cast<size_t>(srcY) * sourceWidth + srcX) * 4u;
      const size_t dstOffset =
          (static_cast<size_t>(y) * targetWidth + x) * 4u;
      result[dstOffset + 0] = source[srcOffset + 0];
      result[dstOffset + 1] = source[srcOffset + 1];
      result[dstOffset + 2] = source[srcOffset + 2];
      result[dstOffset + 3] = source[srcOffset + 3];
    }
  }

  return result;
}

bool CaptureThumbnail(std::vector<uint8_t> &thumbnailRgba,
                      uint32_t &thumbnailWidth, uint32_t &thumbnailHeight) {
  std::vector<uint8_t> sourceRgba;
  uint32_t sourceWidth = 0;
  uint32_t sourceHeight = 0;
  if (!ReadBackCurrentBackbuffer(sourceRgba, sourceWidth, sourceHeight)) {
    return false;
  }

  thumbnailWidth = kThumbnailWidth;
  thumbnailHeight = kThumbnailHeight;
  thumbnailRgba = ResizeRgbaNearest(sourceRgba, sourceWidth, sourceHeight,
                                    thumbnailWidth, thumbnailHeight);
  return !thumbnailRgba.empty();
}

} // namespace

const std::vector<SavedView> &GetViews() { return g_savedViews; }

const std::string &GetLastStatus() { return g_lastStatus; }

int GetSelectedViewIndex() { return g_selectedViewIndex; }

void SetSelectedViewIndex(int index) { g_selectedViewIndex = index; }

SavedView CaptureCurrentState() {
  SavedView view;
  CaptureCurrentCameraState(view);
  return view;
}

size_t AddCurrentView(const std::string &preferredName) {
  SavedView view;
  view.name = preferredName.empty() ? MakeDefaultViewName() : preferredName;
  CaptureCurrentCameraState(view);
  CaptureThumbnail(view.thumbnailRgba, view.thumbnailWidth,
                   view.thumbnailHeight);
  g_savedViews.push_back(std::move(view));
  g_selectedViewIndex = static_cast<int>(g_savedViews.size()) - 1;
  g_lastStatus = "Saved view: " + g_savedViews.back().name;
  return g_savedViews.size() - 1;
}

bool RemoveView(size_t index) {
  if (index >= g_savedViews.size()) {
    return false;
  }
  g_lastStatus = "Deleted view: " + g_savedViews[index].name;
  g_savedViews.erase(g_savedViews.begin() + static_cast<std::ptrdiff_t>(index));
  AdjustSelectionAfterErase(index);
  return true;
}

bool ApplyView(const SavedView &view) {
  ApplyCameraState(view);
  g_lastStatus = "Applied view: " + view.name;
  return true;
}

bool ApplyView(size_t index) {
  if (index >= g_savedViews.size()) {
    return false;
  }
  return ApplyView(g_savedViews[index]);
}

size_t UpsertExternalView(const SavedView &view) {
  SavedView externalView = view;
  externalView.external = true;
  externalView.thumbnailRgba.clear();
  externalView.thumbnailWidth = 0;
  externalView.thumbnailHeight = 0;

  const int existingIndex = FindExternalViewIndex(externalView.sourceSessionId,
                                                  externalView.sourceObjectId);
  if (existingIndex >= 0) {
    g_savedViews[existingIndex] = std::move(externalView);
    g_lastStatus = "Updated external view: " + g_savedViews[existingIndex].name;
    return static_cast<size_t>(existingIndex);
  }

  g_savedViews.push_back(std::move(externalView));
  g_lastStatus = "Added external view: " + g_savedViews.back().name;
  return g_savedViews.size() - 1;
}

bool RemoveExternalView(const std::string &sessionId,
                        const std::string &objectId) {
  const int index = FindExternalViewIndex(sessionId, objectId);
  if (index < 0) {
    return false;
  }

  g_lastStatus = "Removed external view: " + g_savedViews[index].name;
  g_savedViews.erase(g_savedViews.begin() + index);
  AdjustSelectionAfterErase(static_cast<size_t>(index));
  return true;
}

void RemoveExternalViewsForSession(const std::string &sessionId) {
  for (size_t index = g_savedViews.size(); index > 0; --index) {
    const SavedView &view = g_savedViews[index - 1];
    if (!view.external || view.sourceSessionId != sessionId) {
      continue;
    }
    g_lastStatus = "Removed external view: " + view.name;
    g_savedViews.erase(g_savedViews.begin() +
                       static_cast<std::ptrdiff_t>(index - 1));
    AdjustSelectionAfterErase(index - 1);
  }
}

void RemoveAllExternalViews() {
  for (size_t index = g_savedViews.size(); index > 0; --index) {
    if (!g_savedViews[index - 1].external) {
      continue;
    }
    g_savedViews.erase(g_savedViews.begin() +
                       static_cast<std::ptrdiff_t>(index - 1));
    AdjustSelectionAfterErase(index - 1);
  }
  if (g_lastStatus.empty()) {
    g_lastStatus = "Views cleared";
  }
}

void Clear() {
  g_savedViews.clear();
  g_selectedViewIndex = -1;
  g_lastStatus = "Views cleared";
}

void SetViews(std::vector<SavedView> views) {
  g_savedViews = std::move(views);
  g_selectedViewIndex = g_savedViews.empty() ? -1 : 0;
  g_lastStatus = g_savedViews.empty() ? "No saved views" : "Saved views loaded";
}

} // namespace SavedViews
