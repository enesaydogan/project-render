#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "ibl_manager.h"
#include "d3d12_helpers.h"
#include "dxc_wrapper.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

// Simple analytic approximation of CIE 1931 color matching functions.
inline float Gaussian(float x, float alpha, float mu, float sigma1,
                      float sigma2) {
  float t = (x - mu) / (x < mu ? sigma1 : sigma2);
  return alpha * std::exp(-0.5f * t * t);
}

inline float cieX(float lambda) {
  return Gaussian(lambda, 1.056f, 599.8f, 37.9f, 31.0f) +
         Gaussian(lambda, 0.362f, 442.0f, 16.0f, 26.7f) +
         Gaussian(lambda, -0.065f, 501.1f, 20.4f, 26.2f);
}

inline float cieY(float lambda) {
  return Gaussian(lambda, 0.821f, 568.8f, 46.9f, 40.5f) +
         Gaussian(lambda, 0.286f, 530.9f, 16.3f, 31.1f);
}

inline float cieZ(float lambda) {
  return Gaussian(lambda, 1.217f, 437.0f, 11.8f, 36.0f) +
         Gaussian(lambda, 0.681f, 459.0f, 26.0f, 13.8f);
}

// Convert XYZ to RGB (Linear sRGB / Rec.709)
inline void XYZtoRGB(float x, float y, float z, float &r, float &g, float &b) {
  r = 3.2404542f * x - 1.5371385f * y - 0.4985314f * z;
  g = -0.9692660f * x + 1.8760108f * y + 0.0415560f * z;
  b = 0.0556434f * x - 0.2040259f * y + 1.0572252f * z;
}

bool IBLManager::Initialize(ID3D12Device *device, ID3D12CommandQueue *queue) {
  m_device = device;
  m_queue = queue;
  return true;
}

void IBLManager::SetPhysicalCalibrationEnabled(bool enabled) {
  if (m_physicalCalibrationEnabled == enabled) {
    return;
  }

  m_physicalCalibrationEnabled = enabled;
  if (m_physicalCalibrationEnabled) {
    m_skyIntensity = kPhysicalSkyIntensity;
    m_sunIntensity = kPhysicalSunIntensityLux;
    m_sunSize = kPhysicalSunSizeDeg;
    m_skyDirty = true;
  }
}

bool IBLManager::LoadEnvironmentMap(const std::string &path) {
  if (!m_device)
    return false;

  bool isHDR = false;
  if (path.find(".hdr") != std::string::npos ||
      path.find(".exr") != std::string::npos) {
    isHDR = true;
  }

  Asset::Texture tex = Asset::LoadTextureFromFile(path, isHDR);
  if (!tex.resource) {
    std::cerr << "Failed to load environment map: " << path << std::endl;
    return false;
  }

  std::cout << "Loaded environment map: " << path << " (" << tex.width << "x"
            << tex.height << ")" << std::endl;

  m_fileTexture = tex;

  // Switching to file-based IBL automatically sets the source and disables the
  // analytic sun.  The sun parameters are cached so we can restore them when
  // the user returns to a procedural sky model.
  if (!m_savedSunValid) {
    m_savedSunIntensity = m_sunIntensity;
    m_savedSunSize = m_sunSize;
    m_savedSunValid = true;
  }
  m_sunIntensity = 0.0f;
  m_sunSize = 0.0f;

  m_source = IBLSource::File;
  m_envMap = m_fileTexture;
  BuildEnvironmentImportanceTextures(m_envMap);

  CreateDescriptor();
  return true;
}

bool IBLManager::InitializeSkyModel(const std::string &datasetPath) {
  m_pragueSkyModel = std::make_unique<PragueSkyModel>();
  try {
    m_pragueSkyModel->initialize(datasetPath);
    std::cout << "PragueSkyModel initialized from " << datasetPath << std::endl;
    m_skyInitialized = true;
    m_skyDirty = true;
    // Default update
    UpdateSkyModel();
    return true;
  } catch (const std::exception &e) {
    std::cerr << "Failed to initialize PragueSkyModel: " << e.what()
              << std::endl;
    m_skyInitialized = false;
    return false;
  }
}

void IBLManager::UpdateSkyModel() {
  if (m_source == IBLSource::PragueSkyModel && m_skyDirty && m_skyInitialized) {
    UpdateTextureFromSkyModel();
    m_envMap = m_proceduralTexture;
    BuildEnvironmentImportanceTextures(m_envMap);
    CreateDescriptor();
    m_skyDirty = false;
  }
}

void IBLManager::SetIBLSource(IBLSource source) {
  if (m_source != source) {
    // if we are leaving file-based IBL, restore cached sun parameters
    if (m_source == IBLSource::File && m_savedSunValid &&
        !m_physicalCalibrationEnabled) {
      m_sunIntensity = m_savedSunIntensity;
      m_sunSize = m_savedSunSize;
      m_savedSunValid = false;
    }

    m_source = source;
    if (m_source == IBLSource::File) {
      // backup current sun values (only once)
      if (!m_savedSunValid) {
        m_savedSunIntensity = m_sunIntensity;
        m_savedSunSize = m_sunSize;
        m_savedSunValid = true;
      }
      // mute the analytic sun, the env map may already contain sun lighting
      m_sunIntensity = 0.0f;
      m_sunSize = 0.0f;

      if (m_fileTexture.resource) {
        m_envMap = m_fileTexture;
        BuildEnvironmentImportanceTextures(m_envMap);
        CreateDescriptor();
      } else {
        std::cerr << "Cannot switch to File IBL: no file loaded." << std::endl;
        m_source = IBLSource::PragueSkyModel; // revert
      }
    } else {
      if (m_skyInitialized) {
        if (m_physicalCalibrationEnabled) {
          m_skyIntensity = kPhysicalSkyIntensity;
          m_sunIntensity = kPhysicalSunIntensityLux;
          m_sunSize = kPhysicalSunSizeDeg;
        }
        if (m_skyDirty || !m_proceduralTexture.resource) {
          UpdateTextureFromSkyModel();
        }
        m_envMap = m_proceduralTexture;
        BuildEnvironmentImportanceTextures(m_envMap);
        CreateDescriptor();
      } else {
        std::cerr << "Cannot switch to Sky Model: not initialized."
                  << std::endl;
        m_source = IBLSource::File; // revert
      }
    }
  }
}

void IBLManager::CreateDescriptor() {
  if (m_cpuHandle.ptr != 0 && m_envMap.resource) {
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = m_envMap.format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = m_envMap.mipLevels;
    srvDesc.Texture2D.MostDetailedMip = 0;

    m_device->CreateShaderResourceView(m_envMap.resource.Get(), &srvDesc,
                                       m_cpuHandle);
  }
}

// Helper to create a texture resource from data (RGBA32Float)
static bool CreateTexFromData(ID3D12Device *device, ID3D12CommandQueue *queue,
                              UINT width, UINT height,
                              const std::vector<float> &data,
                              Asset::Texture &outTex) {
  if (data.size() != width * height * 4)
    return false;

  D3D12_HEAP_PROPERTIES heapProps = {};
  heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

  D3D12_RESOURCE_DESC texDesc = {};
  texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  texDesc.Alignment = 0;
  texDesc.Width = width;
  texDesc.Height = height;
  texDesc.DepthOrArraySize = 1;
  texDesc.MipLevels = 1;
  texDesc.Format =
      DXGI_FORMAT_R32G32B32_FLOAT; // Note: Use R32G32B32 for IBL if supported,
                                   // but usually A32 is better aligned.
  // Actually source is 4 floats. R32G32B32A32_FLOAT
  texDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;

  texDesc.SampleDesc.Count = 1;
  texDesc.SampleDesc.Quality = 0;
  texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

  ComPtr<ID3D12Resource> texture;
  HRESULT hr = device->CreateCommittedResource(
      &heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
      D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture));
  if (FAILED(hr)) {
    std::cerr << "CreateTexFromData: Failed to create texture resource. HR=0x"
              << std::hex << hr << std::dec << std::endl;
    return false;
  }

  // Calculate footprint
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
  UINT numRows = 0;
  UINT64 rowSizeInBytes = 0;
  UINT64 totalBytes = 0;
  device->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, &numRows,
                                &rowSizeInBytes, &totalBytes);

  // Upload Buffer
  D3D12_HEAP_PROPERTIES uploadHeapProps = {};
  uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

  D3D12_RESOURCE_DESC bufferDesc = {};
  bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  bufferDesc.Width = totalBytes;
  bufferDesc.Height = 1;
  bufferDesc.DepthOrArraySize = 1;
  bufferDesc.MipLevels = 1;
  bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
  bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
  bufferDesc.SampleDesc.Count = 1;
  bufferDesc.SampleDesc.Quality = 0;

  ComPtr<ID3D12Resource> uploadBuffer;
  hr = device->CreateCommittedResource(
      &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer));
  if (FAILED(hr)) {
    std::cerr << "CreateTexFromData: Failed to create upload buffer. HR=0x"
              << std::hex << hr << std::dec << std::endl;
    return false;
  }

  // Map and Copy
  BYTE *pData = nullptr;
  // Map with empty range reading
  D3D12_RANGE readRange = {0, 0};
  hr = uploadBuffer->Map(0, &readRange, (void **)&pData);
  if (FAILED(hr))
    return false;

  // Source Row Pitch is usually packed: width * 16 bytes
  UINT srcRowPitch = width * 16;

  for (UINT y = 0; y < height; ++y) {
    BYTE *destRow = pData + footprint.Offset + y * footprint.Footprint.RowPitch;
    const BYTE *srcRow = (const BYTE *)data.data() + y * srcRowPitch;
    memcpy(destRow, srcRow, srcRowPitch);
  }

  uploadBuffer->Unmap(0, nullptr);

  // Copy Command
  ComPtr<ID3D12CommandAllocator> cmdAlloc;
  device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                 IID_PPV_ARGS(&cmdAlloc));
  ComPtr<ID3D12GraphicsCommandList> cmdList;
  device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc.Get(),
                            nullptr, IID_PPV_ARGS(&cmdList));

  D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
  dstLoc.pResource = texture.Get();
  dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  dstLoc.SubresourceIndex = 0;

  D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
  srcLoc.pResource = uploadBuffer.Get();
  srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  srcLoc.PlacedFootprint = footprint;

  cmdList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = texture.Get();
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  cmdList->ResourceBarrier(1, &barrier);

  cmdList->Close();
  ID3D12CommandList *lists[] = {cmdList.Get()};
  queue->ExecuteCommandLists(1, lists);

  // Simple sync
  ComPtr<ID3D12Fence> fence;
  device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
  HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  queue->Signal(fence.Get(), 1);
  fence->SetEventOnCompletion(1, event);
  WaitForSingleObject(event, INFINITE);
  CloseHandle(event);

  outTex.resource = texture;
  outTex.width = width;
  outTex.height = height;
  outTex.mipLevels = 1;
  outTex.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
  outTex.cpuData.assign(reinterpret_cast<const uint8_t *>(data.data()),
                        reinterpret_cast<const uint8_t *>(data.data()) +
                            data.size() * sizeof(float));

  std::cout << "CreateTexFromData: Texture uploaded successfully. " << width
            << "x" << height << std::endl;

  return true;
}

bool IBLManager::BuildEnvironmentImportanceTextures(const Asset::Texture &envTex) {
  if (!m_device || !m_queue || !envTex.resource || envTex.width == 0 ||
      envTex.height == 0 || envTex.cpuData.empty()) {
    m_envConditionalCdf = {};
    m_envMarginalCdf = {};
    return false;
  }

  const UINT width = envTex.width;
  const UINT height = envTex.height;
  const double pi = 3.14159265358979323846;

  const bool isFloatRGBA =
      envTex.format == DXGI_FORMAT_R32G32B32A32_FLOAT &&
      envTex.cpuData.size() >= (size_t)width * (size_t)height * 16;
  const bool isUNormRGBA =
      envTex.format == DXGI_FORMAT_R8G8B8A8_UNORM &&
      envTex.cpuData.size() >= (size_t)width * (size_t)height * 4;

  if (!isFloatRGBA && !isUNormRGBA) {
    m_envConditionalCdf = {};
    m_envMarginalCdf = {};
    return false;
  }

  std::vector<double> texelWeights((size_t)width * (size_t)height, 0.0);
  std::vector<double> rowSums(height, 0.0);

  for (UINT y = 0; y < height; ++y) {
    const double theta = ((double)y + 0.5) / (double)height * pi;
    const double sinTheta = std::max(0.0, std::sin(theta));
    double rowSum = 0.0;

    for (UINT x = 0; x < width; ++x) {
      float r = 0.0f, g = 0.0f, b = 0.0f;
      const size_t idx = ((size_t)y * (size_t)width + (size_t)x);
      if (isFloatRGBA) {
        const float *rgba = reinterpret_cast<const float *>(envTex.cpuData.data());
        const size_t base = idx * 4;
        r = rgba[base + 0];
        g = rgba[base + 1];
        b = rgba[base + 2];
      } else {
        const uint8_t *rgba = envTex.cpuData.data();
        const size_t base = idx * 4;
        r = (float)rgba[base + 0] / 255.0f;
        g = (float)rgba[base + 1] / 255.0f;
        b = (float)rgba[base + 2] / 255.0f;
      }

      const double luminance = std::max(
          0.0, 0.2126 * (double)r + 0.7152 * (double)g + 0.0722 * (double)b);
      const double weight = luminance * sinTheta;
      texelWeights[idx] = weight;
      rowSum += weight;
    }

    rowSums[y] = rowSum;
  }

  double totalWeight = 0.0;
  for (UINT y = 0; y < height; ++y) {
    totalWeight += rowSums[y];
  }

  if (totalWeight <= 1e-20) {
    totalWeight = 0.0;
    for (UINT y = 0; y < height; ++y) {
      const double theta = ((double)y + 0.5) / (double)height * pi;
      const double sinTheta = std::max(1e-6, std::sin(theta));
      const double rowWeight = sinTheta * (double)width;
      rowSums[y] = rowWeight;
      totalWeight += rowWeight;
      for (UINT x = 0; x < width; ++x) {
        texelWeights[(size_t)y * (size_t)width + (size_t)x] = sinTheta;
      }
    }
  }

  std::vector<float> conditionalData((size_t)width * (size_t)height * 4, 0.0f);
  std::vector<float> marginalData((size_t)height * 4, 0.0f);

  double marginalAccum = 0.0;
  for (UINT y = 0; y < height; ++y) {
    const double rowSum = rowSums[y];
    double rowAccum = 0.0;

    for (UINT x = 0; x < width; ++x) {
      const size_t texelIdx = (size_t)y * (size_t)width + (size_t)x;
      const double w = texelWeights[texelIdx];
      rowAccum += w;

      const float condCdf = (rowSum > 1e-20)
                                ? (float)std::clamp(rowAccum / rowSum, 0.0, 1.0)
                                : (float)(x + 1) / (float)width;
      const float pmf =
          (totalWeight > 1e-20) ? (float)std::max(0.0, w / totalWeight) : 0.0f;

      const size_t outBase = texelIdx * 4;
      conditionalData[outBase + 0] = condCdf;
      conditionalData[outBase + 1] = pmf;
      conditionalData[outBase + 2] = 0.0f;
      conditionalData[outBase + 3] = 1.0f;
    }

    marginalAccum += rowSum;
    const float marginalCdf =
        (totalWeight > 1e-20)
            ? (float)std::clamp(marginalAccum / totalWeight, 0.0, 1.0)
            : (float)(y + 1) / (float)height;
    const size_t mBase = (size_t)y * 4;
    marginalData[mBase + 0] = marginalCdf;
    marginalData[mBase + 1] = 0.0f;
    marginalData[mBase + 2] = 0.0f;
    marginalData[mBase + 3] = 1.0f;
  }

  conditionalData[((size_t)height * (size_t)width - 1) * 4 + 0] = 1.0f;
  marginalData[((size_t)height - 1) * 4 + 0] = 1.0f;

  if (!CreateTexFromData(m_device.Get(), m_queue.Get(), width, height,
                         conditionalData, m_envConditionalCdf)) {
    m_envConditionalCdf = {};
    m_envMarginalCdf = {};
    return false;
  }

  if (!CreateTexFromData(m_device.Get(), m_queue.Get(), height, 1,
                         marginalData, m_envMarginalCdf)) {
    m_envConditionalCdf = {};
    m_envMarginalCdf = {};
    return false;
  }

  return true;
}

void IBLManager::UpdateTextureFromSkyModel() {
  if (!m_skyInitialized)
    return;

  const UINT width = 256 * 2;
  const UINT height = 128 * 2;
  std::vector<float> pixels(width * height * 4);

  // Altitude should be in meters [0, 15000]. Origin is at sea level.
  PragueSkyModel::Vector3 viewpoint = {0.0, 0.0, (double)m_altitude};
  PragueSkyModel::Vector3 sunDir = {
      std::cos(m_solarAzimuth) * std::cos(m_solarElevation),
      std::sin(m_solarAzimuth) * std::cos(m_solarElevation),
      std::sin(m_solarElevation)};

  const double PI = 3.14159265358979323846;
  const float startLambda = 380.0f;
  const float endLambda = 780.0f;
  const float stepLambda = 20.0f;

  // Parallelize generation across rows to utilize multiple CPU cores.
  unsigned hwThreads = std::thread::hardware_concurrency();
  if (hwThreads == 0)
    hwThreads = 1;
  unsigned numThreads = (unsigned)std::min<unsigned>(hwThreads, height);
  unsigned chunk = (height + numThreads - 1) / numThreads;

  // Capture frequently used values locally for thread safety and perf
  auto skyModel = m_pragueSkyModel.get();
  const double solarElev = m_solarElevation;
  const double solarAzi = m_solarAzimuth;
  const double visibility = m_visibility;
  const double albedo = m_albedo;
  const float skyIntensity =
      m_physicalCalibrationEnabled ? kPhysicalSkyIntensity : m_skyIntensity;

  double globalSkyLumSum = 0.0;
  double globalSkyLumMax = 0.0;
  double globalHorizonLumSum = 0.0;
  unsigned long long globalSkyLumCount = 0;
  unsigned long long globalHorizonLumCount = 0;
  std::mutex statsMutex;

  std::vector<std::thread> threads;
  threads.reserve(numThreads);

  for (unsigned t = 0; t < numThreads; ++t) {
    int y0 = (int)std::min<unsigned>(t * chunk, height);
    int y1 = (int)std::min<unsigned>((t + 1) * chunk, height);
    threads.emplace_back([=, &pixels, &globalSkyLumSum, &globalSkyLumMax,
                          &globalHorizonLumSum, &globalSkyLumCount,
                          &globalHorizonLumCount, &statsMutex]() {
      double localSkyLumSum = 0.0;
      double localSkyLumMax = 0.0;
      double localHorizonLumSum = 0.0;
      unsigned long long localSkyLumCount = 0;
      unsigned long long localHorizonLumCount = 0;

      for (int y = y0; y < y1; ++y) {
        for (int x = 0; x < (int)width; ++x) {
          float u = (float)x / (float)width;
          float v = (float)y / (float)height;

          float theta = v * (float)PI;
          float phi = u * 2.0f * (float)PI;

          double dy = std::cos(theta);
          double sinTheta = std::sin(theta);
          double dx = sinTheta * -std::sin(phi);
          double dz = sinTheta * -std::cos(phi);

          PragueSkyModel::Vector3 viewDir = {dx, dz, dy};

          // For lower hemisphere, mirror the direction across the horizon so
          // we can derive physically plausible ground illumination from sky
          // radiance instead of using a black/constant placeholder.
          PragueSkyModel::Vector3 sampleDir = viewDir;
          if (sampleDir.z < 0.0) {
            sampleDir.z = -sampleDir.z;
          }

          auto params = skyModel->computeParameters(
              viewpoint, sampleDir, solarElev, solarAzi, visibility, albedo);

          float X = 0, Y = 0, Z = 0;
          for (float l = startLambda; l <= endLambda; l += stepLambda) {
            double rad = skyModel->skyRadiance(params, l);
            // Convert spectral radiance (W/sr/m2/nm) to physical luminance
            // (cd/m2) using K_m = 683.0 lm/W
            float val = (float)rad * skyIntensity * 683.0f;
            X += val * cieX(l) * stepLambda;
            Y += val * cieY(l) * stepLambda;
            Z += val * cieZ(l) * stepLambda;
          }

          float r, g, b;
          XYZtoRGB(X, Y, Z, r, g, b);

          // Keep physical luminance diagnostics from the original sky model
          // output (before lower hemisphere replacement).
          if (dy >= 0.0) {
            float lum = (std::max)(0.0f, Y);
            localSkyLumSum += lum;
            localSkyLumMax = (std::max)(localSkyLumMax, (double)lum);
            ++localSkyLumCount;
            if (dy < 0.1) {
              localHorizonLumSum += lum;
              ++localHorizonLumCount;
            }
          }

          // Lower hemisphere: approximate earth as Lambertian reflector lit by
          // sky dome. This keeps ground values in a realistic range relative to
          // the sky and avoids exposure bias from a pitch-black nadir.
          if (dy < 0.0) {
            const float PI_F = 3.14159265359f;
            const float groundAlbedo = std::clamp((float)albedo, 0.02f, 0.95f);
            const float reflectance = groundAlbedo / PI_F;

            // Earth/soil tint blended with neutral so user albedo still drives
            // brightness while color remains plausible outdoors.
            const float earthR = 0.36f;
            const float earthG = 0.30f;
            const float earthB = 0.24f;
            const float tintStrength = 0.65f;

            float gr = std::max(0.0f, r) * reflectance *
                       ((1.0f - tintStrength) + tintStrength * earthR);
            float gg = std::max(0.0f, g) * reflectance *
                       ((1.0f - tintStrength) + tintStrength * earthG);
            float gb = std::max(0.0f, b) * reflectance *
                       ((1.0f - tintStrength) + tintStrength * earthB);

            // Smooth blend around the horizon to avoid a hard seam.
            float horizonBlend = std::clamp((float)(-dy) / 0.08f, 0.0f, 1.0f);
            r = r * (1.0f - horizonBlend) + gr * horizonBlend;
            g = g * (1.0f - horizonBlend) + gg * horizonBlend;
            b = b * (1.0f - horizonBlend) + gb * horizonBlend;
          }

          if (x == (int)(width / 2) && y == (int)(height / 2)) {
            std::cout << "[DEBUG] Center RGB: " << r << ", " << g << ", " << b
                      << " (Unscaled X:" << X << " Y:" << Y << ")" << std::endl;
          }

          pixels[(y * width + x) * 4 + 0] = std::max(0.0f, r);
          pixels[(y * width + x) * 4 + 1] = std::max(0.0f, g);
          pixels[(y * width + x) * 4 + 2] = std::max(0.0f, b);
          pixels[(y * width + x) * 4 + 3] = 1.0f;
        }
      }

      {
        std::lock_guard<std::mutex> lock(statsMutex);
        globalSkyLumSum += localSkyLumSum;
        globalSkyLumMax = (std::max)(globalSkyLumMax, localSkyLumMax);
        globalHorizonLumSum += localHorizonLumSum;
        globalSkyLumCount += localSkyLumCount;
        globalHorizonLumCount += localHorizonLumCount;
      }
    });
  }

  for (auto &th : threads)
    th.join();

  m_dbgSkyAvgLuminanceCdM2 =
      (globalSkyLumCount > 0)
          ? (float)(globalSkyLumSum / (double)globalSkyLumCount)
          : 0.0f;
  m_dbgSkyHorizonLuminanceCdM2 =
      (globalHorizonLumCount > 0)
          ? (float)(globalHorizonLumSum / (double)globalHorizonLumCount)
          : 0.0f;
  m_dbgSkyMaxLuminanceCdM2 = (float)globalSkyLumMax;

  CreateTexFromData(m_device.Get(), m_queue.Get(), width, height, pixels,
                    m_proceduralTexture);
}

DirectX::XMFLOAT3 IBLManager::GetSunColor() const {
  if (!m_skyInitialized || !m_pragueSkyModel) {
    return {1.0f, 1.0f, 1.0f};
  }

  PragueSkyModel::Vector3 viewpoint = {0.0, 0.0, (double)m_altitude};
  PragueSkyModel::Vector3 sunDir = {
      std::cos(m_solarAzimuth) * std::cos(m_solarElevation),
      std::sin(m_solarAzimuth) * std::cos(m_solarElevation),
      std::sin(m_solarElevation)};

  auto params = m_pragueSkyModel->computeParameters(
      viewpoint, sunDir, m_solarElevation, m_solarAzimuth, m_visibility,
      m_albedo);

  const float startLambda = 380.0f;
  const float endLambda = 780.0f;
  const float stepLambda = 20.0f;

  float X = 0, Y = 0, Z = 0;

  for (float l = startLambda; l <= endLambda; l += stepLambda) {
    double rad = m_pragueSkyModel->sunRadiance(params, l);
    // Convert spectral irradiance (W/m2/nm) to physical illuminance (Lux)
    // using K_m = 683.0 lm/W
    float val = (float)rad * 683.0f;
    X += val * cieX(l) * stepLambda;
    Y += val * cieY(l) * stepLambda;
    Z += val * cieZ(l) * stepLambda;
  }

  float r, g, b;
  XYZtoRGB(X, Y, Z, r, g, b);

  // Return normalized color (chromaticity).
  // The absolute intensity (Y) will be handled by the Sun Intensity slider in
  // Lux.
  float mag = std::max(1e-6f, Y);
  return {r / mag, g / mag, b / mag};
}
