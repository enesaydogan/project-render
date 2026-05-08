#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <cstdint>

class OidnDenoiser {
public:
  OidnDenoiser();
  ~OidnDenoiser();

  // Initialize with D3D12 device. Returns true if initialization succeeded.
  // If OpenImageDenoise support is compiled in and a GPU backend is available,
  // the wrapper will attempt to use GPU zero-copy import via shared handles.
  bool Initialize(ID3D12Device *device);
  void Shutdown();

  // Create persistent filter/intermediate resources for the current render
  // target before the end-condition denoise is needed. This is a best-effort
  // warmup; RunDenoise() still validates and prepares lazily if necessary.
  bool Prepare(ID3D12Resource *input, ID3D12Resource *albedo,
               ID3D12Resource *normal, ID3D12Resource *output);

  // Run denoise pass. If async is true, the implementation should attempt
  // asynchronous execution when available. Returns true if the denoiser ran.
  // Requires command queue for interop copies and synchronization.
  bool RunDenoise(ID3D12GraphicsCommandList *cmd, ID3D12CommandQueue *queue, 
                  ID3D12Resource *input, ID3D12Resource *albedo, 
                  ID3D12Resource *normal, ID3D12Resource *output, 
                  bool async);

  // CPU fallback: denoise host-mapped Half4 buffers.
  // The input/output pointers must point to the first pixel (row 0, col 0).
  // Row pitch must match the mapped buffer row stride (can be >= width*8).
  bool RunDenoiseHostHalf4(const void* input, uint32_t width, uint32_t height,
                           size_t inputRowPitchBytes, void* output,
                           size_t outputRowPitchBytes);

  enum class Quality {
    Fast,
    Balanced,
    High
  };
  void SetQuality(Quality q) { m_quality = q; }

private:
  bool m_initialized = false;
  ID3D12Device *m_device = nullptr;
  Quality m_quality = Quality::Balanced;

#ifdef USE_OIDN
  // OIDN-specific members (only present when compiled with OIDN support)
  bool m_oidnAvailable = false;
  bool m_gpuBackendAvailable = false;
  // Forward-declare OIDN objects in implementation file to avoid exposing
  // OIDN headers in this header.
  void *m_oidnDevice = nullptr; // opaque pointer to oidn::DeviceRef
  void *m_oidnFilter = nullptr; // opaque pointer to oidn::FilterRef
  
  // Persistent OIDN buffers wrapping the D3D12 resources
  void *m_oidnColorBuf = nullptr;
  void *m_oidnAlbedoBuf = nullptr;
  void *m_oidnNormalBuf = nullptr;
  void *m_oidnOutputBuf = nullptr;

  // Interop resources (Linear buffers on Default Heap)
  Microsoft::WRL::ComPtr<ID3D12Resource> m_linearColor;
  Microsoft::WRL::ComPtr<ID3D12Resource> m_linearAlbedo;
  Microsoft::WRL::ComPtr<ID3D12Resource> m_linearNormal;
  Microsoft::WRL::ComPtr<ID3D12Resource> m_linearOutput;
  
  // Synchronization for internal copies
  Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
  uint64_t m_fenceValue = 0;
  HANDLE m_fenceEvent = nullptr;
  
  Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_cmdAlloc;
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_cmdList;

  D3D12_PLACED_SUBRESOURCE_FOOTPRINT m_linearFootprint = {};

  // Track state to detect when we need to recreate OIDN objects
  uint32_t m_width = 0;
  uint32_t m_height = 0;
  ID3D12Resource *m_lastInput = nullptr;
  ID3D12Resource *m_lastAlbedo = nullptr;
  ID3D12Resource *m_lastNormal = nullptr;
  ID3D12Resource *m_lastOutput = nullptr;
  Quality m_lastQuality = Quality::Balanced;

  // CPU fallback objects
  void* m_oidnCpuDevice = nullptr; // oidn::DeviceRef
  void* m_oidnCpuFilter = nullptr; // oidn::FilterRef
  uint32_t m_cpuWidth = 0;
  uint32_t m_cpuHeight = 0;
  Quality m_cpuLastQuality = Quality::Balanced;
#endif
};
