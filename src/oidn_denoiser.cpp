#include "oidn_denoiser.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cassert>
#include <cstring>
#include <utility>
#include <vector>
#include <stdexcept>

#ifdef USE_OIDN
#include <OpenImageDenoise/oidn.hpp>
#endif

using Microsoft::WRL::ComPtr;

#ifdef USE_OIDN
struct OidnInputStats {
  uint64_t pixels = 0;
  uint64_t nonFinite = 0;
  uint64_t negative = 0;
  uint64_t clamped = 0;
  uint64_t zeroAlbedo = 0;
  uint64_t defaultUpNormal = 0;
  uint64_t normalOutOfRange = 0;
  float maxRgb = 0.0f;
  float p995Luminance = 0.0f;
  float maxAbsNormal = 0.0f;
};

static float HalfToFloat(uint16_t h) {
  const uint32_t sign = (uint32_t)(h & 0x8000) << 16;
  uint32_t exp = (h >> 10) & 0x1f;
  uint32_t mant = h & 0x03ff;
  uint32_t bits = 0;

  if (exp == 0) {
    if (mant == 0) {
      bits = sign;
    } else {
      int e = -14;
      while ((mant & 0x0400) == 0) {
        mant <<= 1;
        --e;
      }
      mant &= 0x03ff;
      bits = sign | (uint32_t)(e + 127) << 23 | (mant << 13);
    }
  } else if (exp == 0x1f) {
    bits = sign | 0x7f800000u | (mant << 13);
  } else {
    bits = sign | ((exp + 112u) << 23) | (mant << 13);
  }

  float value = 0.0f;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

static uint16_t FloatToHalf(float value) {
  uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));

  const uint32_t sign = (bits >> 16) & 0x8000u;
  int exp = (int)((bits >> 23) & 0xffu) - 127 + 15;
  uint32_t mant = bits & 0x007fffffu;

  if (exp <= 0) {
    if (exp < -10) {
      return (uint16_t)sign;
    }
    mant = (mant | 0x00800000u) >> (uint32_t)(1 - exp);
    return (uint16_t)(sign | ((mant + 0x00001000u) >> 13));
  }

  if (exp >= 31) {
    return (uint16_t)(sign | 0x7bffu);
  }

  uint32_t half = sign | ((uint32_t)exp << 10) |
                  ((mant + 0x00001000u) >> 13);
  if ((half & 0x03ffu) == 0x0400u) {
    half += 0x0400u;
    half &= ~0x03ffu;
  }
  return (uint16_t)half;
}

static OidnInputStats SanitizeOidnHalf4Rows(uint8_t* data, uint32_t width,
                                            uint32_t height,
                                            size_t rowPitchBytes, int kind) {
  OidnInputStats stats = {};
  if (!data || width == 0 || height == 0)
    return stats;

  std::array<uint64_t, 256> luminanceHistogram = {};
  uint64_t luminanceSamples = 0;
  constexpr float kLogLumMin = -16.0f;
  constexpr float kLogLumMax = 16.0f;
  constexpr float kLogLumScale =
      255.0f / (kLogLumMax - kLogLumMin);

  for (uint32_t y = 0; y < height; ++y) {
    uint16_t* row = reinterpret_cast<uint16_t*>(data + (size_t)y * rowPitchBytes);
    for (uint32_t x = 0; x < width; ++x) {
      ++stats.pixels;
      uint16_t* px = row + (size_t)x * 4;
      if (kind == 0) {
        float rgb[3] = {};
        for (int c = 0; c < 3; ++c) {
          float v = HalfToFloat(px[c]);
          if (std::isfinite(v)) {
            stats.maxRgb = (std::max)(stats.maxRgb, v);
          }
          if (!std::isfinite(v) || v < 0.0f) {
            stats.nonFinite += !std::isfinite(v) ? 1 : 0;
            stats.negative += std::isfinite(v) && v < 0.0f ? 1 : 0;
            v = 0.0f;
          }
          if (v > 65504.0f) {
            ++stats.clamped;
          }
          px[c] = FloatToHalf((std::min)(v, 65504.0f));
          rgb[c] = (std::min)(v, 65504.0f);
        }
        const float luminance =
            0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2];
        if (std::isfinite(luminance) && luminance > 0.0f) {
          const float logLum = std::log2(luminance);
          const int bin = std::clamp(
              (int)((logLum - kLogLumMin) * kLogLumScale), 0, 255);
          ++luminanceHistogram[(size_t)bin];
          ++luminanceSamples;
        }
      } else if (kind == 1) {
        bool allNearZero = true;
        for (int c = 0; c < 3; ++c) {
          float v = HalfToFloat(px[c]);
          if (std::isfinite(v)) {
            stats.maxRgb = (std::max)(stats.maxRgb, v);
          }
          if (!std::isfinite(v)) {
            ++stats.nonFinite;
            v = 0.0f;
          }
          if (v < 0.0f || v > 1.0f) {
            ++stats.clamped;
          }
          allNearZero = allNearZero && std::abs(v) < 1.0e-4f;
          px[c] = FloatToHalf(std::clamp(v, 0.0f, 1.0f));
        }
        stats.zeroAlbedo += allNearZero ? 1 : 0;
      } else {
        float n[3] = {HalfToFloat(px[0]), HalfToFloat(px[1]),
                      HalfToFloat(px[2])};
        if (!std::isfinite(n[0]) || !std::isfinite(n[1]) ||
            !std::isfinite(n[2])) {
          ++stats.nonFinite;
          n[0] = 0.0f;
          n[1] = 1.0f;
          n[2] = 0.0f;
        }
        bool defaultUp = std::abs(n[0]) < 1.0e-4f &&
                         std::abs(n[1] - 1.0f) < 1.0e-4f &&
                         std::abs(n[2]) < 1.0e-4f;
        stats.defaultUpNormal += defaultUp ? 1 : 0;
        for (int c = 0; c < 3; ++c) {
          stats.maxAbsNormal = (std::max)(stats.maxAbsNormal, std::abs(n[c]));
          if (n[c] < -1.0f || n[c] > 1.0f) {
            ++stats.normalOutOfRange;
          }
          px[c] = FloatToHalf(std::clamp(n[c], -1.0f, 1.0f));
        }
      }
    }
  }
  if (kind == 0 && luminanceSamples > 0) {
    const uint64_t target = (luminanceSamples * 995ull + 999ull) / 1000ull;
    uint64_t running = 0;
    for (size_t i = 0; i < luminanceHistogram.size(); ++i) {
      running += luminanceHistogram[i];
      if (running >= target) {
        const float t = (float)i / 255.0f;
        const float logLum = kLogLumMin + t * (kLogLumMax - kLogLumMin);
        stats.p995Luminance = std::pow(2.0f, logLum);
        break;
      }
    }
  }
  return stats;
}
#endif

OidnDenoiser::OidnDenoiser() {}
OidnDenoiser::~OidnDenoiser() { Shutdown(); }

bool OidnDenoiser::Initialize(ID3D12Device *device) {
  if (!device) return false;
  m_device = device;
#ifdef USE_OIDN
  if (m_oidnAvailable) return true;

  try {
    LUID adapterLuid = device->GetAdapterLuid();
    oidn::LUID oidnLuid;
    memcpy(oidnLuid.bytes, &adapterLuid, 8);
    oidn::DeviceRef dev = oidn::newDevice(oidnLuid);
    if (!dev) {
        fprintf(stderr, "OidnDenoiser: Warning - newDevice(LUID) failed. Falling back to default device.\n");
        dev = oidn::newDevice(oidn::DeviceType::Default);
    }
    dev.set("verbose", 2); // Enable verbose logging
    dev.commit();
    m_oidnDevice = new oidn::DeviceRef(dev);
    m_oidnAvailable = true;
    
    // Check for GPU interop support
    int memTypes = dev.get<int>("externalMemoryTypes");
    m_gpuBackendAvailable = (memTypes & (int)oidn::ExternalMemoryTypeFlag::D3D12Resource) != 0;

    fprintf(stderr, "OidnDenoiser: OIDN initialized. GPU Zero-copy: %s\n", 
            m_gpuBackendAvailable ? "Supported" : "Not supported (CPU fallback will be used)");
    
    // Create fence for synchronization
    if (m_gpuBackendAvailable) {
        if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)))) {
            m_gpuBackendAvailable = false;
        } else {
            m_fenceValue = 1;
            m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            
            device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_cmdAlloc));
            device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_cmdAlloc.Get(), nullptr, IID_PPV_ARGS(&m_cmdList));
            m_cmdList->Close(); // Start closed
        }
    }
  } catch (const std::exception& e) {
    m_oidnAvailable = false;
    m_oidnDevice = nullptr;
    fprintf(stderr, "OidnDenoiser: Initialization failed: %s\n", e.what());
  }
#endif
  m_initialized = true;
  return true;
}

void OidnDenoiser::Shutdown() {
#ifdef USE_OIDN
  if (m_oidnFilter) {
    delete static_cast<oidn::FilterRef*>(m_oidnFilter);
    m_oidnFilter = nullptr;
  }
  if (m_oidnCpuFilter) {
    delete static_cast<oidn::FilterRef*>(m_oidnCpuFilter);
    m_oidnCpuFilter = nullptr;
  }
  if (m_oidnColorBuf) {
    delete static_cast<oidn::BufferRef*>(m_oidnColorBuf);
    m_oidnColorBuf = nullptr;
  }
  if (m_oidnAlbedoBuf) {
    delete static_cast<oidn::BufferRef*>(m_oidnAlbedoBuf);
    m_oidnAlbedoBuf = nullptr;
  }
  if (m_oidnNormalBuf) {
    delete static_cast<oidn::BufferRef*>(m_oidnNormalBuf);
    m_oidnNormalBuf = nullptr;
  }
  if (m_oidnOutputBuf) {
    delete static_cast<oidn::BufferRef*>(m_oidnOutputBuf);
    m_oidnOutputBuf = nullptr;
  }
  if (m_oidnDevice) {
    delete static_cast<oidn::DeviceRef*>(m_oidnDevice);
    m_oidnDevice = nullptr;
  }
  if (m_oidnCpuDevice) {
    delete static_cast<oidn::DeviceRef*>(m_oidnCpuDevice);
    m_oidnCpuDevice = nullptr;
  }
  
  m_linearColor.Reset();
  m_linearAlbedo.Reset();
  m_linearNormal.Reset();
  m_linearOutput.Reset();
  m_sanitizeReadback.Reset();
  m_sanitizeUpload.Reset();
  m_sanitizeStagingBytes = 0;
  m_depthReadback.Reset();
  m_depthFootprint = {};
  m_depthReadbackBytes = 0;
  m_linearBufferBytes = 0;
  m_oidnInputScale = 1.0f;
  m_cmdAlloc.Reset();
  m_cmdList.Reset();
  m_fence.Reset();
  if (m_fenceEvent) {
      CloseHandle(m_fenceEvent);
      m_fenceEvent = nullptr;
  }

  m_oidnAvailable = false;
  m_gpuBackendAvailable = false;
  m_width = 0;
  m_height = 0;
  m_cpuWidth = 0;
  m_cpuHeight = 0;
  m_cpuInputRowPitchBytes = 0;
  m_cpuSanitizedInput.clear();
  m_cpuSanitizedInputPtr = nullptr;
  m_lastInput = nullptr;
  m_lastAlbedo = nullptr;
  m_lastNormal = nullptr;
  m_lastOutput = nullptr;
#endif
  m_initialized = false;
  m_device = nullptr;
}

bool OidnDenoiser::RunDenoiseHostHalf4(const void* input, uint32_t width,
                                      uint32_t height,
                                      size_t inputRowPitchBytes, void* output,
                                      size_t outputRowPitchBytes) {
  if (!input || !output || width == 0 || height == 0)
    return false;

#ifdef USE_OIDN
  try {
    // Lazily create a CPU device for safe, layout-correct denoising.
    if (!m_oidnCpuDevice) {
      oidn::DeviceRef dev = oidn::newDevice(oidn::DeviceType::CPU);
      dev.commit();
      m_oidnCpuDevice = new oidn::DeviceRef(dev);
    }

    oidn::DeviceRef& dev = *static_cast<oidn::DeviceRef*>(m_oidnCpuDevice);

    const size_t sanitizedBytes = inputRowPitchBytes * (size_t)height;
    m_cpuSanitizedInput.resize(sanitizedBytes);
    uint8_t* sanitized = m_cpuSanitizedInput.data();
    const uint8_t* inBytes = reinterpret_cast<const uint8_t*>(input);
    for (uint32_t y = 0; y < height; ++y) {
      std::memcpy(sanitized + (size_t)y * inputRowPitchBytes,
                  inBytes + (size_t)y * inputRowPitchBytes,
                  (size_t)width * 8);
    }
    (void)SanitizeOidnHalf4Rows(sanitized, width, height,
                                inputRowPitchBytes, 0);

    const bool needsUpdate = (!m_oidnCpuFilter || width != m_cpuWidth ||
                              height != m_cpuHeight ||
                              inputRowPitchBytes != m_cpuInputRowPitchBytes ||
                              sanitized != m_cpuSanitizedInputPtr ||
                              m_quality != m_cpuLastQuality);
    if (needsUpdate) {
      if (m_oidnCpuFilter) {
        delete static_cast<oidn::FilterRef*>(m_oidnCpuFilter);
        m_oidnCpuFilter = nullptr;
      }

      oidn::FilterRef filter = dev.newFilter("RT");
      // Denoise RGB only (Half3) but keep a pixel stride of 8 bytes to match
      // our Half4 layout. This avoids any ambiguity around alpha handling.
      // Provide explicit row strides (mapped readback buffers use padded rows).
      filter.setImage("color", sanitized, oidn::Format::Half3, width, height, 0,
              8, inputRowPitchBytes);
      filter.setImage("output", output, oidn::Format::Half3, width, height, 0,
              8, outputRowPitchBytes);
      filter.set("hdr", true);
      switch (m_quality) {
      case Quality::Fast:
        filter.set("quality", OIDN_QUALITY_FAST);
        break;
      case Quality::Balanced:
        filter.set("quality", OIDN_QUALITY_BALANCED);
        break;
      case Quality::High:
        filter.set("quality", OIDN_QUALITY_HIGH);
        break;
      }
      filter.commit();
      m_oidnCpuFilter = new oidn::FilterRef(filter);
      m_cpuWidth = width;
      m_cpuHeight = height;
      m_cpuInputRowPitchBytes = inputRowPitchBytes;
      m_cpuSanitizedInputPtr = sanitized;
      m_cpuLastQuality = m_quality;
    }

    // Preserve alpha (and any padding) by copying input->output first.
    // OIDN writes only the first 6 bytes (RGB) per pixel with our stride.
    const uint8_t* inB = reinterpret_cast<const uint8_t*>(input);
    uint8_t* outB = reinterpret_cast<uint8_t*>(output);
    const size_t rowCopyBytes = (size_t)width * 8;
    for (uint32_t y = 0; y < height; ++y) {
      memcpy(outB + (size_t)y * outputRowPitchBytes,
             inB + (size_t)y * inputRowPitchBytes,
             rowCopyBytes);
    }

    oidn::FilterRef& filter = *static_cast<oidn::FilterRef*>(m_oidnCpuFilter);
    filter.execute();
    dev.sync();
    return true;
  } catch (const std::exception& e) {
    fprintf(stderr, "OidnDenoiser: CPU denoise failed: %s\n", e.what());
    return false;
  }
#else
  (void)input;
  (void)width;
  (void)height;
  (void)inputRowPitchBytes;
  (void)output;
  (void)outputRowPitchBytes;
  return false;
#endif
}

// Helper: create a temporary shared handle for a D3D12 resource. Caller must
// CloseHandle() the returned handle when done.
static inline bool CreateSharedHandleForResource(ID3D12Device *device,
                                                 ID3D12Resource *res,
                                                 HANDLE &outHandle) {
  if (!device || !res) return false;
  outHandle = nullptr;
  HRESULT hr = device->CreateSharedHandle(res, nullptr, GENERIC_ALL, nullptr, &outHandle);
  if (FAILED(hr) || !outHandle) {
    fprintf(stderr, "OidnDenoiser: CreateSharedHandle failed 0x%08x\n", hr);
    return false;
  }
  return true;
}

static inline size_t GetD3D12ResourceAllocationSizeBytes(ID3D12Device* device,
                                                         ID3D12Resource* res) {
  if (!device || !res) return 0;
  const D3D12_RESOURCE_DESC desc = res->GetDesc();
  const D3D12_RESOURCE_ALLOCATION_INFO info =
      device->GetResourceAllocationInfo(0, 1, &desc);
  return (size_t)info.SizeInBytes;
}

static inline bool IsSupportedOidnInteropTexture(const D3D12_RESOURCE_DESC& desc) {
  // This wrapper currently assumes a 2D RGBA16F texture.
  // If you switch formats, update setImage() Format and validation.
  return desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
         desc.MipLevels == 1 &&
         desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT;
}

#ifdef USE_OIDN
bool OidnDenoiser::EnsureSanitizeStaging(uint64_t byteSize) {
  if (!m_device || byteSize == 0)
    return false;
  if (m_sanitizeReadback && m_sanitizeUpload &&
      m_sanitizeStagingBytes >= byteSize) {
    return true;
  }

  m_sanitizeReadback.Reset();
  m_sanitizeUpload.Reset();
  m_sanitizeStagingBytes = 0;

  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = byteSize;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

  D3D12_HEAP_PROPERTIES readbackHeap = {};
  readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
  HRESULT hr = m_device->CreateCommittedResource(
      &readbackHeap, D3D12_HEAP_FLAG_NONE, &desc,
      D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
      IID_PPV_ARGS(m_sanitizeReadback.GetAddressOf()));
  if (FAILED(hr)) {
    fprintf(stderr, "OidnDenoiser: sanitize readback allocation failed 0x%08x\n",
            (unsigned)hr);
    return false;
  }
  m_sanitizeReadback->SetName(L"OIDN Sanitize Readback");

  D3D12_HEAP_PROPERTIES uploadHeap = {};
  uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
  hr = m_device->CreateCommittedResource(
      &uploadHeap, D3D12_HEAP_FLAG_NONE, &desc,
      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
      IID_PPV_ARGS(m_sanitizeUpload.GetAddressOf()));
  if (FAILED(hr)) {
    fprintf(stderr, "OidnDenoiser: sanitize upload allocation failed 0x%08x\n",
            (unsigned)hr);
    m_sanitizeReadback.Reset();
    return false;
  }
  m_sanitizeUpload->SetName(L"OIDN Sanitize Upload");
  m_sanitizeStagingBytes = byteSize;
  return true;
}

bool OidnDenoiser::SanitizeLinearBufferForOidn(ID3D12CommandQueue* queue,
                                               ID3D12Resource* linearBuffer,
                                               OidnInputKind kind) {
  if (!queue || !linearBuffer || !m_cmdAlloc || !m_cmdList || !m_fence ||
      !m_fenceEvent || m_linearBufferBytes == 0) {
    return false;
  }
  if (!EnsureSanitizeStaging(m_linearBufferBytes))
    return false;

  auto ExecuteAndWait = [&]() -> bool {
    ID3D12CommandList* lists[] = {m_cmdList.Get()};
    queue->ExecuteCommandLists(1, lists);
    HRESULT hr = queue->Signal(m_fence.Get(), m_fenceValue);
    if (FAILED(hr)) {
      fprintf(stderr, "OidnDenoiser: sanitize queue signal failed 0x%08x\n",
              (unsigned)hr);
      return false;
    }
    hr = m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent);
    if (FAILED(hr)) {
      fprintf(stderr, "OidnDenoiser: sanitize fence wait setup failed 0x%08x\n",
              (unsigned)hr);
      return false;
    }
    WaitForSingleObject(m_fenceEvent, INFINITE);
    ++m_fenceValue;
    return true;
  };

  HRESULT hr = m_cmdAlloc->Reset();
  if (FAILED(hr))
    return false;
  hr = m_cmdList->Reset(m_cmdAlloc.Get(), nullptr);
  if (FAILED(hr))
    return false;

  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = linearBuffer;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  m_cmdList->ResourceBarrier(1, &barrier);
  m_cmdList->CopyBufferRegion(m_sanitizeReadback.Get(), 0, linearBuffer, 0,
                              m_linearBufferBytes);
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
  m_cmdList->ResourceBarrier(1, &barrier);
  hr = m_cmdList->Close();
  if (FAILED(hr))
    return false;
  if (!ExecuteAndWait())
    return false;

  void* readbackPtr = nullptr;
  void* uploadPtr = nullptr;
  const D3D12_RANGE readRange = {0, (SIZE_T)m_linearBufferBytes};
  hr = m_sanitizeReadback->Map(0, &readRange, &readbackPtr);
  if (FAILED(hr))
    return false;
  hr = m_sanitizeUpload->Map(0, nullptr, &uploadPtr);
  if (FAILED(hr)) {
    m_sanitizeReadback->Unmap(0, nullptr);
    return false;
  }
  std::memcpy(uploadPtr, readbackPtr, (size_t)m_linearBufferBytes);
  const OidnInputStats stats = SanitizeOidnHalf4Rows(
      reinterpret_cast<uint8_t*>(uploadPtr), m_width, m_height,
      m_linearFootprint.Footprint.RowPitch, static_cast<int>(kind));
  const char* kindName = (kind == OidnInputKind::Color)
                             ? "color"
                             : (kind == OidnInputKind::Albedo) ? "albedo"
                                                               : "normal";
  if (kind == OidnInputKind::Color) {
    const float scaleReference = (std::max)(stats.p995Luminance, 1.0f);
    m_oidnInputScale =
        std::clamp(1.0f / scaleReference, 1.0f / 65504.0f, 1.0f);
    fprintf(stderr,
            "OidnDenoiser: %s stats pixels=%llu nonFinite=%llu "
            "negative=%llu clamped=%llu maxRgb=%.6g p995Lum=%.6g "
            "inputScale=%.9g\n",
            kindName, (unsigned long long)stats.pixels,
            (unsigned long long)stats.nonFinite,
            (unsigned long long)stats.negative,
            (unsigned long long)stats.clamped, stats.maxRgb,
            stats.p995Luminance, m_oidnInputScale);
  } else if (kind == OidnInputKind::Albedo) {
    fprintf(stderr,
            "OidnDenoiser: %s stats pixels=%llu nonFinite=%llu "
            "clamped=%llu zeroAlbedoPixels=%llu maxRgb=%.6g\n",
            kindName, (unsigned long long)stats.pixels,
            (unsigned long long)stats.nonFinite,
            (unsigned long long)stats.clamped,
            (unsigned long long)stats.zeroAlbedo, stats.maxRgb);
  } else {
    fprintf(stderr,
            "OidnDenoiser: %s stats pixels=%llu nonFinite=%llu "
            "outOfRange=%llu defaultUpPixels=%llu maxAbs=%.6g\n",
            kindName, (unsigned long long)stats.pixels,
            (unsigned long long)stats.nonFinite,
            (unsigned long long)stats.normalOutOfRange,
            (unsigned long long)stats.defaultUpNormal, stats.maxAbsNormal);
  }
  const D3D12_RANGE uploadWritten = {0, (SIZE_T)m_linearBufferBytes};
  m_sanitizeUpload->Unmap(0, &uploadWritten);
  m_sanitizeReadback->Unmap(0, nullptr);

  hr = m_cmdAlloc->Reset();
  if (FAILED(hr))
    return false;
  hr = m_cmdList->Reset(m_cmdAlloc.Get(), nullptr);
  if (FAILED(hr))
    return false;

  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
  m_cmdList->ResourceBarrier(1, &barrier);
  m_cmdList->CopyBufferRegion(linearBuffer, 0, m_sanitizeUpload.Get(), 0,
                              m_linearBufferBytes);
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
  m_cmdList->ResourceBarrier(1, &barrier);
  hr = m_cmdList->Close();
  if (FAILED(hr))
    return false;
  return ExecuteAndWait();
}

bool OidnDenoiser::SanitizeLinearInputsForOidn(ID3D12CommandQueue* queue,
                                               bool hasAlbedo,
                                               bool hasNormal) {
  if (!SanitizeLinearBufferForOidn(queue, m_linearColor.Get(),
                                   OidnInputKind::Color)) {
    return false;
  }
  if (hasAlbedo && m_linearAlbedo &&
      !SanitizeLinearBufferForOidn(queue, m_linearAlbedo.Get(),
                                   OidnInputKind::Albedo)) {
    return false;
  }
  if (hasNormal && m_linearNormal &&
      !SanitizeLinearBufferForOidn(queue, m_linearNormal.Get(),
                                   OidnInputKind::Normal)) {
    return false;
  }
  return true;
}

bool OidnDenoiser::RestoreSkyPixelsAfterOidn(ID3D12CommandQueue* queue,
                                             ID3D12Resource* linearDepth) {
  if (!queue || !linearDepth || !m_linearColor || !m_linearOutput ||
      !m_cmdAlloc || !m_cmdList || !m_fence || !m_fenceEvent ||
      m_linearBufferBytes == 0) {
    return false;
  }
  const D3D12_RESOURCE_DESC depthDesc = linearDepth->GetDesc();
  if (depthDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
      depthDesc.Format != DXGI_FORMAT_R32_FLOAT ||
      (uint32_t)depthDesc.Width != m_width ||
      depthDesc.Height != m_height) {
    return false;
  }
  if (!EnsureSanitizeStaging(m_linearBufferBytes)) {
    return false;
  }

  auto ExecuteAndWait = [&]() -> bool {
    ID3D12CommandList* lists[] = {m_cmdList.Get()};
    queue->ExecuteCommandLists(1, lists);
    HRESULT hr = queue->Signal(m_fence.Get(), m_fenceValue);
    if (FAILED(hr))
      return false;
    hr = m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent);
    if (FAILED(hr))
      return false;
    WaitForSingleObject(m_fenceEvent, INFINITE);
    ++m_fenceValue;
    return true;
  };

  UINT64 depthBytes = 0;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT depthFootprint = {};
  m_device->GetCopyableFootprints(&depthDesc, 0, 1, 0, &depthFootprint,
                                  nullptr, nullptr, &depthBytes);
  if (!m_depthReadback || m_depthReadbackBytes < depthBytes) {
    m_depthReadback.Reset();
    D3D12_RESOURCE_DESC rbDesc = {};
    rbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rbDesc.Width = depthBytes;
    rbDesc.Height = 1;
    rbDesc.DepthOrArraySize = 1;
    rbDesc.MipLevels = 1;
    rbDesc.SampleDesc.Count = 1;
    rbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES rbHeap = {};
    rbHeap.Type = D3D12_HEAP_TYPE_READBACK;
    HRESULT hr = m_device->CreateCommittedResource(
        &rbHeap, D3D12_HEAP_FLAG_NONE, &rbDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(m_depthReadback.GetAddressOf()));
    if (FAILED(hr)) {
      fprintf(stderr,
              "OidnDenoiser: depth readback allocation failed 0x%08x\n",
              (unsigned)hr);
      return false;
    }
    m_depthReadback->SetName(L"OIDN Sky Restore Depth Readback");
    m_depthReadbackBytes = depthBytes;
  }
  m_depthFootprint = depthFootprint;

  HRESULT hr = m_cmdAlloc->Reset();
  if (FAILED(hr))
    return false;
  hr = m_cmdList->Reset(m_cmdAlloc.Get(), nullptr);
  if (FAILED(hr))
    return false;

  D3D12_RESOURCE_BARRIER depthBarrier = {};
  depthBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  depthBarrier.Transition.pResource = linearDepth;
  depthBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  depthBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
  depthBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  m_cmdList->ResourceBarrier(1, &depthBarrier);
  D3D12_TEXTURE_COPY_LOCATION depthSrc = {};
  depthSrc.pResource = linearDepth;
  depthSrc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  depthSrc.SubresourceIndex = 0;
  D3D12_TEXTURE_COPY_LOCATION depthDst = {};
  depthDst.pResource = m_depthReadback.Get();
  depthDst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  depthDst.PlacedFootprint = depthFootprint;
  m_cmdList->CopyTextureRegion(&depthDst, 0, 0, 0, &depthSrc, nullptr);
  depthBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
  depthBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
  m_cmdList->ResourceBarrier(1, &depthBarrier);
  hr = m_cmdList->Close();
  if (FAILED(hr) || !ExecuteAndWait())
    return false;

  auto ReadLinearBuffer = [&](ID3D12Resource* source,
                              std::vector<uint8_t>& out) -> bool {
    HRESULT localHr = m_cmdAlloc->Reset();
    if (FAILED(localHr))
      return false;
    localHr = m_cmdList->Reset(m_cmdAlloc.Get(), nullptr);
    if (FAILED(localHr))
      return false;
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = source;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    m_cmdList->ResourceBarrier(1, &b);
    m_cmdList->CopyBufferRegion(m_sanitizeReadback.Get(), 0, source, 0,
                                m_linearBufferBytes);
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
    m_cmdList->ResourceBarrier(1, &b);
    localHr = m_cmdList->Close();
    if (FAILED(localHr) || !ExecuteAndWait())
      return false;

    void* mapped = nullptr;
    const D3D12_RANGE range = {0, (SIZE_T)m_linearBufferBytes};
    localHr = m_sanitizeReadback->Map(0, &range, &mapped);
    if (FAILED(localHr))
      return false;
    out.resize((size_t)m_linearBufferBytes);
    std::memcpy(out.data(), mapped, (size_t)m_linearBufferBytes);
    m_sanitizeReadback->Unmap(0, nullptr);
    return true;
  };

  std::vector<uint8_t> denoisedBytes;
  std::vector<uint8_t> rawColorBytes;
  if (!ReadLinearBuffer(m_linearOutput.Get(), denoisedBytes) ||
      !ReadLinearBuffer(m_linearColor.Get(), rawColorBytes)) {
    return false;
  }

  void* depthMapped = nullptr;
  const D3D12_RANGE depthRange = {0, (SIZE_T)depthBytes};
  hr = m_depthReadback->Map(0, &depthRange, &depthMapped);
  if (FAILED(hr))
    return false;

  float maxDepth = 0.0f;
  for (uint32_t y = 0; y < m_height; ++y) {
    const float* depthRow = reinterpret_cast<const float*>(
        reinterpret_cast<const uint8_t*>(depthMapped) +
        (size_t)y * depthFootprint.Footprint.RowPitch);
    for (uint32_t x = 0; x < m_width; ++x) {
      const float d = depthRow[x];
      if (std::isfinite(d)) {
        maxDepth = (std::max)(maxDepth, d);
      }
    }
  }

  uint64_t restoredPixels = 0;
  if (maxDepth > 0.0f) {
    const float skyDepthThreshold = maxDepth * 0.999f;
    for (uint32_t y = 0; y < m_height; ++y) {
      const float* depthRow = reinterpret_cast<const float*>(
          reinterpret_cast<const uint8_t*>(depthMapped) +
          (size_t)y * depthFootprint.Footprint.RowPitch);
      uint8_t* denoisedRow =
          denoisedBytes.data() + (size_t)y * m_linearFootprint.Footprint.RowPitch;
      const uint8_t* rawRow =
          rawColorBytes.data() + (size_t)y * m_linearFootprint.Footprint.RowPitch;
      for (uint32_t x = 0; x < m_width; ++x) {
        const float d = depthRow[x];
        if (std::isfinite(d) && d >= skyDepthThreshold) {
          std::memcpy(denoisedRow + (size_t)x * 8, rawRow + (size_t)x * 8, 8);
          ++restoredPixels;
        }
      }
    }
  }
  m_depthReadback->Unmap(0, nullptr);

  if (restoredPixels == 0) {
    fprintf(stderr,
            "OidnDenoiser: sky restore found no background pixels "
            "(maxDepth=%.6g).\n",
            maxDepth);
    return true;
  }

  void* uploadMapped = nullptr;
  hr = m_sanitizeUpload->Map(0, nullptr, &uploadMapped);
  if (FAILED(hr))
    return false;
  std::memcpy(uploadMapped, denoisedBytes.data(), (size_t)m_linearBufferBytes);
  const D3D12_RANGE written = {0, (SIZE_T)m_linearBufferBytes};
  m_sanitizeUpload->Unmap(0, &written);

  hr = m_cmdAlloc->Reset();
  if (FAILED(hr))
    return false;
  hr = m_cmdList->Reset(m_cmdAlloc.Get(), nullptr);
  if (FAILED(hr))
    return false;
  D3D12_RESOURCE_BARRIER outBarrier = {};
  outBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  outBarrier.Transition.pResource = m_linearOutput.Get();
  outBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  outBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
  outBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
  m_cmdList->ResourceBarrier(1, &outBarrier);
  m_cmdList->CopyBufferRegion(m_linearOutput.Get(), 0, m_sanitizeUpload.Get(),
                              0, m_linearBufferBytes);
  outBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  outBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
  m_cmdList->ResourceBarrier(1, &outBarrier);
  hr = m_cmdList->Close();
  if (FAILED(hr) || !ExecuteAndWait())
    return false;

  fprintf(stderr,
          "OidnDenoiser: restored %llu sky/background pixels after OIDN "
          "(maxDepth=%.6g).\n",
          (unsigned long long)restoredPixels, maxDepth);
  return true;
}
#endif

bool OidnDenoiser::Prepare(ID3D12Resource *input, ID3D12Resource *albedo,
                           ID3D12Resource *normal,
                           ID3D12Resource *output) {
  if (!input || !output || !m_device)
    return false;

#ifdef USE_OIDN
  if (!m_initialized && !Initialize(m_device))
    return false;
  if (!m_oidnAvailable || !m_gpuBackendAvailable || !m_cmdList)
    return false;

  const D3D12_RESOURCE_DESC inputDesc = input->GetDesc();
  const D3D12_RESOURCE_DESC outputDesc = output->GetDesc();
  if (!IsSupportedOidnInteropTexture(inputDesc) ||
      !IsSupportedOidnInteropTexture(outputDesc) ||
      inputDesc.Width != outputDesc.Width ||
      inputDesc.Height != outputDesc.Height) {
    return false;
  }

  const uint32_t width = (uint32_t)inputDesc.Width;
  const uint32_t height = inputDesc.Height;
  const bool hasAlbedo = albedo != nullptr;
  const bool hasNormal = normal != nullptr;

  try {
    oidn::DeviceRef& dev = *static_cast<oidn::DeviceRef*>(m_oidnDevice);

    // Recreate OIDN objects if resolution, guide buffers, or quality changed.
    // The denoiser imports persistent linear buffers, so source texture pointer
    // changes do not require filter rebuilds as long as the logical layout is
    // unchanged.
    bool needsUpdate = (width != m_width || height != m_height ||
                        m_quality != m_lastQuality || !m_linearColor ||
                        hasAlbedo != (m_linearAlbedo.Get() != nullptr) ||
                        hasNormal != (m_linearNormal.Get() != nullptr));

    if (needsUpdate) {
      // Clean up old
      if (m_oidnFilter) delete static_cast<oidn::FilterRef*>(m_oidnFilter);
      if (m_oidnColorBuf) delete static_cast<oidn::BufferRef*>(m_oidnColorBuf);
      if (m_oidnAlbedoBuf) delete static_cast<oidn::BufferRef*>(m_oidnAlbedoBuf);
      if (m_oidnNormalBuf) delete static_cast<oidn::BufferRef*>(m_oidnNormalBuf);
      if (m_oidnOutputBuf) delete static_cast<oidn::BufferRef*>(m_oidnOutputBuf);
      m_oidnFilter = nullptr;
      m_oidnColorBuf = nullptr;
      m_oidnAlbedoBuf = nullptr;
      m_oidnNormalBuf = nullptr;
      m_oidnOutputBuf = nullptr;
      m_linearColor.Reset();
      m_linearAlbedo.Reset();
      m_linearNormal.Reset();
      m_linearOutput.Reset();

      m_width = width;
      m_height = height;
      m_lastQuality = m_quality;
      m_lastInput = input;
      m_lastAlbedo = albedo;
      m_lastNormal = normal;
      m_lastOutput = output;

      // Calculate footprint
      UINT64 totalBytes = 0;
      m_device->GetCopyableFootprints(&inputDesc, 0, 1, 0, &m_linearFootprint, nullptr, nullptr, &totalBytes);
      m_linearBufferBytes = totalBytes;

      D3D12_RESOURCE_DESC bufDesc = {};
      bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
      bufDesc.Width = totalBytes;
      bufDesc.Height = 1;
      bufDesc.DepthOrArraySize = 1;
      bufDesc.MipLevels = 1;
      bufDesc.SampleDesc.Count = 1;
      bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
      bufDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS; // OIDN might want UAV? Or just Generic?

      D3D12_HEAP_PROPERTIES heapProps = {};
      heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

      // Create Linear Buffers
      auto CreateBuf = [&](ComPtr<ID3D12Resource> &resource,
                           const wchar_t* name) {
          // IMPORTANT: D3D12_HEAP_FLAG_SHARED is required for CreateSharedHandle to work
          if (FAILED(m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_SHARED, &bufDesc, 
              D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(resource.GetAddressOf())))) {
              throw std::runtime_error("Failed to create OIDN linear buffer");
          }
          resource->SetName(name);
      };

      CreateBuf(m_linearColor, L"OIDN Linear Color");
      CreateBuf(m_linearOutput, L"OIDN Linear Output");
      if (hasAlbedo) CreateBuf(m_linearAlbedo, L"OIDN Linear Albedo");
      if (hasNormal) CreateBuf(m_linearNormal, L"OIDN Linear Normal");

      // Register with OIDN
      auto RegisterBuf = [&](ID3D12Resource* res) -> void* {
          HANDLE h = nullptr;
          if (!CreateSharedHandleForResource(m_device, res, h)) {
              throw std::runtime_error("Failed to create shared handle for OIDN buffer");
          }
          void* b = new oidn::BufferRef(dev.newBuffer(oidn::ExternalMemoryTypeFlag::D3D12Resource, h, nullptr, totalBytes));
          CloseHandle(h);
          return b;
      };

      m_oidnColorBuf = RegisterBuf(m_linearColor.Get());
      m_oidnOutputBuf = RegisterBuf(m_linearOutput.Get());
      if (m_linearAlbedo) m_oidnAlbedoBuf = RegisterBuf(m_linearAlbedo.Get());
      if (m_linearNormal) m_oidnNormalBuf = RegisterBuf(m_linearNormal.Get());

      // Create Filter
      // NOTE: We use Format::Half3 with a stride of 8 bytes (sizeof(Half)*4) because the input is RGBA16F.
      // OIDN writes RGB and skips the A channel. Half4 is often not supported by the RT filter.
      oidn::FilterRef filter = dev.newFilter("RT");
      filter.setImage("color", *static_cast<oidn::BufferRef*>(m_oidnColorBuf), oidn::Format::Half3, width, height, 0, 8, m_linearFootprint.Footprint.RowPitch);
      if (m_oidnAlbedoBuf)
          filter.setImage("albedo", *static_cast<oidn::BufferRef*>(m_oidnAlbedoBuf), oidn::Format::Half3, width, height, 0, 8, m_linearFootprint.Footprint.RowPitch);
      if (m_oidnNormalBuf)
          filter.setImage("normal", *static_cast<oidn::BufferRef*>(m_oidnNormalBuf), oidn::Format::Half3, width, height, 0, 8, m_linearFootprint.Footprint.RowPitch);
      filter.setImage("output", *static_cast<oidn::BufferRef*>(m_oidnOutputBuf), oidn::Format::Half3, width, height, 0, 8, m_linearFootprint.Footprint.RowPitch);
      
      filter.set("hdr", true);
      switch(m_quality) {
        case Quality::Fast:     filter.set("quality", OIDN_QUALITY_FAST); break;
        case Quality::Balanced: filter.set("quality", OIDN_QUALITY_BALANCED); break;
        case Quality::High:     filter.set("quality", OIDN_QUALITY_HIGH); break;
      }
      filter.commit();
      m_oidnFilter = new oidn::FilterRef(filter);
    }
    return true;
  } catch (const std::exception& e) {
    fprintf(stderr, "OidnDenoiser: Prepare failed: %s\n", e.what());
    return false;
  }
#else
  (void)input;
  (void)albedo;
  (void)normal;
  (void)output;
  return false;
#endif
}

bool OidnDenoiser::RunDenoise(ID3D12GraphicsCommandList *cmd, ID3D12CommandQueue *queue,
                               ID3D12Resource *input, ID3D12Resource *albedo,
                               ID3D12Resource *normal,
                               ID3D12Resource *linearDepth,
                               ID3D12Resource *output, bool async) {
  if (!queue || !input || !output) return false;
  (void)async;

#ifdef USE_OIDN
  (void)cmd;
  if (!Prepare(input, albedo, normal, output))
    return false;

  try {
    oidn::DeviceRef& dev = *static_cast<oidn::DeviceRef*>(m_oidnDevice);

    // --- STEP 1: Copy Textures -> Linear Buffers ---
    m_cmdAlloc->Reset();
    m_cmdList->Reset(m_cmdAlloc.Get(), nullptr);

    {
        std::vector<D3D12_RESOURCE_BARRIER> barriers;
        auto AddBarrier = [&](ID3D12Resource* res, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
            D3D12_RESOURCE_BARRIER b = {};
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition.pResource = res;
            b.Transition.StateBefore = before;
            b.Transition.StateAfter = after;
            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barriers.push_back(b);
        };

        // Inputs Common -> CopySource
        AddBarrier(input, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
        if (albedo) AddBarrier(albedo, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
        if (normal) AddBarrier(normal, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
        // Linear Buffers Common -> CopyDest
        AddBarrier(m_linearColor.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
        // Copy input to output buffer as well, to preserve Alpha channel (OIDN only writes RGB)
        AddBarrier(m_linearOutput.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
        
        if (m_linearAlbedo) AddBarrier(m_linearAlbedo.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
        if (m_linearNormal) AddBarrier(m_linearNormal.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
        
        m_cmdList->ResourceBarrier((UINT)barriers.size(), barriers.data());

        auto CopyTexToBuf = [&](ID3D12Resource* tex, ID3D12Resource* buf) {
            D3D12_TEXTURE_COPY_LOCATION src = {};
            src.pResource = tex;
            src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            src.SubresourceIndex = 0;
            D3D12_TEXTURE_COPY_LOCATION dst = {};
            dst.pResource = buf;
            dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dst.PlacedFootprint = m_linearFootprint;
            m_cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        };

        CopyTexToBuf(input, m_linearColor.Get());
        CopyTexToBuf(input, m_linearOutput.Get()); // Validates Alpha
        if (albedo && m_linearAlbedo) CopyTexToBuf(albedo, m_linearAlbedo.Get());
        if (normal && m_linearNormal) CopyTexToBuf(normal, m_linearNormal.Get());

        // Restore Inputs to Common, Transition Linear to Common (for OIDN)
        barriers.clear();
        AddBarrier(input, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
        if (albedo) AddBarrier(albedo, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
        if (normal) AddBarrier(normal, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
        AddBarrier(m_linearColor.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
        AddBarrier(m_linearOutput.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
        if (m_linearAlbedo) AddBarrier(m_linearAlbedo.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
        if (m_linearNormal) AddBarrier(m_linearNormal.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
        
        m_cmdList->ResourceBarrier((UINT)barriers.size(), barriers.data());
    }
    m_cmdList->Close();
    
    ID3D12CommandList* lists[] = { m_cmdList.Get() };
    queue->ExecuteCommandLists(1, lists);
    
    // Sync before OIDN
    queue->Signal(m_fence.Get(), m_fenceValue);
    m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent);
    WaitForSingleObject(m_fenceEvent, INFINITE);
    m_fenceValue++;

    if (!SanitizeLinearInputsForOidn(queue, albedo && m_linearAlbedo,
                                     normal && m_linearNormal)) {
      fprintf(stderr, "OidnDenoiser: input sanitation failed; skipping OIDN.\n");
      return false;
    }

    // --- STEP 2: Execute OIDN ---
    oidn::FilterRef& filter = *static_cast<oidn::FilterRef*>(m_oidnFilter);
    filter.set("inputScale", m_oidnInputScale);
    filter.commit();
    filter.execute();
    dev.sync(); // Wait for OIDN to finish on GPU

    if (linearDepth) {
      if (!RestoreSkyPixelsAfterOidn(queue, linearDepth)) {
        fprintf(stderr,
                "OidnDenoiser: sky restore failed; keeping denoised output.\n");
      }
    }

    // --- STEP 3: Copy Linear Buffer -> Texture ---
    m_cmdAlloc->Reset();
    m_cmdList->Reset(m_cmdAlloc.Get(), nullptr);

    {
        std::vector<D3D12_RESOURCE_BARRIER> barriers;
        // Output Common -> CopyDest
        D3D12_RESOURCE_BARRIER b1 = {};
        b1.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b1.Transition.pResource = output;
        b1.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b1.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        b1.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        barriers.push_back(b1);
        
        // Linear Output Common -> CopySource
        D3D12_RESOURCE_BARRIER b2 = {};
        b2.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b2.Transition.pResource = m_linearOutput.Get();
        b2.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        b2.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barriers.push_back(b2);
        
        m_cmdList->ResourceBarrier((UINT)barriers.size(), barriers.data());

        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource = m_linearOutput.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = m_linearFootprint;
        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource = output;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = 0;
        m_cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        
        // Transition Back
        barriers.clear();
        b1.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        b1.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        barriers.push_back(b1);
        b2.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        b2.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        barriers.push_back(b2);
        
        m_cmdList->ResourceBarrier((UINT)barriers.size(), barriers.data());
    }
    m_cmdList->Close();
    
    queue->ExecuteCommandLists(1, lists);
    
    // Sync before returning
    queue->Signal(m_fence.Get(), m_fenceValue);
    m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent);
    WaitForSingleObject(m_fenceEvent, INFINITE);
    m_fenceValue++;

  } catch (const std::exception& e) {
    fprintf(stderr, "OidnDenoiser: RunDenoise failed: %s\n", e.what());
    return false;
  }

  return true;
#else
  // Fall back: no OIDN support compiled in.
  cmd->CopyResource(output, input);
  return true;
#endif
}
