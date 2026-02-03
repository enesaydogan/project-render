#include "oidn_denoiser.h"
#include <cstdio>
#include <cassert>
#include <utility>
#include <vector>
#include <stdexcept>

#ifdef USE_OIDN
#include <OpenImageDenoise/oidn.hpp>
#endif

using Microsoft::WRL::ComPtr;

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

    const bool needsUpdate = (!m_oidnCpuFilter || width != m_cpuWidth ||
                              height != m_cpuHeight ||
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
      filter.setImage("color", const_cast<void*>(input), oidn::Format::Half3, width, height, 0,
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

bool OidnDenoiser::RunDenoise(ID3D12GraphicsCommandList *cmd, ID3D12CommandQueue *queue,
                               ID3D12Resource *input, ID3D12Resource *albedo,
                               ID3D12Resource *normal, ID3D12Resource *output,
                               bool async) {
  if (!queue || !input || !output) return false;

#ifdef USE_OIDN
  if (!m_oidnAvailable || !m_gpuBackendAvailable || !m_cmdList)
    return false;

  const D3D12_RESOURCE_DESC inputDesc = input->GetDesc();
  const D3D12_RESOURCE_DESC outputDesc = output->GetDesc();
  if (inputDesc.Width != outputDesc.Width ||
      inputDesc.Height != outputDesc.Height) {
    return false;
  }

  const uint32_t width = (uint32_t)inputDesc.Width;
  const uint32_t height = inputDesc.Height;

  try {
    oidn::DeviceRef& dev = *static_cast<oidn::DeviceRef*>(m_oidnDevice);

    // Recreate OIDN objects if resolution, resources, or quality changed
    // Note: We check if input/output pointers changed, but we fundamentally rely on the Linear Buffers now.
    // If the logical size changes, we must recreate.
    bool needsUpdate = (width != m_width || height != m_height ||
                        m_quality != m_lastQuality || !m_linearColor);

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

      m_width = width;
      m_height = height;
      m_lastQuality = m_quality;

      // Calculate footprint
      UINT64 totalBytes = 0;
      m_device->GetCopyableFootprints(&inputDesc, 0, 1, 0, &m_linearFootprint, nullptr, nullptr, &totalBytes);

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
      auto CreateBuf = [&](ID3D12Resource** ppRes, const wchar_t* name) {
          // IMPORTANT: D3D12_HEAP_FLAG_SHARED is required for CreateSharedHandle to work
          if (FAILED(m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_SHARED, &bufDesc, 
              D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(ppRes)))) {
              throw std::runtime_error("Failed to create OIDN linear buffer");
          }
          (*ppRes)->SetName(name);
      };

      CreateBuf(&m_linearColor, L"OIDN Linear Color");
      CreateBuf(&m_linearOutput, L"OIDN Linear Output");
      if (albedo) CreateBuf(&m_linearAlbedo, L"OIDN Linear Albedo");
      if (normal) CreateBuf(&m_linearNormal, L"OIDN Linear Normal");

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

    // --- STEP 2: Execute OIDN ---
    oidn::FilterRef& filter = *static_cast<oidn::FilterRef*>(m_oidnFilter);
    filter.execute();
    dev.sync(); // Wait for OIDN to finish on GPU

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
