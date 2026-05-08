#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl.h>

class OptixDenoiserWrapper {
public:
  OptixDenoiserWrapper();
  ~OptixDenoiserWrapper();

  bool Initialize(ID3D12Device *device);
  void Shutdown();
  bool Prepare(ID3D12Resource *input, ID3D12Resource *albedo,
               ID3D12Resource *normal, ID3D12Resource *output);

  bool RunDenoise(ID3D12CommandQueue *queue, ID3D12Resource *input,
                  ID3D12Resource *albedo, ID3D12Resource *normal,
                  ID3D12Resource *output);

private:
  bool m_initialized = false;
  ID3D12Device *m_device = nullptr;

#ifdef USE_OPTIX_DENOISER
  struct ExternalBuffer {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    void *externalMemory = nullptr;
    void *devicePtr = nullptr;
    uint64_t byteSize = 0;
    uint64_t importedSize = 0;
  };

  bool m_optixAvailable = false;
  int m_cudaDeviceIndex = -1;
  void *m_cudaStream = nullptr;
  void *m_optixContext = nullptr;
  void *m_optixDenoiser = nullptr;
  uint64_t m_state = 0;
  uint64_t m_scratch = 0;
  uint64_t m_intensity = 0;
  size_t m_stateSize = 0;
  size_t m_scratchSize = 0;
  size_t m_intensityScratchSize = 0;

  ExternalBuffer m_linearColor;
  ExternalBuffer m_linearAlbedo;
  ExternalBuffer m_linearNormal;
  ExternalBuffer m_linearOutput;

  Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
  uint64_t m_fenceValue = 1;
  HANDLE m_fenceEvent = nullptr;
  Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_cmdAlloc;
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_cmdList;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT m_linearFootprint = {};

  uint32_t m_width = 0;
  uint32_t m_height = 0;
  bool m_hasGuideAlbedo = false;
  bool m_hasGuideNormal = false;

  bool InitializeCudaForDevice(ID3D12Device *device);
  bool CreateOrResizeResources(ID3D12Resource *input, ID3D12Resource *albedo,
                               ID3D12Resource *normal,
                               ID3D12Resource *output);
  bool CreateExternalBuffer(ExternalBuffer &buffer, uint64_t byteSize,
                            const wchar_t *name);
  void ReleaseExternalBuffer(ExternalBuffer &buffer);
  void ReleaseOptixObjects();
  bool WaitForQueue(ID3D12CommandQueue *queue);
#endif
};
