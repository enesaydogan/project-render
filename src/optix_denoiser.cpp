#include "optix_denoiser.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

using Microsoft::WRL::ComPtr;

OptixDenoiserWrapper::OptixDenoiserWrapper() = default;
OptixDenoiserWrapper::~OptixDenoiserWrapper() { Shutdown(); }

#ifdef USE_OPTIX_DENOISER

#include <cuda.h>
#include <cuda_runtime_api.h>
#include <optix.h>
#include <optix_function_table_definition.h>
#include <optix_stubs.h>

namespace {

const char *CudaErrorString(cudaError_t err) { return cudaGetErrorString(err); }

bool CheckCuda(cudaError_t err, const char *what) {
  if (err == cudaSuccess)
    return true;
  fprintf(stderr, "OptixDenoiser: %s failed: %s\n", what,
          CudaErrorString(err));
  return false;
}

bool CheckCu(CUresult result, const char *what) {
  if (result == CUDA_SUCCESS)
    return true;
  const char *name = nullptr;
  const char *msg = nullptr;
  cuGetErrorName(result, &name);
  cuGetErrorString(result, &msg);
  fprintf(stderr, "OptixDenoiser: %s failed: %s (%s)\n", what,
          name ? name : "CUresult", msg ? msg : "no details");
  return false;
}

bool CheckOptix(OptixResult result, const char *what) {
  if (result == OPTIX_SUCCESS)
    return true;
  fprintf(stderr, "OptixDenoiser: %s failed: %d\n", what, (int)result);
  return false;
}

void ProjectRenderOptixLog(unsigned int level, const char *tag,
                           const char *message, void *) {
  fprintf(stderr, "OptiX[%u][%s]: %s\n", level, tag ? tag : "-", message);
}

uint64_t GetResourceAllocationSize(ID3D12Device *device, ID3D12Resource *res) {
  if (!device || !res)
    return 0;
  const D3D12_RESOURCE_DESC desc = res->GetDesc();
  const D3D12_RESOURCE_ALLOCATION_INFO info =
      device->GetResourceAllocationInfo(0, 1, &desc);
  return info.SizeInBytes;
}

bool IsRgba16FloatTexture(const D3D12_RESOURCE_DESC &desc) {
  return desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
         desc.MipLevels == 1 &&
         desc.DepthOrArraySize == 1 &&
         desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT;
}

} // namespace

bool OptixDenoiserWrapper::Initialize(ID3D12Device *device) {
  if (!device)
    return false;
  if (m_initialized)
    return m_optixAvailable;

  m_device = device;
  m_initialized = true;

  try {
    if (!InitializeCudaForDevice(device))
      return false;

    if (!CheckOptix(optixInit(), "optixInit"))
      return false;

    OptixDeviceContextOptions options = {};
    options.logCallbackFunction = ProjectRenderOptixLog;
    options.logCallbackLevel = 3;

    OptixDeviceContext context = nullptr;
    if (!CheckOptix(optixDeviceContextCreate(nullptr, &options, &context),
                    "optixDeviceContextCreate"))
      return false;
    m_optixContext = context;

    if (!CheckCuda(cudaStreamCreate(
                       reinterpret_cast<cudaStream_t *>(&m_cudaStream)),
                   "cudaStreamCreate"))
      return false;

    if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                   IID_PPV_ARGS(&m_fence)))) {
      fprintf(stderr, "OptixDenoiser: CreateFence failed\n");
      return false;
    }
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_fenceEvent)
      return false;

    if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              IID_PPV_ARGS(&m_cmdAlloc))) ||
        FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         m_cmdAlloc.Get(), nullptr,
                                         IID_PPV_ARGS(&m_cmdList)))) {
      fprintf(stderr, "OptixDenoiser: command list creation failed\n");
      return false;
    }
    m_cmdList->Close();

    m_optixAvailable = true;
    fprintf(stderr, "OptixDenoiser: initialized on CUDA device %d\n",
            m_cudaDeviceIndex);
    return true;
  } catch (const std::exception &e) {
    fprintf(stderr, "OptixDenoiser: initialization failed: %s\n", e.what());
    Shutdown();
    return false;
  }
}

void OptixDenoiserWrapper::Shutdown() {
  ReleaseOptixObjects();
  ReleaseExternalBuffer(m_linearColor);
  ReleaseExternalBuffer(m_linearAlbedo);
  ReleaseExternalBuffer(m_linearNormal);
  ReleaseExternalBuffer(m_linearOutput);

  if (m_cudaStream) {
    cudaStreamDestroy(reinterpret_cast<cudaStream_t>(m_cudaStream));
    m_cudaStream = nullptr;
  }
  if (m_optixContext) {
    optixDeviceContextDestroy(
        reinterpret_cast<OptixDeviceContext>(m_optixContext));
    m_optixContext = nullptr;
  }
  if (m_fenceEvent) {
    CloseHandle(m_fenceEvent);
    m_fenceEvent = nullptr;
  }
  m_cmdList.Reset();
  m_cmdAlloc.Reset();
  m_fence.Reset();
  m_initialized = false;
  m_optixAvailable = false;
  m_device = nullptr;
  m_cudaDeviceIndex = -1;
  m_width = 0;
  m_height = 0;
  m_hasGuideAlbedo = false;
  m_hasGuideNormal = false;
}

bool OptixDenoiserWrapper::InitializeCudaForDevice(ID3D12Device *device) {
  if (!CheckCu(cuInit(0), "cuInit"))
    return false;

  LUID d3dLuid = device->GetAdapterLuid();
  char d3dLuidBytes[8] = {};
  memcpy(d3dLuidBytes, &d3dLuid, sizeof(d3dLuidBytes));

  int deviceCount = 0;
  if (!CheckCu(cuDeviceGetCount(&deviceCount), "cuDeviceGetCount") ||
      deviceCount <= 0) {
    return false;
  }

  for (int i = 0; i < deviceCount; ++i) {
    CUdevice cuDevice = 0;
    if (cuDeviceGet(&cuDevice, i) != CUDA_SUCCESS)
      continue;

    char cudaLuid[8] = {};
    unsigned int nodeMask = 0;
    if (cuDeviceGetLuid(cudaLuid, &nodeMask, cuDevice) == CUDA_SUCCESS &&
        memcmp(cudaLuid, d3dLuidBytes, sizeof(cudaLuid)) == 0) {
      m_cudaDeviceIndex = i;
      break;
    }
  }

  if (m_cudaDeviceIndex < 0) {
    fprintf(stderr,
            "OptixDenoiser: no CUDA device matches the D3D12 adapter LUID\n");
    return false;
  }

  if (!CheckCuda(cudaSetDevice(m_cudaDeviceIndex), "cudaSetDevice"))
    return false;
  return CheckCuda(cudaFree(nullptr), "cuda context creation");
}

bool OptixDenoiserWrapper::CreateExternalBuffer(ExternalBuffer &buffer,
                                                uint64_t byteSize,
                                                const wchar_t *name) {
  ReleaseExternalBuffer(buffer);
  if (byteSize == 0)
    return false;

  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = byteSize;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  desc.Flags = D3D12_RESOURCE_FLAG_NONE;

  D3D12_HEAP_PROPERTIES heapProps = {};
  heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
  if (FAILED(m_device->CreateCommittedResource(
          &heapProps, D3D12_HEAP_FLAG_SHARED, &desc,
          D3D12_RESOURCE_STATE_COMMON, nullptr,
          IID_PPV_ARGS(&buffer.resource)))) {
    fprintf(stderr, "OptixDenoiser: failed to create shared linear buffer\n");
    return false;
  }
  buffer.resource->SetName(name);
  buffer.byteSize = byteSize;
  buffer.importedSize = GetResourceAllocationSize(m_device, buffer.resource.Get());

  HANDLE sharedHandle = nullptr;
  if (FAILED(m_device->CreateSharedHandle(buffer.resource.Get(), nullptr,
                                          GENERIC_ALL, nullptr,
                                          &sharedHandle)) ||
      !sharedHandle) {
    fprintf(stderr, "OptixDenoiser: CreateSharedHandle failed\n");
    ReleaseExternalBuffer(buffer);
    return false;
  }

  cudaExternalMemoryHandleDesc extDesc = {};
  extDesc.type = cudaExternalMemoryHandleTypeD3D12Resource;
  extDesc.handle.win32.handle = sharedHandle;
  extDesc.size = buffer.importedSize;
  extDesc.flags = cudaExternalMemoryDedicated;

  cudaExternalMemory_t externalMemory = nullptr;
  const cudaError_t importResult =
      cudaImportExternalMemory(&externalMemory, &extDesc);
  CloseHandle(sharedHandle);
  if (!CheckCuda(importResult, "cudaImportExternalMemory")) {
    ReleaseExternalBuffer(buffer);
    return false;
  }

  cudaExternalMemoryBufferDesc mapDesc = {};
  mapDesc.offset = 0;
  mapDesc.size = buffer.byteSize;
  void *devicePtr = nullptr;
  if (!CheckCuda(cudaExternalMemoryGetMappedBuffer(&devicePtr, externalMemory,
                                                   &mapDesc),
                 "cudaExternalMemoryGetMappedBuffer")) {
    cudaDestroyExternalMemory(externalMemory);
    ReleaseExternalBuffer(buffer);
    return false;
  }

  buffer.externalMemory = externalMemory;
  buffer.devicePtr = devicePtr;
  return true;
}

void OptixDenoiserWrapper::ReleaseExternalBuffer(ExternalBuffer &buffer) {
  if (buffer.devicePtr) {
    cudaFree(buffer.devicePtr);
    buffer.devicePtr = nullptr;
  }
  if (buffer.externalMemory) {
    cudaDestroyExternalMemory(
        reinterpret_cast<cudaExternalMemory_t>(buffer.externalMemory));
    buffer.externalMemory = nullptr;
  }
  buffer.resource.Reset();
  buffer.byteSize = 0;
  buffer.importedSize = 0;
}

void OptixDenoiserWrapper::ReleaseOptixObjects() {
  if (m_optixDenoiser) {
    optixDenoiserDestroy(reinterpret_cast<OptixDenoiser>(m_optixDenoiser));
    m_optixDenoiser = nullptr;
  }
  if (m_state) {
    cudaFree(reinterpret_cast<void *>(m_state));
    m_state = 0;
  }
  if (m_scratch) {
    cudaFree(reinterpret_cast<void *>(m_scratch));
    m_scratch = 0;
  }
  if (m_intensity) {
    cudaFree(reinterpret_cast<void *>(m_intensity));
    m_intensity = 0;
  }
  m_stateSize = 0;
  m_scratchSize = 0;
  m_intensityScratchSize = 0;
}

bool OptixDenoiserWrapper::CreateOrResizeResources(ID3D12Resource *input,
                                                   ID3D12Resource *albedo,
                                                   ID3D12Resource *normal,
                                                   ID3D12Resource *output) {
  if (!input || !output)
    return false;

  const D3D12_RESOURCE_DESC inputDesc = input->GetDesc();
  const D3D12_RESOURCE_DESC outputDesc = output->GetDesc();
  if (!IsRgba16FloatTexture(inputDesc) || !IsRgba16FloatTexture(outputDesc) ||
      inputDesc.Width != outputDesc.Width ||
      inputDesc.Height != outputDesc.Height) {
    fprintf(stderr, "OptixDenoiser: unsupported input/output format or size\n");
    return false;
  }

  const uint32_t width = static_cast<uint32_t>(inputDesc.Width);
  const uint32_t height = inputDesc.Height;
  const bool hasAlbedo = albedo && IsRgba16FloatTexture(albedo->GetDesc());
  const bool hasNormal = normal && IsRgba16FloatTexture(normal->GetDesc());

  UINT64 totalBytes = 0;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
  m_device->GetCopyableFootprints(&inputDesc, 0, 1, 0, &footprint, nullptr,
                                  nullptr, &totalBytes);

  const bool needsUpdate =
      width != m_width || height != m_height || !m_linearColor.resource ||
      hasAlbedo != m_hasGuideAlbedo || hasNormal != m_hasGuideNormal;
  if (!needsUpdate)
    return true;

  ReleaseOptixObjects();
  ReleaseExternalBuffer(m_linearColor);
  ReleaseExternalBuffer(m_linearAlbedo);
  ReleaseExternalBuffer(m_linearNormal);
  ReleaseExternalBuffer(m_linearOutput);

  if (!CreateExternalBuffer(m_linearColor, totalBytes, L"OptiX Linear Color") ||
      !CreateExternalBuffer(m_linearOutput, totalBytes,
                            L"OptiX Linear Output")) {
    return false;
  }
  if (hasAlbedo &&
      !CreateExternalBuffer(m_linearAlbedo, totalBytes,
                            L"OptiX Linear Albedo")) {
    return false;
  }
  if (hasNormal &&
      !CreateExternalBuffer(m_linearNormal, totalBytes,
                            L"OptiX Linear Normal")) {
    return false;
  }

  OptixDenoiserOptions options = {};
  options.guideAlbedo = hasAlbedo ? 1u : 0u;
  options.guideNormal = hasNormal ? 1u : 0u;
  options.denoiseAlpha = OPTIX_DENOISER_ALPHA_MODE_COPY;

  OptixDenoiser denoiser = nullptr;
  if (!CheckOptix(optixDenoiserCreate(
                      reinterpret_cast<OptixDeviceContext>(m_optixContext),
                      OPTIX_DENOISER_MODEL_KIND_HDR, &options, &denoiser),
                  "optixDenoiserCreate")) {
    return false;
  }
  m_optixDenoiser = denoiser;

  OptixDenoiserSizes sizes = {};
  if (!CheckOptix(optixDenoiserComputeMemoryResources(
                      denoiser, width, height, &sizes),
                  "optixDenoiserComputeMemoryResources")) {
    return false;
  }
  m_stateSize = sizes.stateSizeInBytes;
  m_scratchSize = (std::max)(sizes.withoutOverlapScratchSizeInBytes,
                             sizes.computeIntensitySizeInBytes);
  m_intensityScratchSize = sizes.computeIntensitySizeInBytes;

  if (!CheckCuda(cudaMalloc(reinterpret_cast<void **>(&m_state), m_stateSize),
                 "cudaMalloc denoiser state") ||
      !CheckCuda(cudaMalloc(reinterpret_cast<void **>(&m_scratch),
                            m_scratchSize),
                 "cudaMalloc denoiser scratch") ||
      !CheckCuda(cudaMalloc(reinterpret_cast<void **>(&m_intensity),
                            sizeof(float)),
                 "cudaMalloc denoiser intensity")) {
    return false;
  }

  cudaStream_t stream = reinterpret_cast<cudaStream_t>(m_cudaStream);
  if (!CheckOptix(optixDenoiserSetup(denoiser, stream, width, height, m_state,
                                     m_stateSize, m_scratch, m_scratchSize),
                  "optixDenoiserSetup")) {
    return false;
  }
  CheckCuda(cudaStreamSynchronize(stream), "cudaStreamSynchronize");

  m_width = width;
  m_height = height;
  m_hasGuideAlbedo = hasAlbedo;
  m_hasGuideNormal = hasNormal;
  m_linearFootprint = footprint;
  return true;
}

bool OptixDenoiserWrapper::WaitForQueue(ID3D12CommandQueue *queue) {
  if (!queue || !m_fence || !m_fenceEvent)
    return false;

  const uint64_t value = m_fenceValue++;
  if (FAILED(queue->Signal(m_fence.Get(), value)))
    return false;
  if (m_fence->GetCompletedValue() < value) {
    if (FAILED(m_fence->SetEventOnCompletion(value, m_fenceEvent)))
      return false;
    WaitForSingleObject(m_fenceEvent, INFINITE);
  }
  return true;
}

bool OptixDenoiserWrapper::Prepare(ID3D12Resource *input,
                                   ID3D12Resource *albedo,
                                   ID3D12Resource *normal,
                                   ID3D12Resource *output) {
  if (!input || !output)
    return false;
  if (!m_initialized || !m_optixAvailable) {
    if (!Initialize(m_device))
      return false;
  }
  try {
    return CreateOrResizeResources(input, albedo, normal, output);
  } catch (const std::exception &e) {
    fprintf(stderr, "OptixDenoiser: Prepare failed: %s\n", e.what());
    return false;
  }
}

bool OptixDenoiserWrapper::RunDenoise(ID3D12CommandQueue *queue,
                                      ID3D12Resource *input,
                                      ID3D12Resource *albedo,
                                      ID3D12Resource *normal,
                                      ID3D12Resource *output) {
  if (!queue || !input || !output)
    return false;
  if (!Prepare(input, albedo, normal, output) || !m_optixAvailable ||
      !m_cmdList)
    return false;

  try {
    m_cmdAlloc->Reset();
    m_cmdList->Reset(m_cmdAlloc.Get(), nullptr);

    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    auto AddBarrier = [&](ID3D12Resource *res, D3D12_RESOURCE_STATES before,
                          D3D12_RESOURCE_STATES after) {
      D3D12_RESOURCE_BARRIER b = {};
      b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      b.Transition.pResource = res;
      b.Transition.StateBefore = before;
      b.Transition.StateAfter = after;
      b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      barriers.push_back(b);
    };

    AddBarrier(input, D3D12_RESOURCE_STATE_COMMON,
               D3D12_RESOURCE_STATE_COPY_SOURCE);
    AddBarrier(m_linearColor.resource.Get(), D3D12_RESOURCE_STATE_COMMON,
               D3D12_RESOURCE_STATE_COPY_DEST);
    if (m_hasGuideAlbedo && albedo) {
      AddBarrier(albedo, D3D12_RESOURCE_STATE_COMMON,
                 D3D12_RESOURCE_STATE_COPY_SOURCE);
      AddBarrier(m_linearAlbedo.resource.Get(), D3D12_RESOURCE_STATE_COMMON,
                 D3D12_RESOURCE_STATE_COPY_DEST);
    }
    if (m_hasGuideNormal && normal) {
      AddBarrier(normal, D3D12_RESOURCE_STATE_COMMON,
                 D3D12_RESOURCE_STATE_COPY_SOURCE);
      AddBarrier(m_linearNormal.resource.Get(), D3D12_RESOURCE_STATE_COMMON,
                 D3D12_RESOURCE_STATE_COPY_DEST);
    }
    AddBarrier(m_linearOutput.resource.Get(), D3D12_RESOURCE_STATE_COMMON,
               D3D12_RESOURCE_STATE_COPY_DEST);
    m_cmdList->ResourceBarrier((UINT)barriers.size(), barriers.data());

    auto CopyTexToBuffer = [&](ID3D12Resource *tex, ID3D12Resource *buf) {
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

    CopyTexToBuffer(input, m_linearColor.resource.Get());
    CopyTexToBuffer(input, m_linearOutput.resource.Get());
    if (m_hasGuideAlbedo && albedo)
      CopyTexToBuffer(albedo, m_linearAlbedo.resource.Get());
    if (m_hasGuideNormal && normal)
      CopyTexToBuffer(normal, m_linearNormal.resource.Get());

    barriers.clear();
    AddBarrier(input, D3D12_RESOURCE_STATE_COPY_SOURCE,
               D3D12_RESOURCE_STATE_COMMON);
    AddBarrier(m_linearColor.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
               D3D12_RESOURCE_STATE_COMMON);
    if (m_hasGuideAlbedo && albedo) {
      AddBarrier(albedo, D3D12_RESOURCE_STATE_COPY_SOURCE,
                 D3D12_RESOURCE_STATE_COMMON);
      AddBarrier(m_linearAlbedo.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                 D3D12_RESOURCE_STATE_COMMON);
    }
    if (m_hasGuideNormal && normal) {
      AddBarrier(normal, D3D12_RESOURCE_STATE_COPY_SOURCE,
                 D3D12_RESOURCE_STATE_COMMON);
      AddBarrier(m_linearNormal.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                 D3D12_RESOURCE_STATE_COMMON);
    }
    AddBarrier(m_linearOutput.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
               D3D12_RESOURCE_STATE_COMMON);
    m_cmdList->ResourceBarrier((UINT)barriers.size(), barriers.data());
    m_cmdList->Close();

    ID3D12CommandList *copyInLists[] = {m_cmdList.Get()};
    queue->ExecuteCommandLists(1, copyInLists);
    if (!WaitForQueue(queue))
      return false;

    OptixImage2D color = {};
    color.data = reinterpret_cast<CUdeviceptr>(m_linearColor.devicePtr);
    color.width = m_width;
    color.height = m_height;
    color.rowStrideInBytes = m_linearFootprint.Footprint.RowPitch;
    color.pixelStrideInBytes = 8;
    color.format = OPTIX_PIXEL_FORMAT_HALF4;

    OptixImage2D out = color;
    out.data = reinterpret_cast<CUdeviceptr>(m_linearOutput.devicePtr);

    OptixDenoiserGuideLayer guide = {};
    if (m_hasGuideAlbedo) {
      guide.albedo = color;
      guide.albedo.data =
          reinterpret_cast<CUdeviceptr>(m_linearAlbedo.devicePtr);
    }
    if (m_hasGuideNormal) {
      guide.normal = color;
      guide.normal.data =
          reinterpret_cast<CUdeviceptr>(m_linearNormal.devicePtr);
    }

    OptixDenoiserLayer layer = {};
    layer.input = color;
    layer.output = out;

    OptixDenoiserParams params = {};
    params.hdrIntensity = m_intensity;
    params.blendFactor = 0.0f;

    cudaStream_t stream = reinterpret_cast<cudaStream_t>(m_cudaStream);
    OptixDenoiser denoiser = reinterpret_cast<OptixDenoiser>(m_optixDenoiser);
    if (!CheckOptix(optixDenoiserComputeIntensity(
                        denoiser, stream, &color, m_intensity, m_scratch,
                        m_intensityScratchSize),
                    "optixDenoiserComputeIntensity") ||
        !CheckOptix(optixDenoiserInvoke(denoiser, stream, &params, m_state,
                                        m_stateSize, &guide, &layer, 1, 0, 0,
                                        m_scratch, m_scratchSize),
                    "optixDenoiserInvoke") ||
        !CheckCuda(cudaStreamSynchronize(stream), "cudaStreamSynchronize")) {
      return false;
    }

    m_cmdAlloc->Reset();
    m_cmdList->Reset(m_cmdAlloc.Get(), nullptr);

    barriers.clear();
    AddBarrier(output, D3D12_RESOURCE_STATE_COMMON,
               D3D12_RESOURCE_STATE_COPY_DEST);
    AddBarrier(m_linearOutput.resource.Get(), D3D12_RESOURCE_STATE_COMMON,
               D3D12_RESOURCE_STATE_COPY_SOURCE);
    m_cmdList->ResourceBarrier((UINT)barriers.size(), barriers.data());

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = m_linearOutput.resource.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = m_linearFootprint;
    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = output;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;
    m_cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    barriers.clear();
    AddBarrier(output, D3D12_RESOURCE_STATE_COPY_DEST,
               D3D12_RESOURCE_STATE_COMMON);
    AddBarrier(m_linearOutput.resource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
               D3D12_RESOURCE_STATE_COMMON);
    m_cmdList->ResourceBarrier((UINT)barriers.size(), barriers.data());
    m_cmdList->Close();

    ID3D12CommandList *copyOutLists[] = {m_cmdList.Get()};
    queue->ExecuteCommandLists(1, copyOutLists);
    return WaitForQueue(queue);
  } catch (const std::exception &e) {
    fprintf(stderr, "OptixDenoiser: RunDenoise failed: %s\n", e.what());
    return false;
  }
}

#else

bool OptixDenoiserWrapper::Initialize(ID3D12Device *device) {
  m_device = device;
  m_initialized = true;
  return false;
}

void OptixDenoiserWrapper::Shutdown() {
  m_initialized = false;
  m_device = nullptr;
}

bool OptixDenoiserWrapper::Prepare(ID3D12Resource *, ID3D12Resource *,
                                   ID3D12Resource *, ID3D12Resource *) {
  return false;
}

bool OptixDenoiserWrapper::RunDenoise(ID3D12CommandQueue *, ID3D12Resource *,
                                      ID3D12Resource *, ID3D12Resource *,
                                      ID3D12Resource *) {
  fprintf(stderr,
          "OptixDenoiser: USE_OPTIX_DENOISER is OFF; OptiX denoise skipped.\n");
  return false;
}

#endif
