#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "asset_loader.h"
#include <algorithm>
#include <cfloat>
#include <cctype>
#include <cstring>
#include <cmath>
#include <d3d12.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#define STB_DXT_IMPLEMENTATION
#include <stb_dxt.h>
#include <stb_image.h>
#include <stdio.h>
#include <string>
#include <tinyexr.h>
#include <vector>
#include <windows.h>
#include <wrl.h>

#include <assimp/Importer.hpp>
#include <assimp/ProgressHandler.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#ifdef USE_TINYGLTF
#include <tiny_gltf.h>
#endif

#ifdef USE_SKETCHUP_SDK
#include <SketchUpAPI/initialize.h>
#endif

using Microsoft::WRL::ComPtr;

// Access global runtime flags set by WinMain
extern bool g_debugLog;
extern bool g_fastImport;

inline void ThrowIfFailed(HRESULT hr) {
  if (FAILED(hr)) {
    char buf[256];
    sprintf_s(buf, "HRESULT 0x%08x\n", static_cast<unsigned>(hr));
    fprintf(stderr, "%s", buf);
    ExitProcess(static_cast<UINT>(hr));
  }
}

namespace Asset {

static ComPtr<ID3D12Device> s_device;
static ComPtr<ID3D12CommandQueue> s_queue;
// Optional progress callback. Signature: progress [0..1], status message
std::function<void(float, const std::string &)> s_progressCb;
static thread_local bool s_deferGpuUpload = false;
static TextureCompressionMode s_textureCompressionMode =
    TextureCompressionMode::Balanced;

static std::wstring WidePathFromUtf8(const std::string &path) {
  if (path.empty()) {
    return {};
  }

  int wideCount = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                      path.data(),
                                      static_cast<int>(path.size()), nullptr,
                                      0);
  UINT codePage = CP_UTF8;
  DWORD flags = MB_ERR_INVALID_CHARS;
  if (wideCount <= 0) {
    codePage = CP_ACP;
    flags = 0;
    wideCount = MultiByteToWideChar(codePage, flags, path.data(),
                                    static_cast<int>(path.size()), nullptr, 0);
  }
  if (wideCount <= 0) {
    return {};
  }

  std::wstring widePath(static_cast<size_t>(wideCount), L'\0');
  MultiByteToWideChar(codePage, flags, path.data(),
                      static_cast<int>(path.size()), widePath.data(),
                      wideCount);
  return widePath;
}

static std::filesystem::path NativePathFromUtf8(const std::string &path) {
  std::wstring widePath = WidePathFromUtf8(path);
  return widePath.empty() ? std::filesystem::path(path)
                          : std::filesystem::path(widePath);
}

static float ComputeTangentHandedness(const aiVector3D &normal,
                                      const aiVector3D &tangent,
                                      const aiVector3D &bitangent) {
  const float crossX = normal.y * tangent.z - normal.z * tangent.y;
  const float crossY = normal.z * tangent.x - normal.x * tangent.z;
  const float crossZ = normal.x * tangent.y - normal.y * tangent.x;
  const float orientation =
      crossX * bitangent.x + crossY * bitangent.y + crossZ * bitangent.z;
  return orientation < 0.0f ? -1.0f : 1.0f;
}

static float ComputeLinearTransformHandedness(const float transform[16]) {
  const float determinant =
      transform[0] *
          (transform[5] * transform[10] - transform[9] * transform[6]) -
      transform[4] *
          (transform[1] * transform[10] - transform[9] * transform[2]) +
      transform[8] *
          (transform[1] * transform[6] - transform[5] * transform[2]);
  return determinant < 0.0f ? -1.0f : 1.0f;
}

static bool PathUsesDirectXNormalConvention(const std::string &path) {
  std::string lower = path;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return static_cast<char>(tolower(c)); });
  return lower.find("_nor_dx") != std::string::npos ||
         lower.find("_normal_dx") != std::string::npos ||
         lower.find("directx") != std::string::npos;
}

static void SetIdentityMatrix(float out[16]) {
  if (!out) {
    return;
  }
  for (int index = 0; index < 16; ++index) {
    out[index] = 0.0f;
  }
  out[0] = 1.0f;
  out[5] = 1.0f;
  out[10] = 1.0f;
  out[15] = 1.0f;
}

static void MultiplyColumnMajor(const float lhs[16], const float rhs[16],
                                float out[16]) {
  float result[16] = {0.0f};
  for (int row = 0; row < 4; ++row) {
    for (int col = 0; col < 4; ++col) {
      float sum = 0.0f;
      for (int k = 0; k < 4; ++k) {
        sum += lhs[k * 4 + row] * rhs[col * 4 + k];
      }
      result[col * 4 + row] = sum;
    }
  }
  memcpy(out, result, sizeof(result));
}

static void CopyAiMatrixToColumnMajor(const aiMatrix4x4 &matrix, float out[16]) {
  if (!out) {
    return;
  }
  out[0] = matrix.a1;
  out[1] = matrix.b1;
  out[2] = matrix.c1;
  out[3] = matrix.d1;
  out[4] = matrix.a2;
  out[5] = matrix.b2;
  out[6] = matrix.c2;
  out[7] = matrix.d2;
  out[8] = matrix.a3;
  out[9] = matrix.b3;
  out[10] = matrix.c3;
  out[11] = matrix.d3;
  out[12] = matrix.a4;
  out[13] = matrix.b4;
  out[14] = matrix.c4;
  out[15] = matrix.d4;
}

void Initialize(ID3D12Device *device, ID3D12CommandQueue *queue) {
  s_device = device;
  s_queue = queue;
#ifdef USE_SKETCHUP_SDK
  // Initialize SketchUp C API when SDK support is compiled in.
  SUInitialize();
#endif
}

void SetProgressCallback(ProgressCallback cb) { s_progressCb = cb; }

void ClearProgressCallback() { s_progressCb = ProgressCallback(); }

void SetDeferGpuUpload(bool enable) { s_deferGpuUpload = enable; }

bool GetDeferGpuUpload() { return s_deferGpuUpload; }

void SetTextureCompressionMode(TextureCompressionMode mode) {
  s_textureCompressionMode = mode;
}

TextureCompressionMode GetTextureCompressionMode() {
  return s_textureCompressionMode;
}

const char *TextureCompressionModeName(TextureCompressionMode mode) {
  switch (mode) {
  case TextureCompressionMode::Off:
    return "Off";
  case TextureCompressionMode::Balanced:
    return "Balanced";
  case TextureCompressionMode::HighQuality:
    return "High Quality";
  default:
    return "Unknown";
  }
}

const char *TextureUsageSemanticName(TextureUsageSemantic semantic) {
  switch (semantic) {
  case TextureUsageSemantic::Color:
    return "Color";
  case TextureUsageSemantic::ColorAlpha:
    return "Color + Alpha";
  case TextureUsageSemantic::Normal:
    return "Normal";
  case TextureUsageSemantic::PackedSurface:
    return "Packed Surface";
  case TextureUsageSemantic::Scalar:
    return "Scalar";
  case TextureUsageSemantic::Emissive:
    return "Emissive";
  case TextureUsageSemantic::Hdr:
    return "HDR";
  default:
    return "Unknown";
  }
}

// ... rest of the file ... (excluding LoadGltf until later)

static void WaitForQueueIdle(ID3D12CommandQueue *queue) {
  // Create a temporary fence and wait until GPU has finished executing.
  ComPtr<ID3D12Fence> fence;
  ThrowIfFailed(
      s_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
  HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  const UINT64 fenceValue = 1;
  ThrowIfFailed(queue->Signal(fence.Get(), fenceValue));
  if (fence->GetCompletedValue() < fenceValue) {
    ThrowIfFailed(fence->SetEventOnCompletion(fenceValue, event));
    WaitForSingleObject(event, INFINITE);
  }
  CloseHandle(event);
}

static void ExecuteCommandListAndWait(ID3D12GraphicsCommandList *cmdList) {
  ThrowIfFailed(cmdList->Close());
  ID3D12CommandList *lists[] = {cmdList};
  s_queue->ExecuteCommandLists(1, lists);
  WaitForQueueIdle(s_queue.Get());
}

inline uint32_t ComputeMipLevels(uint32_t width, uint32_t height) {
  if (width == 0 || height == 0)
    return 1;
  return static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) +
         1;
}

static bool IsBlockCompressedTextureFormat(DXGI_FORMAT format) {
  switch (format) {
  case DXGI_FORMAT_BC1_UNORM:
  case DXGI_FORMAT_BC1_UNORM_SRGB:
  case DXGI_FORMAT_BC2_UNORM:
  case DXGI_FORMAT_BC2_UNORM_SRGB:
  case DXGI_FORMAT_BC3_UNORM:
  case DXGI_FORMAT_BC3_UNORM_SRGB:
  case DXGI_FORMAT_BC4_UNORM:
  case DXGI_FORMAT_BC4_SNORM:
  case DXGI_FORMAT_BC5_UNORM:
  case DXGI_FORMAT_BC5_SNORM:
  case DXGI_FORMAT_BC6H_UF16:
  case DXGI_FORMAT_BC6H_SF16:
  case DXGI_FORMAT_BC7_UNORM:
  case DXGI_FORMAT_BC7_UNORM_SRGB:
    return true;
  default:
    return false;
  }
}

static size_t TextureBlockBytes(DXGI_FORMAT format) {
  switch (format) {
  case DXGI_FORMAT_BC1_UNORM:
  case DXGI_FORMAT_BC1_UNORM_SRGB:
  case DXGI_FORMAT_BC4_UNORM:
  case DXGI_FORMAT_BC4_SNORM:
    return 8;
  case DXGI_FORMAT_BC2_UNORM:
  case DXGI_FORMAT_BC2_UNORM_SRGB:
  case DXGI_FORMAT_BC3_UNORM:
  case DXGI_FORMAT_BC3_UNORM_SRGB:
  case DXGI_FORMAT_BC5_UNORM:
  case DXGI_FORMAT_BC5_SNORM:
  case DXGI_FORMAT_BC6H_UF16:
  case DXGI_FORMAT_BC6H_SF16:
  case DXGI_FORMAT_BC7_UNORM:
  case DXGI_FORMAT_BC7_UNORM_SRGB:
    return 16;
  default:
    return 0;
  }
}

static size_t TextureBytesPerPixel(DXGI_FORMAT format) {
  switch (format) {
  case DXGI_FORMAT_R32G32B32A32_FLOAT:
    return 16;
  case DXGI_FORMAT_R8G8B8A8_UNORM:
  case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
  case DXGI_FORMAT_B8G8R8A8_UNORM:
  case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
  case DXGI_FORMAT_B8G8R8X8_UNORM:
  case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
    return 4;
  default:
    return 0;
  }
}

static size_t TightlyPackedMipSize(DXGI_FORMAT format, uint32_t width,
                                   uint32_t height) {
  if (IsBlockCompressedTextureFormat(format)) {
    const size_t blockBytes = TextureBlockBytes(format);
    const size_t blockW = (width + 3u) / 4u;
    const size_t blockH = (height + 3u) / 4u;
    return blockW * blockH * blockBytes;
  }
  return static_cast<size_t>(width) * static_cast<size_t>(height) *
         TextureBytesPerPixel(format);
}

static bool CreateGpuTexture(const void *src, int width, int height,
                             int components, DXGI_FORMAT format,
                             Texture &outTex) {
  if (!s_device || !src)
    return false;

  uint32_t mipLevels = ComputeMipLevels(width, height);

  D3D12_RESOURCE_DESC texDesc = {};
  texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  texDesc.Width = (UINT)width;
  texDesc.Height = (UINT)height;
  texDesc.DepthOrArraySize = 1;
  texDesc.MipLevels = (UINT16)mipLevels;
  texDesc.Format = format;
  texDesc.SampleDesc.Count = 1;
  texDesc.SampleDesc.Quality = 0;
  texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

  D3D12_HEAP_PROPERTIES defaultHeapProps = {D3D12_HEAP_TYPE_DEFAULT,
                                            D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                                            D3D12_MEMORY_POOL_UNKNOWN, 1, 1};
  ThrowIfFailed(s_device->CreateCommittedResource(
      &defaultHeapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
      D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&outTex.resource)));

  std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(mipLevels);
  std::vector<UINT> numRows(mipLevels);
  std::vector<UINT64> rowPitches(mipLevels);
  UINT64 totalBytes = 0;
  s_device->GetCopyableFootprints(&texDesc, 0, (UINT)mipLevels, 0,
                                  footprints.data(), numRows.data(),
                                  rowPitches.data(), &totalBytes);

  D3D12_HEAP_PROPERTIES uploadHeapProps = {D3D12_HEAP_TYPE_UPLOAD,
                                           D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                                           D3D12_MEMORY_POOL_UNKNOWN, 1, 1};
  D3D12_RESOURCE_DESC bufDesc = {};
  bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  bufDesc.Width = totalBytes;
  bufDesc.Height = 1;
  bufDesc.DepthOrArraySize = 1;
  bufDesc.MipLevels = 1;
  bufDesc.Format = DXGI_FORMAT_UNKNOWN;
  bufDesc.SampleDesc.Count = 1;
  bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

  ComPtr<ID3D12Resource> uploadBuffer;
  ThrowIfFailed(s_device->CreateCommittedResource(
      &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer)));

  void *mapped = nullptr;
  ThrowIfFailed(uploadBuffer->Map(0, nullptr, &mapped));

  int bpp = (format == DXGI_FORMAT_R32G32B32A32_FLOAT) ? 16 : 4;

  // Copy base level and generate mips in system memory
  std::vector<uint8_t> mipMemory;
  const uint8_t *currentSrc = (const uint8_t *)src;
  int curW = width;
  int curH = height;

  for (uint32_t i = 0; i < mipLevels; ++i) {
    // Copy current level to upload buffer
    for (int y = 0; y < curH; ++y) {
      memcpy((uint8_t *)mapped + footprints[i].Offset +
                 (size_t)y * footprints[i].Footprint.RowPitch,
             currentSrc + (size_t)y * curW * bpp, (size_t)curW * bpp);
    }

    if (i + 1 < mipLevels) {
      int nextW = std::max(1, curW >> 1);
      int nextH = std::max(1, curH >> 1);
      std::vector<uint8_t> nextMip(nextW * nextH * bpp);

      if (format == DXGI_FORMAT_R32G32B32A32_FLOAT) {
        const float *s = (const float *)currentSrc;
        float *d = (float *)nextMip.data();
        for (int y = 0; y < nextH; ++y) {
          for (int x = 0; x < nextW; ++x) {
            float r = 0, g = 0, b = 0, a = 0;
            for (int sy = 0; sy < 2; ++sy) {
              for (int sx = 0; sx < 2; ++sx) {
                int sX = std::min(x * 2 + sx, curW - 1);
                int sY = std::min(y * 2 + sy, curH - 1);
                const float *p = s + (sY * curW + sX) * 4;
                r += p[0];
                g += p[1];
                b += p[2];
                a += p[3];
              }
            }
            float *pDst = d + (y * nextW + x) * 4;
            pDst[0] = r * 0.25f;
            pDst[1] = g * 0.25f;
            pDst[2] = b * 0.25f;
            pDst[3] = a * 0.25f;
          }
        }
      } else {
        for (int y = 0; y < nextH; ++y) {
          for (int x = 0; x < nextW; ++x) {
            int r = 0, g = 0, b = 0, a = 0;
            for (int sy = 0; sy < 2; ++sy) {
              for (int sx = 0; sx < 2; ++sx) {
                int sX = std::min(x * 2 + sx, curW - 1);
                int sY = std::min(y * 2 + sy, curH - 1);
                const uint8_t *p = currentSrc + (sY * curW + sX) * 4;
                r += p[0];
                g += p[1];
                b += p[2];
                a += p[3];
              }
            }
            uint8_t *pDst = nextMip.data() + (y * nextW + x) * 4;
            pDst[0] = (uint8_t)(r / 4);
            pDst[1] = (uint8_t)(g / 4);
            pDst[2] = (uint8_t)(b / 4);
            pDst[3] = (uint8_t)(a / 4);
          }
        }
      }
      mipMemory = std::move(nextMip);
      currentSrc = mipMemory.data();
      curW = nextW;
      curH = nextH;
    }
  }

  uploadBuffer->Unmap(0, nullptr);

  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> cmdList;
  ThrowIfFailed(s_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                 IID_PPV_ARGS(&allocator)));
  ThrowIfFailed(s_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                            allocator.Get(), nullptr,
                                            IID_PPV_ARGS(&cmdList)));

  for (uint32_t i = 0; i < mipLevels; ++i) {
    D3D12_TEXTURE_COPY_LOCATION dst = {
        outTex.resource.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, i};
    D3D12_TEXTURE_COPY_LOCATION srcLoc = {
        uploadBuffer.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
        footprints[i]};
    cmdList->CopyTextureRegion(&dst, 0, 0, 0, &srcLoc, nullptr);
  }

  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = outTex.resource.Get();
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
  cmdList->ResourceBarrier(1, &barrier);

  ExecuteCommandListAndWait(cmdList.Get());

  outTex.width = width;
  outTex.height = height;
  outTex.format = format;
  outTex.mipLevels = mipLevels;
  outTex.cpuFormat = format;
  outTex.cpuMipLevels = 1;
  outTex.gpuCompressed = IsBlockCompressedTextureFormat(format);
  outTex.compressionMode = TextureCompressionMode::Off;

  // Store CPU data for serialization
  bpp = (format == DXGI_FORMAT_R32G32B32A32_FLOAT) ? 16 : 4;
  outTex.cpuData.assign((const uint8_t *)src,
                        (const uint8_t *)src + (size_t)width * height * bpp);

  return true;
}

Texture LoadTextureFromFile(const std::string &path, bool isHDR) {
  if (!s_device)
    return {};

  // Helper: robustly convert path bytes to a Windows wide string.
  // Try UTF-8 first; if that doesn't map to an existing file, fall back to
  // the ANSI code page (CP_ACP). This fixes paths produced by Assimp on
  // Windows that use the local code page for non-ASCII characters.
  auto ConvertPathToWString = [](const std::string &s) -> std::wstring {
    if (s.empty())
      return {};

    std::wstring utf8w;
    // Try UTF-8 (fail on invalid sequences)
    int req = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), -1,
                                  nullptr, 0);
    if (req) {
      utf8w.resize(req);
      if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), -1,
                              &utf8w[0], req)) {
        if (!utf8w.empty() && utf8w.back() == L'\0')
          utf8w.pop_back();
        try {
          if (std::filesystem::exists(std::filesystem::path(utf8w)))
            return utf8w;
        } catch (...) {
          /* ignore filesystem errors */
        }
      }
    }

    // Fallback: interpret input as ANSI (system code page)
    int req2 = MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, nullptr, 0);
    if (req2) {
      std::wstring acpw(req2, 0);
      if (MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, &acpw[0], req2)) {
        if (!acpw.empty() && acpw.back() == L'\0')
          acpw.pop_back();
        try {
          if (std::filesystem::exists(std::filesystem::path(acpw)))
            return acpw;
        } catch (...) {
          /* ignore filesystem errors */
        }
        return acpw; // return ACP conversion if UTF-8 didn't resolve to an
                     // existing file
      }
    }

    // Final fallback: return the UTF-8 conversion (may be empty)
    return utf8w;
  };

  int width = 0, height = 0, comp = 0;

  if (isHDR) {
    // EXR via tinyexr (filename API)
    if (path.find(".exr") != std::string::npos) {
      float *data = nullptr;
      const char *err = nullptr;
      int ret = LoadEXR(&data, &width, &height, path.c_str(), &err);
      if (ret != TINYEXR_SUCCESS) {
        if (err) {
          fprintf(stderr, "tinyexr error: %s\n", err);
          FreeEXRErrorMessage(err);
        }
        return {};
      }
      Texture tex;
      CreateGpuTexture(data, width, height, 4, DXGI_FORMAT_R32G32B32A32_FLOAT,
                       tex);
      free(data);
      return tex;
    }

    // HDR (non-EXR) — try stbi_loadf first, then fallback to wide-path file I/O
    float *fdata = stbi_loadf(path.c_str(), &width, &height, &comp, 4);
    if (!fdata) {
      std::wstring wpath = ConvertPathToWString(path);
      if (!wpath.empty()) {
        FILE *wf = _wfopen(wpath.c_str(), L"rb");
        if (wf) {
          fdata = stbi_loadf_from_file(wf, &width, &height, &comp, 4);
          fclose(wf);
        }
      }
    }

    if (!fdata)
      return {};

    Texture tex;
    CreateGpuTexture(fdata, width, height, 4, DXGI_FORMAT_R32G32B32A32_FLOAT,
                     tex);
    stbi_image_free(fdata);
    return tex;
  } else {
    // LDR images: try stbi_load then fallback to wide-path _wfopen +
    // stbi_load_from_file
    unsigned char *data = stbi_load(path.c_str(), &width, &height, &comp, 4);
    if (!data) {
      std::wstring wpath = ConvertPathToWString(path);
      if (!wpath.empty()) {
        FILE *wf = _wfopen(wpath.c_str(), L"rb");
        if (wf) {
          data = stbi_load_from_file(wf, &width, &height, &comp, 4);
          fclose(wf);
        }
      }
    }

    if (!data)
      return {};

    Texture tex;
    CreateGpuTexture(data, width, height, 4, DXGI_FORMAT_R8G8B8A8_UNORM, tex);
    stbi_image_free(data);
    return tex;
  }
}

static void CreateDefaultBuffer(
    const void *initData, UINT64 byteSize,
    ComPtr<ID3D12Resource> &defaultBuffer, ComPtr<ID3D12Resource> &uploadBuffer,
    D3D12_RESOURCE_STATES finalState = D3D12_RESOURCE_STATE_GENERIC_READ) {
  if (byteSize == 0)
    return;
  D3D12_HEAP_PROPERTIES defaultHeapProps = {};
  defaultHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

  D3D12_RESOURCE_DESC bufferDesc = {};
  bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  bufferDesc.Width = byteSize;
  bufferDesc.Height = 1;
  bufferDesc.DepthOrArraySize = 1;
  bufferDesc.MipLevels = 1;
  bufferDesc.SampleDesc.Count = 1;
  bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

  ThrowIfFailed(s_device->CreateCommittedResource(
      &defaultHeapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
      D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&defaultBuffer)));

  D3D12_HEAP_PROPERTIES uploadHeapProps = {};
  uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

  ThrowIfFailed(s_device->CreateCommittedResource(
      &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer)));

  // Copy init data into upload buffer
  UINT8 *mapped = nullptr;
  D3D12_RANGE readRange = {0, 0};
  ThrowIfFailed(
      uploadBuffer->Map(0, &readRange, reinterpret_cast<void **>(&mapped)));
  memcpy(mapped, initData, static_cast<size_t>(byteSize));
  uploadBuffer->Unmap(0, nullptr);

  // Create a temporary command allocator and list
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> cmdList;
  ThrowIfFailed(s_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                 IID_PPV_ARGS(&allocator)));
  ThrowIfFailed(s_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                            allocator.Get(), nullptr,
                                            IID_PPV_ARGS(&cmdList)));

  // Copy from upload to default
  cmdList->CopyBufferRegion(defaultBuffer.Get(), 0, uploadBuffer.Get(), 0,
                            byteSize);

  // Transition default buffer to the requested final state (defaults to
  // GENERIC_READ)
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = defaultBuffer.Get();
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  barrier.Transition.StateAfter = finalState;
  cmdList->ResourceBarrier(1, &barrier);

  ExecuteCommandListAndWait(cmdList.Get());
}

static void CreateDefaultBufferQueued(
    const void *initData, UINT64 byteSize, ID3D12GraphicsCommandList *cmdList,
    ComPtr<ID3D12Resource> &defaultBuffer, ComPtr<ID3D12Resource> &uploadBuffer,
    D3D12_RESOURCE_STATES finalState = D3D12_RESOURCE_STATE_GENERIC_READ) {
  if (byteSize == 0 || !cmdList)
    return;

  D3D12_HEAP_PROPERTIES defaultHeapProps = {};
  defaultHeapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

  D3D12_RESOURCE_DESC bufferDesc = {};
  bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  bufferDesc.Width = byteSize;
  bufferDesc.Height = 1;
  bufferDesc.DepthOrArraySize = 1;
  bufferDesc.MipLevels = 1;
  bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
  bufferDesc.SampleDesc.Count = 1;
  bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

  ThrowIfFailed(s_device->CreateCommittedResource(
      &defaultHeapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
      D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&defaultBuffer)));

  D3D12_HEAP_PROPERTIES uploadHeapProps = {};
  uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

  ThrowIfFailed(s_device->CreateCommittedResource(
      &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer)));

  UINT8 *mapped = nullptr;
  D3D12_RANGE readRange = {0, 0};
  ThrowIfFailed(
      uploadBuffer->Map(0, &readRange, reinterpret_cast<void **>(&mapped)));
  memcpy(mapped, initData, static_cast<size_t>(byteSize));
  uploadBuffer->Unmap(0, nullptr);

  cmdList->CopyBufferRegion(defaultBuffer.Get(), 0, uploadBuffer.Get(), 0,
                            byteSize);

  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = defaultBuffer.Get();
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  barrier.Transition.StateAfter = finalState;
  cmdList->ResourceBarrier(1, &barrier);
}

#ifdef USE_TINYGLTF

bool LoadGltf(const std::string &path, std::vector<GpuMesh> &outMeshes,
              std::vector<Material> *outMaterials,
              std::vector<Texture> *outTextures, const float *rootTranslation,
              std::vector<ImportedSceneNode> *outSceneNodes) {
  std::ostringstream oss;
  oss << "Asset::LoadGltf (tinygltf) requested: " << path << "\n";
  fprintf(stderr, "%s", oss.str().c_str());

  const std::filesystem::path nativePath = NativePathFromUtf8(path);
  std::error_code ec;
  if (!std::filesystem::exists(nativePath, ec)) {
    fprintf(stderr, "File not found: glTF path does not exist\n");
    return false;
  }

  tinygltf::Model model;
  tinygltf::TinyGLTF loader;
  std::string err;
  std::string warn;

  bool isBinary = (path.size() >= 4 && path.substr(path.size() - 4) == ".glb");
  bool ret;

  // Diagnostics: print file size and header bytes before calling loader
  uint64_t fsize = 0;
  try {
    fsize = std::filesystem::file_size(nativePath, ec);
  } catch (...) {
    ec.assign(1, std::generic_category());
  }
  if (ec) {
    fprintf(stderr, "LoadGltf: failed to stat file '%s': %s\n", path.c_str(),
            ec.message().c_str());
  } else {
    fprintf(stderr, "LoadGltf: file size = %llu bytes\n",
            (unsigned long long)fsize);
  }

  if (isBinary) {
    std::ifstream in(nativePath, std::ios::binary);
    if (in) {
      unsigned char header[4] = {0, 0, 0, 0};
      in.read(reinterpret_cast<char *>(header), sizeof(header));
      fprintf(stderr,
              "LoadGltf: header bytes: %02x %02x %02x %02x ('%c%c%c%c')\n",
              header[0], header[1], header[2], header[3],
              isprint(header[0]) ? header[0] : '?',
              isprint(header[1]) ? header[1] : '?',
              isprint(header[2]) ? header[2] : '?',
              isprint(header[3]) ? header[3] : '?');
    } else {
      fprintf(stderr,
              "LoadGltf: failed to open file for header inspection: %s\n",
              path.c_str());
    }
    ret = loader.LoadBinaryFromFile(&model, &err, &warn, path);
  } else {
    ret = loader.LoadASCIIFromFile(&model, &err, &warn, path);
  }
  if (!warn.empty())
    fprintf(stderr, "%s", warn.c_str());
  if (!err.empty())
    fprintf(stderr, "%s", err.c_str());
  if (!ret) {
    fprintf(stderr, "tinygltf: Failed to load glTF file\n");
    return false;
  }

  std::ostringstream info;
  info << "Loaded glTF: " << path << " | scenes=" << model.scenes.size()
       << " nodes=" << model.nodes.size() << " meshes=" << model.meshes.size()
       << " images=" << model.images.size() << "\n";
  fprintf(stderr, "%s", info.str().c_str());

  // Optionally prepare textures and materials containers
  std::vector<Texture> tmpTextures;
  std::vector<Material> tmpMaterials;
  bool wantTextures = (outTextures != nullptr);
  bool wantMaterials = (outMaterials != nullptr);

  // Helper: create a default RGBA8 texture from image bytes and upload to GPU
  auto CreateTextureFromImage = [&](const unsigned char *src, int width,
                                    int height, int components,
                                    Texture &outTex) -> bool {
    if (!s_device)
      return false;

    // Support any component count (1, 2, 3, 4) by converting to RGBA8
    std::vector<unsigned char> rgba;
    const unsigned char *srcPtr = src;
    if (components != 4) {
      rgba.resize(width * height * 4);
      for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
          int si = (y * width + x) * components;
          int di = (y * width + x) * 4;
          if (components == 1) { // Grayscale
            rgba[di + 0] = rgba[di + 1] = rgba[di + 2] = src[si + 0];
            rgba[di + 3] = 255;
          } else if (components == 2) { // Grayscale + Alpha
            rgba[di + 0] = rgba[di + 1] = rgba[di + 2] = src[si + 0];
            rgba[di + 3] = src[si + 1];
          } else if (components == 3) { // RGB
            rgba[di + 0] = src[si + 0];
            rgba[di + 1] = src[si + 1];
            rgba[di + 2] = src[si + 2];
            rgba[di + 3] = 255;
          }
        }
      }
      srcPtr = rgba.data();
    }

    return CreateGpuTexture(srcPtr, width, height, 4,
                            DXGI_FORMAT_R8G8B8A8_UNORM, outTex);
  };

  // If the caller requested materials/textures, load them first
  if (wantTextures && model.images.size() > 0) {
    tmpTextures.resize(model.images.size());
    for (size_t ii = 0; ii < model.images.size(); ++ii) {
      const tinygltf::Image &img = model.images[ii];
      if (img.width > 0 && img.height > 0 && !img.image.empty()) {
        int comp = img.component; // 3 or 4 usually
        const unsigned char *src = img.image.data();
        // tinygltf may store PNG/JPEG raw bytes if not requested to load; but
        // when loaded via Loader it usually decodes. Assume decoded image in
        // img.image; try create texture
        Texture t;
        if (!CreateTextureFromImage(src, img.width, img.height, comp, t)) {
          fprintf(stderr, "Failed to create texture from image\n");
        }
        tmpTextures[ii] = std::move(t);
      } else {
        fprintf(stderr, "Image missing pixel data; skipping texture\n");
      }
    }
  }

  if (wantMaterials && model.materials.size() > 0) {
    tmpMaterials.resize(model.materials.size());
    for (size_t mi = 0; mi < model.materials.size(); ++mi) {
      const tinygltf::Material &m = model.materials[mi];
      Material mat;
      // Use Name if available
      if (!m.name.empty())
        strncpy_s(mat.name, m.name.c_str(), _TRUNCATE);

      // Standard PBR Metallic-Roughness logic
      float baseColorFactor[4] = {1, 1, 1, 1};
      if (m.pbrMetallicRoughness.baseColorFactor.size() >= 4) {
        for (int i = 0; i < 4; ++i)
          baseColorFactor[i] = (float)m.pbrMetallicRoughness.baseColorFactor[i];
      }
      float metallicFactor = (float)m.pbrMetallicRoughness.metallicFactor;
      float roughnessFactor = (float)m.pbrMetallicRoughness.roughnessFactor;

      // Store raw factors. Shaders will handle the Metallic vs Specular split.
      // We use diffuseColor for Base Color factor in GLTF mode.
      for (int c = 0; c < 3; ++c)
        mat.diffuseColor[c] = baseColorFactor[c];
      mat.diffuseColor[3] = baseColorFactor[3];

      mat.metalness = metallicFactor;
      mat.roughness = roughnessFactor;

      // Proper mapping from texture index to image source index
      auto GetImgIdx = [&](int texIdx) {
        if (texIdx >= 0 && texIdx < (int)model.textures.size()) {
          return model.textures[texIdx].source;
        }
        return -1;
      };

      // Emissive
      if (m.emissiveFactor.size() >= 3) {
        for (int i = 0; i < 3; ++i)
          mat.emissiveColor[i] = (float)m.emissiveFactor[i];
      } else if (GetImgIdx(m.emissiveTexture.index) >= 0) {
        // Default to white if an emissive texture is present but no factor is
        // defined
        mat.emissiveColor[0] = mat.emissiveColor[1] = mat.emissiveColor[2] =
            1.0f;
      }
      mat.emissiveColor[3] = 1.0f; // ior default if not packed elsewhere

      // Lower emissive value to 10% when an emissive texture is imported
      mat.emissiveIntensity = 1.0f;
      if (GetImgIdx(m.emissiveTexture.index) >= 0) {
        mat.emissiveIntensity *= 0.1f;
      }

      auto GetExtensionNumber = [&](const tinygltf::Value &value,
                                    const char *key,
                                    double fallback) -> double {
        return value.Has(key) ? value.Get(key).GetNumberAsDouble() : fallback;
      };

      auto khrTransmission = m.extensions.find("KHR_materials_transmission");
      if (khrTransmission != m.extensions.end()) {
        mat.transmissionWeight =
            (float)GetExtensionNumber(khrTransmission->second,
                                      "transmissionFactor", 0.0);
      }

      auto khrIor = m.extensions.find("KHR_materials_ior");
      if (khrIor != m.extensions.end()) {
        mat.ior = (float)GetExtensionNumber(khrIor->second, "ior", mat.ior);
      }

      auto khrSpecular = m.extensions.find("KHR_materials_specular");
      if (khrSpecular != m.extensions.end()) {
        const float specularFactor =
            (float)GetExtensionNumber(khrSpecular->second,
                                      "specularFactor",
                                      mat.specularWeight);
        mat.specularWeight = (std::clamp)(specularFactor, 0.0f, 1.0f);

        if (khrSpecular->second.Has("specularColorFactor")) {
          const auto specColor = khrSpecular->second.Get("specularColorFactor");
          for (int i = 0; i < 3; ++i) {
            mat.specularColor[i] =
                (float)specColor.Get(i).GetNumberAsDouble();
          }
        }
        if (khrSpecular->second.Has("specularColorTexture")) {
          mat.specularColorTexture = GetImgIdx(
              khrSpecular->second.Get("specularColorTexture")
                  .Get("index")
                  .GetNumberAsInt());
        }
      }

      auto khrClearcoat = m.extensions.find("KHR_materials_clearcoat");
      if (khrClearcoat != m.extensions.end()) {
        mat.coatWeight = (float)GetExtensionNumber(khrClearcoat->second,
                                                   "clearcoatFactor", 0.0);
        mat.coatRoughness =
            (float)GetExtensionNumber(khrClearcoat->second,
                                      "clearcoatRoughnessFactor", 0.0);
        if (khrClearcoat->second.Has("clearcoatNormalTexture")) {
          mat.coatNormalTexture = GetImgIdx(
              khrClearcoat->second.Get("clearcoatNormalTexture")
                  .Get("index")
                  .GetNumberAsInt());
        }
      }

      auto khrEmissiveStrength =
          m.extensions.find("KHR_materials_emissive_strength");
      if (khrEmissiveStrength != m.extensions.end()) {
        mat.emissiveIntensity =
            (float)GetExtensionNumber(khrEmissiveStrength->second,
                                      "emissiveStrength",
                                      mat.emissiveIntensity);
      }

      auto khrVolume = m.extensions.find("KHR_materials_volume");
      if (khrVolume != m.extensions.end()) {
        const float thicknessFactor =
            (float)GetExtensionNumber(khrVolume->second,
                                      "thicknessFactor",
                                      0.0);
        mat.thickness = (std::max)(0.0f, thicknessFactor);
        mat.thinWalled = (thicknessFactor > 1e-4f) ? 0.0f : 1.0f;
        mat.attenuationDistance =
            (float)GetExtensionNumber(khrVolume->second,
                                      "attenuationDistance", 0.0);

        if (khrVolume->second.Has("attenuationColor")) {
          const auto attenuationColor = khrVolume->second.Get("attenuationColor");
          for (int i = 0; i < 3; ++i) {
            mat.transmissionColor[i] =
                (float)attenuationColor.Get(i).GetNumberAsDouble();
          }
        }
        if (khrVolume->second.Has("thicknessTexture")) {
          mat.thicknessTexture = GetImgIdx(
              khrVolume->second.Get("thicknessTexture")
                  .Get("index")
                  .GetNumberAsInt());
        }
      } else if (mat.transmissionWeight > 1.0e-5f) {
        // glTF transmission without KHR_materials_volume behaves like thin glass.
        mat.thinWalled = 1.0f;
      }

      auto khrSheen = m.extensions.find("KHR_materials_sheen");
      if (khrSheen != m.extensions.end()) {
        mat.sheenWeight = (float)GetExtensionNumber(khrSheen->second,
                                                    "sheenRoughnessFactor", 0.0);
        if (khrSheen->second.Has("sheenColorFactor")) {
          const auto sheenColor = khrSheen->second.Get("sheenColorFactor");
          for (int i = 0; i < 3; ++i) {
            mat.sheenColor[i] = (float)sheenColor.Get(i).GetNumberAsDouble();
          }
          mat.sheenWeight = (std::max)(
              mat.sheenWeight,
              (std::max)(mat.sheenColor[0],
                         (std::max)(mat.sheenColor[1], mat.sheenColor[2])));
        }
      }

      int baseColorTexIdx =
          GetImgIdx(m.pbrMetallicRoughness.baseColorTexture.index);
      mat.diffuseTexture = baseColorTexIdx;
      mat.normalTexture = GetImgIdx(m.normalTexture.index);
      // glTF normal textures use the renderer's native OpenGL-style +Y basis.
      mat.normalMapFlipY = false;
      mat.emissiveTexture = GetImgIdx(m.emissiveTexture.index);
      mat.occlusionTexture = GetImgIdx(m.occlusionTexture.index);
      mat.metalRoughTexture =
          GetImgIdx(m.pbrMetallicRoughness.metallicRoughnessTexture.index);

      // If texture creation failed for a referenced image, clear the slot.
      // This avoids sampling uninitialized descriptors (often shows up as
      // black).
      auto ClearIfInvalid = [&](int &texIdx) {
        if (texIdx < 0)
          return;
        if (texIdx >= (int)tmpTextures.size()) {
          texIdx = -1;
          return;
        }
        if (!tmpTextures[texIdx].resource)
          texIdx = -1;
      };
      ClearIfInvalid(mat.diffuseTexture);
      ClearIfInvalid(mat.normalTexture);
      ClearIfInvalid(mat.emissiveTexture);
      ClearIfInvalid(mat.occlusionTexture);
      ClearIfInvalid(mat.metalRoughTexture);
      ClearIfInvalid(mat.specularColorTexture);
      ClearIfInvalid(mat.thicknessTexture);
      ClearIfInvalid(mat.coatNormalTexture);

      mat.doubleSided = m.doubleSided;
      if (!m.alphaMode.empty())
        mat.alphaMode = m.alphaMode;
      mat.alphaCutoff = (std::clamp)((float)m.alphaCutoff, 0.0f, 1.0f);

      // Handle KHR_materials_pbrSpecularGlossiness extension - Reference for
      // proper Glossiness workflow
      auto khrSpec = m.extensions.find("KHR_materials_pbrSpecularGlossiness");
      if (khrSpec != m.extensions.end()) {
        const auto &ext = khrSpec->second;
        if (ext.Has("diffuseFactor")) {
          auto p = ext.Get("diffuseFactor");
          for (int i = 0; i < 4; ++i)
            mat.diffuseColor[i] = (float)p.Get(i).GetNumberAsDouble();
        }
        if (ext.Has("specularFactor")) {
          auto p = ext.Get("specularFactor");
          for (int i = 0; i < 3; ++i) {
            mat.specularColor[i] = (float)p.Get(i).GetNumberAsDouble();
          }
          mat.specularWeight =
              (std::clamp)((std::max)(
                               mat.specularColor[0],
                               (std::max)(mat.specularColor[1],
                                          mat.specularColor[2])),
                           0.0f, 1.0f);
        }
        if (ext.Has("glossinessFactor")) {
          mat.roughness =
              1.0f - (float)ext.Get("glossinessFactor").GetNumberAsDouble();
        }
        if (ext.Has("diffuseTexture")) {
          mat.diffuseTexture = GetImgIdx(
              ext.Get("diffuseTexture").Get("index").GetNumberAsInt());
        }

        // Validate any overridden texture slots as well.
        if (mat.diffuseTexture >= 0 &&
            mat.diffuseTexture < (int)tmpTextures.size() &&
            !tmpTextures[mat.diffuseTexture].resource)
          mat.diffuseTexture = -1;
      }
      const float emissiveMax =
          (std::max)(mat.emissiveColor[0],
                     (std::max)(mat.emissiveColor[1], mat.emissiveColor[2])) *
          (std::max)(mat.emissiveIntensity, 0.0f);
      if (emissiveMax > 1.0e-4f) {
        mat.materialClass = Material::kMaterialClassEmissive;
      } else if (mat.transmissionWeight > 1.0e-5f ||
                 mat.thinWalled > 0.5f) {
        mat.materialClass = Material::kMaterialClassGlass;
      } else if (mat.metalness > 0.5f) {
        mat.materialClass = Material::kMaterialClassMetal;
      } else if (mat.sheenWeight > 1.0e-4f) {
        mat.materialClass = Material::kMaterialClassFabric;
      } else {
        mat.materialClass = Material::kMaterialClassGeneric;
      }
      mat.schemaVersion = Material::kSchemaVersionCoronaArchviz;
      tmpMaterials[mi] = std::move(mat);
    }
  }

  auto GetCompSize = [](int compType) -> size_t {
    if (compType == TINYGLTF_COMPONENT_TYPE_FLOAT)
      return 4;
    if (compType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT ||
        compType == TINYGLTF_COMPONENT_TYPE_SHORT)
      return 2;
    if (compType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE ||
        compType == TINYGLTF_COMPONENT_TYPE_BYTE)
      return 1;
    return 4;
  };

  auto GetNumComps = [](int type) -> size_t {
    if (type == TINYGLTF_TYPE_VEC4)
      return 4;
    if (type == TINYGLTF_TYPE_VEC3)
      return 3;
    if (type == TINYGLTF_TYPE_VEC2)
      return 2;
    if (type == TINYGLTF_TYPE_SCALAR)
      return 1;
    return 0;
  };

  // Helper to extract data from tinygltf accessors robustly
  auto GetAccessorData = [&](int accessorIdx, const unsigned char *&dataOut,
                             size_t &strideOut, size_t &countOut, int &typeOut,
                             int &compTypeOut) -> bool {
    if (accessorIdx < 0)
      return false;
    const auto &acc = model.accessors[accessorIdx];
    if (acc.bufferView < 0)
      return false;
    const auto &view = model.bufferViews[acc.bufferView];
    dataOut = model.buffers[view.buffer].data.data() + view.byteOffset +
              acc.byteOffset;
    strideOut = acc.ByteStride(view);
    if (strideOut == 0) {
      strideOut = GetCompSize(acc.componentType) * GetNumComps(acc.type);
    }
    countOut = acc.count;
    typeOut = acc.type;
    compTypeOut = acc.componentType;
    return true;
  };

  auto ReadVec2 = [&](const unsigned char *data, int compType, float *out) {
    for (int i = 0; i < 2; ++i) {
      if (compType == TINYGLTF_COMPONENT_TYPE_FLOAT)
        out[i] = reinterpret_cast<const float *>(data)[i];
      else if (compType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
        out[i] = (float)reinterpret_cast<const uint16_t *>(data)[i] / 65535.0f;
      else if (compType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
        out[i] = (float)data[i] / 255.0f;
    }
  };

  auto ReadVec3 = [&](const unsigned char *data, int compType, float *out) {
    for (int i = 0; i < 3; ++i) {
      if (compType == TINYGLTF_COMPONENT_TYPE_FLOAT)
        out[i] = reinterpret_cast<const float *>(data)[i];
      else if (compType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
        out[i] = (float)reinterpret_cast<const uint16_t *>(data)[i] / 65535.0f;
      else if (compType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
        out[i] = (float)data[i] / 255.0f;
    }
  };

  auto ReadVec4 = [&](const unsigned char *data, int compType, float *out) {
    for (int i = 0; i < 4; ++i) {
      if (compType == TINYGLTF_COMPONENT_TYPE_FLOAT)
        out[i] = reinterpret_cast<const float *>(data)[i];
      else if (compType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
        out[i] = (float)reinterpret_cast<const uint16_t *>(data)[i] / 65535.0f;
      else if (compType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
        out[i] = (float)data[i] / 255.0f;
    }
  };

  auto BuildNodeTransform = [](const tinygltf::Node &node, float out[16]) {
    SetIdentityMatrix(out);
    if (node.matrix.size() == 16) {
      for (int i = 0; i < 16; ++i) {
        out[i] = (float)node.matrix[i];
      }
      return;
    }

    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float scaleZ = 1.0f;
    if (node.scale.size() == 3) {
      scaleX = (float)node.scale[0];
      scaleY = (float)node.scale[1];
      scaleZ = (float)node.scale[2];
    }

    float rotation[9] = {1.0f, 0.0f, 0.0f,
                         0.0f, 1.0f, 0.0f,
                         0.0f, 0.0f, 1.0f};
    if (node.rotation.size() == 4) {
      const float qx = (float)node.rotation[0];
      const float qy = (float)node.rotation[1];
      const float qz = (float)node.rotation[2];
      const float qw = (float)node.rotation[3];
      rotation[0] = 1.0f - 2.0f * qy * qy - 2.0f * qz * qz;
      rotation[1] = 2.0f * qx * qy - 2.0f * qz * qw;
      rotation[2] = 2.0f * qx * qz + 2.0f * qy * qw;
      rotation[3] = 2.0f * qx * qy + 2.0f * qz * qw;
      rotation[4] = 1.0f - 2.0f * qx * qx - 2.0f * qz * qz;
      rotation[5] = 2.0f * qy * qz - 2.0f * qx * qw;
      rotation[6] = 2.0f * qx * qz - 2.0f * qy * qw;
      rotation[7] = 2.0f * qy * qz + 2.0f * qx * qw;
      rotation[8] = 1.0f - 2.0f * qx * qx - 2.0f * qy * qy;
    }

    out[0] = rotation[0] * scaleX;
    out[1] = rotation[3] * scaleX;
    out[2] = rotation[6] * scaleX;
    out[4] = rotation[1] * scaleY;
    out[5] = rotation[4] * scaleY;
    out[6] = rotation[7] * scaleY;
    out[8] = rotation[2] * scaleZ;
    out[9] = rotation[5] * scaleZ;
    out[10] = rotation[8] * scaleZ;

    if (node.translation.size() == 3) {
      out[12] = (float)node.translation[0];
      out[13] = (float)node.translation[1];
      out[14] = (float)node.translation[2];
    }
  };

  if (outSceneNodes) {
    outSceneNodes->clear();

    std::vector<std::vector<size_t>> meshPrimitiveRemap(model.meshes.size());
    for (size_t meshIndex = 0; meshIndex < model.meshes.size(); ++meshIndex) {
      const auto &mesh = model.meshes[meshIndex];
      meshPrimitiveRemap[meshIndex].reserve(mesh.primitives.size());
      for (size_t primitiveIndex = 0; primitiveIndex < mesh.primitives.size();
           ++primitiveIndex) {
        const auto &prim = mesh.primitives[primitiveIndex];
        if (prim.mode != TINYGLTF_MODE_TRIANGLES &&
            prim.mode != TINYGLTF_MODE_TRIANGLE_STRIP &&
            prim.mode != TINYGLTF_MODE_TRIANGLE_FAN) {
          meshPrimitiveRemap[meshIndex].push_back(static_cast<size_t>(-1));
          continue;
        }

        const unsigned char *posData = nullptr;
        const unsigned char *normData = nullptr;
        const unsigned char *uvData = nullptr;
        const unsigned char *tanData = nullptr;
        size_t posStride = 0;
        size_t normStride = 0;
        size_t uvStride = 0;
        size_t tanStride = 0;
        size_t vertexCount = 0;
        int posType = 0;
        int posComp = 0;
        int normType = 0;
        int normComp = 0;
        int uvType = 0;
        int uvComp = 0;
        int tanType = 0;
        int tanComp = 0;

        auto posIt = prim.attributes.find("POSITION");
        if (posIt == prim.attributes.end() ||
            !GetAccessorData(posIt->second, posData, posStride, vertexCount,
                             posType, posComp)) {
          meshPrimitiveRemap[meshIndex].push_back(static_cast<size_t>(-1));
          continue;
        }
        if (posStride == 0) {
          posStride = sizeof(float) * 3;
        }

        bool hasNormal = false;
        auto normIt = prim.attributes.find("NORMAL");
        if (normIt != prim.attributes.end() &&
            GetAccessorData(normIt->second, normData, normStride, vertexCount,
                            normType, normComp)) {
          if (normStride == 0) {
            normStride = sizeof(float) * 3;
          }
          hasNormal = true;
        }

        bool hasUv = false;
        auto uvIt = prim.attributes.find("TEXCOORD_0");
        if (uvIt != prim.attributes.end() &&
            GetAccessorData(uvIt->second, uvData, uvStride, vertexCount, uvType,
                            uvComp)) {
          if (uvStride == 0) {
            uvStride = sizeof(float) * 2;
          }
          hasUv = true;
        }

        bool hasTangent = false;
        auto tanIt = prim.attributes.find("TANGENT");
        if (tanIt != prim.attributes.end() &&
            GetAccessorData(tanIt->second, tanData, tanStride, vertexCount,
                            tanType, tanComp)) {
          if (tanStride == 0) {
            tanStride = sizeof(float) * 4;
          }
          hasTangent = true;
        }

        std::vector<Vertex> vertices;
        vertices.reserve(vertexCount);
        float minBound[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
        float maxBound[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
        for (size_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex) {
          float position[3] = {0.0f, 0.0f, 0.0f};
          ReadVec3(posData + vertexIndex * posStride, posComp, position);

          Vertex vertex = {};
          vertex.pos[0] = position[0];
          vertex.pos[1] = position[1];
          vertex.pos[2] = position[2];
          for (int axis = 0; axis < 3; ++axis) {
            minBound[axis] = (std::min)(minBound[axis], vertex.pos[axis]);
            maxBound[axis] = (std::max)(maxBound[axis], vertex.pos[axis]);
          }

          if (hasNormal) {
            ReadVec3(normData + vertexIndex * normStride, normComp,
                     vertex.normal);
          } else {
            vertex.normal[1] = 1.0f;
          }

          if (hasUv) {
            ReadVec2(uvData + vertexIndex * uvStride, uvComp, vertex.uv);
          }

          if (hasTangent) {
            ReadVec4(tanData + vertexIndex * tanStride, tanComp,
                     vertex.tangent);
          } else {
            vertex.tangent[0] = 1.0f;
            vertex.tangent[3] = 1.0f;
          }

          vertices.push_back(vertex);
        }

        std::vector<uint32_t> indices;
        if (prim.indices >= 0) {
          const unsigned char *idxData = nullptr;
          size_t idxStride = 0;
          size_t idxCount = 0;
          int idxType = 0;
          int idxComp = 0;
          if (GetAccessorData(prim.indices, idxData, idxStride, idxCount,
                              idxType, idxComp)) {
            indices.resize(idxCount);
            for (size_t index = 0; index < idxCount; ++index) {
              if (idxComp == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                indices[index] =
                    *reinterpret_cast<const uint16_t *>(idxData + index * 2);
              } else if (idxComp == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
                indices[index] =
                    *reinterpret_cast<const uint32_t *>(idxData + index * 4);
              } else if (idxComp == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                indices[index] = idxData[index];
              }
            }
          }
        } else {
          indices.resize(vertexCount);
          for (uint32_t index = 0; index < static_cast<uint32_t>(vertexCount);
               ++index) {
            indices[index] = index;
          }
        }

        if (vertices.empty() || indices.empty()) {
          meshPrimitiveRemap[meshIndex].push_back(static_cast<size_t>(-1));
          continue;
        }

        GpuMesh gpuMesh = LoadMeshFromMemory(vertices, indices);
        gpuMesh.materialIndex = prim.material;
        gpuMesh.materialSlot = prim.material;
        for (int axis = 0; axis < 3; ++axis) {
          gpuMesh.minBound[axis] = minBound[axis];
          gpuMesh.maxBound[axis] = maxBound[axis];
        }
        const size_t loadedIndex = outMeshes.size();
        outMeshes.push_back(std::move(gpuMesh));
        meshPrimitiveRemap[meshIndex].push_back(loadedIndex);
      }
    }

    std::vector<int> parentIndices(model.nodes.size(), -1);
    for (size_t nodeIndex = 0; nodeIndex < model.nodes.size(); ++nodeIndex) {
      for (int childIndex : model.nodes[nodeIndex].children) {
        if (childIndex >= 0 && childIndex < static_cast<int>(model.nodes.size())) {
          parentIndices[(size_t)childIndex] = static_cast<int>(nodeIndex);
        }
      }
    }

    std::vector<float> identity(16, 0.0f);
    for (int index = 0; index < 4; ++index) {
      identity[index * 4 + index] = 1.0f;
    }
    if (rootTranslation) {
      identity[12] = rootTranslation[0];
      identity[13] = rootTranslation[1];
      identity[14] = rootTranslation[2];
    }

    std::vector<std::vector<float>> worldTransforms(model.nodes.size(),
                                                    std::vector<float>(16, 0.0f));
    std::vector<bool> visited(model.nodes.size(), false);
    auto traverseNode = [&](auto self, int nodeIndex,
                            const std::vector<float> &parentTransform) -> void {
      if (nodeIndex < 0 || nodeIndex >= static_cast<int>(model.nodes.size())) {
        return;
      }
      const tinygltf::Node &node = model.nodes[(size_t)nodeIndex];
      float localTransform[16];
      BuildNodeTransform(node, localTransform);
      float worldTransform[16];
      MultiplyColumnMajor(parentTransform.data(), localTransform,
                          worldTransform);
      worldTransforms[(size_t)nodeIndex].assign(worldTransform,
                                                worldTransform + 16);
      visited[(size_t)nodeIndex] = true;
      for (int childIndex : node.children) {
        self(self, childIndex, worldTransforms[(size_t)nodeIndex]);
      }
    };

    bool traversedSceneRoots = false;
    if (!model.scenes.empty()) {
      const int defaultSceneIndex =
          model.defaultScene >= 0 &&
                  model.defaultScene < static_cast<int>(model.scenes.size())
              ? model.defaultScene
              : 0;
      for (int nodeIndex : model.scenes[(size_t)defaultSceneIndex].nodes) {
        traverseNode(traverseNode, nodeIndex, identity);
        traversedSceneRoots = true;
      }
    }
    if (!traversedSceneRoots) {
      for (size_t nodeIndex = 0; nodeIndex < model.nodes.size(); ++nodeIndex) {
        if (parentIndices[nodeIndex] >= 0) {
          continue;
        }
        traverseNode(traverseNode, static_cast<int>(nodeIndex), identity);
      }
    }

    std::vector<size_t> importedNodeRemap(model.nodes.size(),
                                          static_cast<size_t>(-1));
    for (size_t nodeIndex = 0; nodeIndex < model.nodes.size(); ++nodeIndex) {
      if (!visited[nodeIndex]) {
        continue;
      }

      ImportedSceneNode sceneNode;
      const tinygltf::Node &node = model.nodes[nodeIndex];
      if (!node.name.empty()) {
        sceneNode.name = node.name;
      } else if (node.mesh >= 0 &&
                 node.mesh < static_cast<int>(model.meshes.size()) &&
                 !model.meshes[(size_t)node.mesh].name.empty()) {
        sceneNode.name = model.meshes[(size_t)node.mesh].name;
      } else {
        sceneNode.name = "Node " + std::to_string(nodeIndex);
      }
      memcpy(sceneNode.transform, worldTransforms[nodeIndex].data(),
             sizeof(sceneNode.transform));

      const int parentNodeIndex = parentIndices[nodeIndex];
      if (parentNodeIndex >= 0 &&
          parentNodeIndex < static_cast<int>(importedNodeRemap.size())) {
        sceneNode.parentIndex = importedNodeRemap[(size_t)parentNodeIndex];
      }

      if (node.mesh >= 0 &&
          node.mesh < static_cast<int>(meshPrimitiveRemap.size())) {
        for (size_t meshIndex : meshPrimitiveRemap[(size_t)node.mesh]) {
          if (meshIndex != static_cast<size_t>(-1)) {
            sceneNode.meshIndices.push_back(meshIndex);
          }
        }
      }

      importedNodeRemap[nodeIndex] = outSceneNodes->size();
      outSceneNodes->push_back(std::move(sceneNode));
    }

    if (outTextures) {
      *outTextures = std::move(tmpTextures);
    }
    if (outMaterials) {
      *outMaterials = std::move(tmpMaterials);
    }
    return !outMeshes.empty() && !outSceneNodes->empty();
  }

  // --- Node traversal to bake transforms into vertices ---
  struct NodeTransform {
    int meshIdx;
    tinygltf::Value matrix;
    std::vector<double> translation, rotation, scale;
  };

  // Helper to get global transform of a node (Column-Major)
  auto GetNodeTransform = [](const tinygltf::Node &node) -> std::vector<float> {
    std::vector<float> mat(16, 0.0f);
    if (node.matrix.size() == 16) {
      for (int i = 0; i < 16; ++i)
        mat[i] = (float)node.matrix[i];
    } else {
      // Start with Identity
      for (int i = 0; i < 4; ++i)
        mat[i * 4 + i] = 1.0f;

      float sx = 1.0f, sy = 1.0f, sz = 1.0f;
      if (node.scale.size() == 3) {
        sx = (float)node.scale[0];
        sy = (float)node.scale[1];
        sz = (float)node.scale[2];
      }

      float r[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
      if (node.rotation.size() == 4) {
        float qx = (float)node.rotation[0], qy = (float)node.rotation[1],
              qz = (float)node.rotation[2], qw = (float)node.rotation[3];
        r[0] = 1 - 2 * qy * qy - 2 * qz * qz;
        r[1] = 2 * qx * qy - 2 * qz * qw;
        r[2] = 2 * qx * qz + 2 * qy * qw;
        r[3] = 2 * qx * qy + 2 * qz * qw;
        r[4] = 1 - 2 * qx * qx - 2 * qz * qz;
        r[5] = 2 * qy * qz - 2 * qx * qw;
        r[6] = 2 * qx * qz - 2 * qy * qw;
        r[7] = 2 * qy * qz + 2 * qx * qw;
        r[8] = 1 - 2 * qx * qx - 2 * qy * qy;
      }

      // Fill Column-Major Matrix from TRS: M = T * R * S
      // Col 0
      mat[0] = r[0] * sx;
      mat[1] = r[3] * sx;
      mat[2] = r[6] * sx;
      // Col 1
      mat[4] = r[1] * sy;
      mat[5] = r[4] * sy;
      mat[6] = r[7] * sy;
      // Col 2
      mat[8] = r[2] * sz;
      mat[9] = r[5] * sz;
      mat[10] = r[8] * sz;

      if (node.translation.size() == 3) {
        mat[12] = (float)node.translation[0];
        mat[13] = (float)node.translation[1];
        mat[14] = (float)node.translation[2];
      }
    }
    return mat;
  };

  // Matrix Multiply (Column-Major)
  auto Multiply = [](const std::vector<float> &A, const std::vector<float> &B) {
    std::vector<float> C(16, 0.0f);
    for (int row = 0; row < 4; ++row) {
      for (int col = 0; col < 4; ++col) {
        float sum = 0.0f;
        for (int k = 0; k < 4; ++k) {
          sum += A[k * 4 + row] * B[col * 4 + k];
        }
        C[col * 4 + row] = sum;
      }
    }
    return C;
  };

  std::vector<std::vector<float>> globalTransforms(model.nodes.size());
  std::vector<bool> visited(model.nodes.size(), false);

  auto Traverse = [&](auto self, int nodeIdx,
                      const std::vector<float> &parentTransform) -> void {
    if (nodeIdx < 0 || nodeIdx >= (int)model.nodes.size())
      return;
    const auto &node = model.nodes[nodeIdx];
    std::vector<float> local = GetNodeTransform(node);
    globalTransforms[nodeIdx] = Multiply(parentTransform, local);
    visited[nodeIdx] = true;
    for (int child : node.children)
      self(self, child, globalTransforms[nodeIdx]);
  };

  std::vector<float> identity(16, 0.0f);
  for (int i = 0; i < 4; ++i)
    identity[i * 4 + i] = 1.0f;

  // Apply initial root translation if provided
  if (rootTranslation) {
    identity[12] = rootTranslation[0];
    identity[13] = rootTranslation[1];
    identity[14] = rootTranslation[2];
  }

  const auto &scene =
      model.scenes[model.defaultScene >= 0 ? model.defaultScene : 0];
  for (int nodeIdx : scene.nodes)
    Traverse(Traverse, nodeIdx, identity);

  for (int ni = 0; ni < (int)model.nodes.size(); ++ni) {
    if (!visited[ni] || model.nodes[ni].mesh < 0)
      continue;
    const auto &node = model.nodes[ni];
    const auto &mesh = model.meshes[node.mesh];
    const auto &worldMat = globalTransforms[ni];

    for (size_t pi = 0; pi < mesh.primitives.size(); ++pi) {
      const auto &prim = mesh.primitives[pi];

      // Only handle TRIANGLES or TRIANGLE_STRIP/STRIP conversions are not
      // implemented
      if (prim.mode != TINYGLTF_MODE_TRIANGLES &&
          prim.mode != TINYGLTF_MODE_TRIANGLE_STRIP &&
          prim.mode != TINYGLTF_MODE_TRIANGLE_FAN) {
        fprintf(stderr, "Skipping non-triangle primitive\n");
        continue;
      }

      const unsigned char *posData = nullptr, *normData = nullptr,
                          *uvData = nullptr, *tanData = nullptr;
      size_t posStride = 0, normStride = 0, uvStride = 0, tanStride = 0;
      size_t vertexCount = 0;
      int posType, posComp, normType, normComp, uvType, uvComp, tanType,
          tanComp;

      auto posIt = prim.attributes.find("POSITION");
      if (posIt == prim.attributes.end() ||
          !GetAccessorData(posIt->second, posData, posStride, vertexCount,
                           posType, posComp)) {
        fprintf(stderr, "Primitive missing POSITION; skipping\n");
        continue;
      }
      if (posStride == 0)
        posStride = sizeof(float) * 3;

      bool hasNormal = false;
      auto normIt = prim.attributes.find("NORMAL");
      if (normIt != prim.attributes.end() &&
          GetAccessorData(normIt->second, normData, normStride, vertexCount,
                          normType, normComp)) {
        if (normStride == 0)
          normStride = sizeof(float) * 3;
        hasNormal = true;
      }

      bool hasUV = false;
      auto uvIt = prim.attributes.find("TEXCOORD_0");
      if (uvIt != prim.attributes.end() &&
          GetAccessorData(uvIt->second, uvData, uvStride, vertexCount, uvType,
                          uvComp)) {
        if (uvStride == 0)
          uvStride = sizeof(float) * 2;
        hasUV = true;
      }

      bool hasTangent = false;
      auto tanIt = prim.attributes.find("TANGENT");
      if (tanIt != prim.attributes.end() &&
          GetAccessorData(tanIt->second, tanData, tanStride, vertexCount,
                          tanType, tanComp)) {
        if (tanStride == 0)
          tanStride = sizeof(float) * 4;
        hasTangent = true;
      }

      // Gather interleaved vertices
      std::vector<Vertex> vertices;
      vertices.reserve(vertexCount);
      float minBound[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
      float maxBound[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};

      for (size_t i = 0; i < vertexCount; ++i) {
        float p[3] = {0, 0, 0};
        ReadVec3(posData + i * posStride, posComp, p);

        float nx = 0.0f, ny = 1.0f, nz = 0.0f;
        if (hasNormal) {
          float n[3];
          ReadVec3(normData + i * normStride, normComp, n);
          nx = n[0];
          ny = n[1];
          nz = n[2];
        }

        float u = 0.0f, v = 0.0f;
        if (hasUV) {
          float uv[2];
          ReadVec2(uvData + i * uvStride, uvComp, uv);
          u = uv[0];
          v = uv[1];
        }

        float tx = 1.0f, ty = 0.0f, tz = 0.0f, tw = 1.0f;
        if (hasTangent) {
          float t[4];
          ReadVec4(tanData + i * tanStride, tanComp, t);
          tx = t[0];
          ty = t[1];
          tz = t[2];
          tw = t[3];
        }

        Vertex vv;
        // Position: P' = M * P
        vv.pos[0] = p[0] * worldMat[0] + p[1] * worldMat[4] +
                    p[2] * worldMat[8] + worldMat[12];
        vv.pos[1] = p[0] * worldMat[1] + p[1] * worldMat[5] +
                    p[2] * worldMat[9] + worldMat[13];
        vv.pos[2] = p[0] * worldMat[2] + p[1] * worldMat[6] +
                    p[2] * worldMat[10] + worldMat[14];

        // Update bounds
        for (int c = 0; c < 3; ++c) {
          if (vv.pos[c] < minBound[c])
            minBound[c] = vv.pos[c];
          if (vv.pos[c] > maxBound[c])
            maxBound[c] = vv.pos[c];
        }

        // Normal: N' = M_3x3 * N
        vv.normal[0] = nx * worldMat[0] + ny * worldMat[4] + nz * worldMat[8];
        vv.normal[1] = nx * worldMat[1] + ny * worldMat[5] + nz * worldMat[9];
        vv.normal[2] = nx * worldMat[2] + ny * worldMat[6] + nz * worldMat[10];

        // Tangent: T' = M_3x3 * T
        vv.tangent[0] = tx * worldMat[0] + ty * worldMat[4] + tz * worldMat[8];
        vv.tangent[1] = tx * worldMat[1] + ty * worldMat[5] + tz * worldMat[9];
        vv.tangent[2] = tx * worldMat[2] + ty * worldMat[6] + tz * worldMat[10];
        vv.tangent[3] = tw;

        vv.uv[0] = u;
        vv.uv[1] = v;
        vertices.push_back(vv);
      }

      // Indices
      std::vector<uint32_t> indices;
      if (prim.indices >= 0) {
        const unsigned char *idxData = nullptr;
        size_t idxStride = 0, idxCount = 0;
        int idxType, idxComp;
        if (GetAccessorData(prim.indices, idxData, idxStride, idxCount, idxType,
                            idxComp)) {
          indices.resize(idxCount);
          for (size_t i = 0; i < idxCount; ++i) {
            if (idxComp == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
              indices[i] = *reinterpret_cast<const uint16_t *>(idxData + i * 2);
            } else if (idxComp == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
              indices[i] = *reinterpret_cast<const uint32_t *>(idxData + i * 4);
            } else if (idxComp == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
              indices[i] = idxData[i];
            }
          }
        }
      } else {
        indices.resize(vertexCount);
        for (uint32_t i = 0; i < (uint32_t)vertexCount; ++i)
          indices[i] = i;
      }

      if (vertices.empty() || indices.empty()) {
        fprintf(stderr,
                "Generated empty vertex or index list; skipping primitive\n");
        continue;
      }

      // Create GPU buffers
      GpuMesh gm;
      ComPtr<ID3D12Resource> vbUpload;
      ComPtr<ID3D12Resource> ibUpload;

      fprintf(stderr,
              "CreateDefaultBuffer: vb bytes=%zu ib bytes=%zu (mesh verts=%zu "
              "idx=%zu)\n",
              sizeof(Vertex) * vertices.size(),
              sizeof(uint32_t) * indices.size(), vertices.size(),
              indices.size());
      fflush(stderr);
      CreateDefaultBuffer(vertices.data(), sizeof(Vertex) * vertices.size(),
                          gm.vertexBuffer, vbUpload,
                          D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
      fprintf(stderr, "CreateDefaultBuffer: vb created\n");
      fflush(stderr);
      CreateDefaultBuffer(indices.data(), sizeof(uint32_t) * indices.size(),
                          gm.indexBuffer, ibUpload,
                          D3D12_RESOURCE_STATE_INDEX_BUFFER);
      fprintf(stderr, "CreateDefaultBuffer: ib created\n");
      fflush(stderr);

      gm.vbView.BufferLocation = gm.vertexBuffer->GetGPUVirtualAddress();
      gm.vbView.SizeInBytes =
          static_cast<UINT>(sizeof(Vertex) * vertices.size());
      gm.vbView.StrideInBytes = sizeof(Vertex);

      gm.ibView.BufferLocation = gm.indexBuffer->GetGPUVirtualAddress();
      gm.ibView.SizeInBytes =
          static_cast<UINT>(sizeof(uint32_t) * indices.size());
      gm.ibView.Format = DXGI_FORMAT_R32_UINT;

      gm.vertexCount = static_cast<UINT>(vertices.size());
      gm.indexCount = static_cast<UINT>(indices.size());
      // Use provided material index or reserve a default slot if none.
      if (prim.material >= 0) {
        gm.materialIndex = prim.material;
        gm.materialSlot = prim.material;
      } else if (wantMaterials) {
        // generate a blank default material and append to tmpMaterials
        Material def;
        def.diffuseColor[0] = def.diffuseColor[1] = def.diffuseColor[2] = 0.8f;
        def.diffuseColor[3] = 1.0f;
        tmpMaterials.push_back(def);
        gm.materialIndex = (int)tmpMaterials.size() - 1;
        gm.materialSlot = gm.materialIndex;
      } else {
        gm.materialIndex = -1;
        gm.materialSlot = -1;
      }

      // Keep CPU copies for raypicking
      gm.cpuVertices = vertices;
      gm.cpuIndices = indices;

      for (int c = 0; c < 3; ++c) {
        gm.minBound[c] = minBound[c];
        gm.maxBound[c] = maxBound[c];
      }

      outMeshes.push_back(std::move(gm));

      std::ostringstream log;
      log << "Imported node[" << ni << "] mesh[" << node.mesh << "] prim[" << pi
          << "] verts=" << vertexCount << " idx=" << indices.size() << "\n";
      fprintf(stderr, "%s", log.str().c_str());
    }
  }

  // If caller requested materials/textures, move temporaries out
  if (outTextures)
    *outTextures = std::move(tmpTextures);
  if (outMaterials)
    *outMaterials = std::move(tmpMaterials);

  return true;
}

#else

bool LoadGltf(const std::string &path, std::vector<GpuMesh> &outMeshes,
              std::vector<Material> *outMaterials,
              std::vector<Texture> *outTextures, const float *rootTranslation,
              std::vector<ImportedSceneNode> *outSceneNodes) {
  std::ostringstream oss;
  oss << "Asset::LoadGltf requested: " << path << "\n";
  fprintf(stderr, "%s", oss.str().c_str());

  const std::filesystem::path nativePath = NativePathFromUtf8(path);
  std::error_code ec;
  if (!std::filesystem::exists(nativePath, ec)) {
    fprintf(stderr, "File not found: glTF path does not exist\n");
    return false;
  }

  (void)outSceneNodes;
  fprintf(stderr, "Found file but loader is stubbed. Enable USE_TINYGLTF in "
                  "CMake to use tinygltf.\n");
  return true;
}

#endif

// Custom Progress Handler for Assimp to feed into our system
class AssetProgressHandler : public Assimp::ProgressHandler {
public:
  AssetProgressHandler(std::function<void(float, const std::string &)> cb,
                       const std::string &path)
      : m_cb(cb), m_path(path) {}

  virtual bool Update(float percentage) override {
    if (m_cb) {
      // Assimp reports 0.0 to 1.0 (or sometimes -1.0 if unknown)
      if (percentage >= 0.0f) {
        float p = percentage; // already 0..1
        // Clamp to 0..1 just in case
        if (p < 0.0f)
          p = 0.0f;
        if (p > 1.0f)
          p = 1.0f;
        // Re-map 0..1 to 0..0.8 range for "Loading" phase (leaving room for
        // processing)
        m_cb(p * 0.8f, "Importing " + m_path + "...");
      }
    }
    return true;
  }

private:
  std::function<void(float, const std::string &)> m_cb;
  std::string m_path;
};

bool LoadWithAssimp(const std::string &path, std::vector<GpuMesh> &outMeshes,
                    std::vector<Material> *outMaterials,
                    std::vector<Texture> *outTextures,
                    const float *rootTranslation,
                    std::vector<ImportedSceneNode> *outSceneNodes) {
  Assimp::Importer importer;
  const bool preserveSceneNodes = outSceneNodes != nullptr;
  // Hook up progress handler.
  // NOTE: Assimp Importer takes ownership of the progress handler and deletes
  // it in its destructor. Therefore, we must allocate it on the heap.
  if (s_progressCb) {
    AssetProgressHandler *progressHandler =
        new AssetProgressHandler(s_progressCb, path);
    importer.SetProgressHandler(progressHandler);
  }

  if (s_progressCb)
    s_progressCb(0.01f, std::string("Starting import: ") + path);
  // Base flags: fast loading, fewer optimizations
  unsigned int assimpFlags =
      aiProcess_Triangulate | aiProcess_CalcTangentSpace |
      aiProcess_GenSmoothNormals | aiProcess_SortByPType | aiProcess_FlipUVs |
      aiProcess_GlobalScale;

  if (g_fastImport) {
    assimpFlags |= aiProcess_JoinIdenticalVertices |
                   aiProcess_ImproveCacheLocality;
    if (!preserveSceneNodes) {
      // The legacy path bakes node transforms into vertex data, so graph
      // flattening is still useful there.
      assimpFlags |= aiProcess_PreTransformVertices | aiProcess_OptimizeMeshes |
                     aiProcess_OptimizeGraph;
    }
  }

  const aiScene *scene = importer.ReadFile(path, assimpFlags);

  if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE ||
      !scene->mRootNode) {
    fprintf(stderr, "Assimp Error: %s\n", importer.GetErrorString());
    if (s_progressCb)
      s_progressCb(0.0f,
                   std::string("Assimp Error: ") + importer.GetErrorString());
    return false;
  }

  if (outMaterials && scene->HasMaterials()) {
    for (unsigned int i = 0; i < scene->mNumMaterials; ++i) {
      aiMaterial *aiMat = scene->mMaterials[i];
      Material mat;
      aiString name;
      if (aiMat->Get(AI_MATKEY_NAME, name) == AI_SUCCESS)
        strncpy_s(mat.name, name.C_Str(), _TRUNCATE);

      aiColor4D color;
      if (aiMat->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS) {
        mat.diffuseColor[0] = color.r;
        mat.diffuseColor[1] = color.g;
        mat.diffuseColor[2] = color.b;
        mat.diffuseColor[3] = color.a;
      }
      if (aiMat->Get(AI_MATKEY_COLOR_EMISSIVE, color) == AI_SUCCESS) {
        mat.emissiveColor[0] = color.r;
        mat.emissiveColor[1] = color.g;
        mat.emissiveColor[2] = color.b;
      }

      float strength = 1.0f;
      if (aiMat->Get(AI_MATKEY_EMISSIVE_INTENSITY, strength) == AI_SUCCESS)
        mat.emissiveIntensity = strength;

      float roughness = 0.5f, metalness = 0.0f;
      aiMat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness);
      aiMat->Get(AI_MATKEY_METALLIC_FACTOR, metalness);
      mat.roughness = roughness;
      mat.metalness = metalness;
      mat.schemaVersion = Material::kSchemaVersionCoronaArchviz;
      const float emissiveMax =
          (std::max)(mat.emissiveColor[0],
                     (std::max)(mat.emissiveColor[1], mat.emissiveColor[2])) *
          (std::max)(mat.emissiveIntensity, 0.0f);
      if (emissiveMax > 1.0e-4f) {
        mat.materialClass = Material::kMaterialClassEmissive;
      } else if (mat.metalness > 0.5f) {
        mat.materialClass = Material::kMaterialClassMetal;
      } else {
        mat.materialClass = Material::kMaterialClassGeneric;
      }

      auto GetTexturePath = [&](aiTextureType type) -> std::string {
        aiString texPath;
        if (aiMat->GetTextureCount(type) > 0) {
          aiMat->GetTexture(type, 0, &texPath);
          std::filesystem::path p = path;
          return (p.parent_path() / texPath.C_Str()).string();
        }
        return "";
      };

      if (outTextures) {
        auto TryAddTexture = [&](const std::string &texPath, int &outIndex) {
          outIndex = -1;
          if (texPath.empty())
            return;

          // Handle embedded Assimp textures referenced as "*<index>" (e.g.
          // "*0"). Assimp stores embedded images in scene->mTextures; these can
          // be compressed (mHeight == 0, pcData points to PNG/JPEG bytes) or
          // uncompressed RGBA (mHeight > 0).
          if (!texPath.empty() && texPath[0] == '*' &&
              scene->mNumTextures > 0) {
            int idx = atoi(texPath.c_str() + 1);
            if (idx >= 0 && idx < (int)scene->mNumTextures) {
              aiTexture *aiTex = scene->mTextures[idx];
              Asset::Texture t;

              if (aiTex->mHeight == 0) {
                // compressed image in memory (mWidth == byte-size)
                int w = 0, h = 0, comp = 0;
                unsigned char *img =
                    stbi_load_from_memory((const unsigned char *)aiTex->pcData,
                                          (int)aiTex->mWidth, &w, &h, &comp, 4);
                if (img) {
                  t = LoadTextureFromMemory(img, w, h,
                                            DXGI_FORMAT_R8G8B8A8_UNORM);
                  stbi_image_free(img);
                }
              } else {
                // uncompressed RGBA data
                t = LoadTextureFromMemory(aiTex->pcData, aiTex->mWidth,
                                          aiTex->mHeight,
                                          DXGI_FORMAT_R8G8B8A8_UNORM);
              }

              if (!t.resource) {
                fprintf(stderr, "Assimp: embedded texture load failed: %s\n",
                        texPath.c_str());
                return;
              }

              outIndex = (int)outTextures->size();
              outTextures->push_back(std::move(t));
              return;
            }
          }

          // Fallback: file-backed texture path
          Asset::Texture t = LoadTextureFromFile(texPath);
          if (!t.resource) {
            // If the material referenced a filename but the file doesn't
            // exist on disk, Assimp may have embedded the image into
            // scene->mTextures. Try to match by filename basename.
            if (scene->mNumTextures > 0) {
              std::string requestedName =
                  std::filesystem::path(texPath).filename().string();
              auto iequals = [](const std::string &a, const std::string &b) {
                if (a.size() != b.size())
                  return false;
                for (size_t i = 0; i < a.size(); ++i)
                  if (tolower((unsigned char)a[i]) !=
                      tolower((unsigned char)b[i]))
                    return false;
                return true;
              };

              for (unsigned int ti = 0; ti < scene->mNumTextures; ++ti) {
                aiTexture *aiTex = scene->mTextures[ti];
                std::string aiName = aiTex->mFilename.C_Str();
                std::string aiBase =
                    std::filesystem::path(aiName).filename().string();
                if (!aiBase.empty() && iequals(aiBase, requestedName)) {
                  Asset::Texture embeddedTex;
                  if (aiTex->mHeight == 0) {
                    int w = 0, h = 0, comp = 0;
                    unsigned char *img = stbi_load_from_memory(
                        (const unsigned char *)aiTex->pcData,
                        (int)aiTex->mWidth, &w, &h, &comp, 4);
                    if (img) {
                      embeddedTex = LoadTextureFromMemory(
                          img, w, h, DXGI_FORMAT_R8G8B8A8_UNORM);
                      stbi_image_free(img);
                    }
                  } else {
                    embeddedTex = LoadTextureFromMemory(
                        aiTex->pcData, aiTex->mWidth, aiTex->mHeight,
                        DXGI_FORMAT_R8G8B8A8_UNORM);
                  }
                  if (embeddedTex.resource) {
                    outIndex = (int)outTextures->size();
                    outTextures->push_back(std::move(embeddedTex));
                    return;
                  }
                }
              }
            }

            fprintf(stderr, "Assimp: texture load failed: %s\n",
                    texPath.c_str());
            return;
          }
          outIndex = (int)outTextures->size();
          outTextures->push_back(std::move(t));
        };

        std::string dp = GetTexturePath(aiTextureType_DIFFUSE);
        TryAddTexture(dp, mat.diffuseTexture);

        std::string np = GetTexturePath(aiTextureType_NORMALS);
        if (np.empty())
          np = GetTexturePath(aiTextureType_HEIGHT); // Fallback
        TryAddTexture(np, mat.normalTexture);
        if (mat.normalTexture >= 0 && PathUsesDirectXNormalConvention(np)) {
          mat.normalMapFlipY = true;
        }

        std::string ep = GetTexturePath(aiTextureType_EMISSIVE);
        TryAddTexture(ep, mat.emissiveTexture);

        // Lower emissive value to 10% when an emissive texture is imported
        if (mat.emissiveTexture >= 0) {
          mat.emissiveIntensity *= 0.1f;
        }
      }
      outMaterials->push_back(mat);
    }
    if (s_progressCb)
      s_progressCb(0.12f, std::string("Materials parsed: ") +
                              std::to_string(scene->mNumMaterials));
  }

  // Progress accounting for mesh processing
  int totalMeshes = (int)scene->mNumMeshes;
  int processedMeshes = 0;

  if (s_progressCb)
    s_progressCb(0.15f, std::string("Processing meshes: 0/") +
                            std::to_string(totalMeshes));

  if (preserveSceneNodes) {
    outSceneNodes->clear();
    outMeshes.reserve(scene->mNumMeshes);
    for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes;
         ++meshIndex) {
      aiMesh *mesh = scene->mMeshes[meshIndex];
      std::vector<Vertex> vertices;
      std::vector<uint32_t> indices;
      vertices.reserve(mesh->mNumVertices);
      float minBound[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
      float maxBound[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};

      for (unsigned int vertexIndex = 0; vertexIndex < mesh->mNumVertices;
           ++vertexIndex) {
        Vertex vertex = {};
        const aiVector3D p = mesh->mVertices[vertexIndex];
        vertex.pos[0] = p.x;
        vertex.pos[1] = p.y;
        vertex.pos[2] = p.z;
        for (int axis = 0; axis < 3; ++axis) {
          minBound[axis] = (std::min)(minBound[axis], vertex.pos[axis]);
          maxBound[axis] = (std::max)(maxBound[axis], vertex.pos[axis]);
        }

        if (mesh->HasNormals()) {
          const aiVector3D n = mesh->mNormals[vertexIndex];
          vertex.normal[0] = n.x;
          vertex.normal[1] = n.y;
          vertex.normal[2] = n.z;
        } else {
          vertex.normal[1] = 1.0f;
        }

        if (mesh->HasTextureCoords(0)) {
          vertex.uv[0] = mesh->mTextureCoords[0][vertexIndex].x;
          vertex.uv[1] = mesh->mTextureCoords[0][vertexIndex].y;
        }

        if (mesh->HasTangentsAndBitangents()) {
          const aiVector3D tangent = mesh->mTangents[vertexIndex];
          const aiVector3D bitangent = mesh->mBitangents[vertexIndex];
          const aiVector3D normal = mesh->mNormals[vertexIndex];
          vertex.tangent[0] = tangent.x;
          vertex.tangent[1] = tangent.y;
          vertex.tangent[2] = tangent.z;
          vertex.tangent[3] =
              ComputeTangentHandedness(normal, tangent, bitangent);
        } else {
          vertex.tangent[0] = 1.0f;
          vertex.tangent[3] = 1.0f;
        }

        vertices.push_back(vertex);
      }

      for (unsigned int faceIndex = 0; faceIndex < mesh->mNumFaces;
           ++faceIndex) {
        const aiFace &face = mesh->mFaces[faceIndex];
        for (unsigned int index = 0; index < face.mNumIndices; ++index) {
          indices.push_back(face.mIndices[index]);
        }
      }

      GpuMesh gpuMesh = LoadMeshFromMemory(vertices, indices);
      gpuMesh.materialIndex = mesh->mMaterialIndex;
      gpuMesh.materialSlot = mesh->mMaterialIndex;
      for (int axis = 0; axis < 3; ++axis) {
        gpuMesh.minBound[axis] = minBound[axis];
        gpuMesh.maxBound[axis] = maxBound[axis];
      }
      outMeshes.push_back(std::move(gpuMesh));

      ++processedMeshes;
      if (s_progressCb && totalMeshes > 0) {
        float p = 0.15f + 0.6f * (processedMeshes / (float)totalMeshes);
        if (p > 0.82f)
          p = 0.82f;
        char buf[256];
        sprintf_s(buf, "Importing shared meshes: %d/%d", processedMeshes,
                  totalMeshes);
        s_progressCb(p, std::string(buf));
      }
    }

    aiMatrix4x4 rootTransform;
    if (rootTranslation) {
      rootTransform.a4 = rootTranslation[0];
      rootTransform.b4 = rootTranslation[1];
      rootTransform.c4 = rootTranslation[2];
    }

    std::function<void(aiNode *, aiMatrix4x4, size_t)> captureNode =
        [&](aiNode *node, aiMatrix4x4 parentTransform,
            size_t parentImportedIndex) {
          aiMatrix4x4 currentTransform = parentTransform * node->mTransformation;

          ImportedSceneNode sceneNode;
          sceneNode.name =
              node->mName.length > 0 ? node->mName.C_Str() : "Imported Node";
          sceneNode.parentIndex = parentImportedIndex;
          CopyAiMatrixToColumnMajor(currentTransform, sceneNode.transform);
          sceneNode.meshIndices.reserve(node->mNumMeshes);
          for (unsigned int meshRefIndex = 0; meshRefIndex < node->mNumMeshes;
               ++meshRefIndex) {
            sceneNode.meshIndices.push_back(node->mMeshes[meshRefIndex]);
          }

          const size_t thisImportedIndex = outSceneNodes->size();
          outSceneNodes->push_back(std::move(sceneNode));
          for (unsigned int childIndex = 0; childIndex < node->mNumChildren;
               ++childIndex) {
            captureNode(node->mChildren[childIndex], currentTransform,
                        thisImportedIndex);
          }
        };

    captureNode(scene->mRootNode, rootTransform, static_cast<size_t>(-1));
    if (s_progressCb)
      s_progressCb(1.0f, std::string("Import complete: ") + path);
    return !outMeshes.empty() && !outSceneNodes->empty();
  }

  std::function<void(aiNode *, aiMatrix4x4)> processNode =
      [&](aiNode *node, aiMatrix4x4 parentTransform) {
        aiMatrix4x4 currentTransform = parentTransform * node->mTransformation;

        float worldMat[16] = {
            currentTransform.a1, currentTransform.b1, currentTransform.c1,
            currentTransform.d1, currentTransform.a2, currentTransform.b2,
            currentTransform.c2, currentTransform.d2, currentTransform.a3,
            currentTransform.b3, currentTransform.c3, currentTransform.d3,
            currentTransform.a4, currentTransform.b4, currentTransform.c4,
            currentTransform.d4};

        for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
          aiMesh *mesh = scene->mMeshes[node->mMeshes[i]];
          std::vector<Vertex> vertices;
          std::vector<uint32_t> indices;
          float minB[3] = {FLT_MAX, FLT_MAX, FLT_MAX},
                maxB[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};

          for (unsigned int vIdx = 0; vIdx < mesh->mNumVertices; ++vIdx) {
            Vertex v = {};
            aiVector3D p = mesh->mVertices[vIdx];
            v.pos[0] = p.x * worldMat[0] + p.y * worldMat[4] +
                       p.z * worldMat[8] + worldMat[12];
            v.pos[1] = p.x * worldMat[1] + p.y * worldMat[5] +
                       p.z * worldMat[9] + worldMat[13];
            v.pos[2] = p.x * worldMat[2] + p.y * worldMat[6] +
                       p.z * worldMat[10] + worldMat[14];

            if (rootTranslation) {
              v.pos[0] += rootTranslation[0];
              v.pos[1] += rootTranslation[1];
              v.pos[2] += rootTranslation[2];
            }

            if (mesh->HasNormals()) {
              aiVector3D n = mesh->mNormals[vIdx];
              v.normal[0] =
                  n.x * worldMat[0] + n.y * worldMat[4] + n.z * worldMat[8];
              v.normal[1] =
                  n.x * worldMat[1] + n.y * worldMat[5] + n.z * worldMat[9];
              v.normal[2] =
                  n.x * worldMat[2] + n.y * worldMat[6] + n.z * worldMat[10];
            }
            if (mesh->HasTextureCoords(0)) {
              v.uv[0] = mesh->mTextureCoords[0][vIdx].x;
              v.uv[1] = mesh->mTextureCoords[0][vIdx].y;
            }
            if (mesh->HasTangentsAndBitangents()) {
              aiVector3D t = mesh->mTangents[vIdx];
              const aiVector3D b = mesh->mBitangents[vIdx];
              const aiVector3D n = mesh->mNormals[vIdx];
              v.tangent[0] =
                  t.x * worldMat[0] + t.y * worldMat[4] + t.z * worldMat[8];
              v.tangent[1] =
                  t.x * worldMat[1] + t.y * worldMat[5] + t.z * worldMat[9];
              v.tangent[2] =
                  t.x * worldMat[2] + t.y * worldMat[6] + t.z * worldMat[10];
              v.tangent[3] =
                  ComputeTangentHandedness(n, t, b) *
                  ComputeLinearTransformHandedness(worldMat);
            }
            for (int c = 0; c < 3; ++c) {
              minB[c] = std::min(minB[c], v.pos[c]);
              maxB[c] = std::max(maxB[c], v.pos[c]);
            }
            vertices.push_back(v);
          }

          for (unsigned int fIdx = 0; fIdx < mesh->mNumFaces; ++fIdx) {
            aiFace face = mesh->mFaces[fIdx];
            for (unsigned int idx = 0; idx < face.mNumIndices; ++idx)
              indices.push_back(face.mIndices[idx]);
          }

          GpuMesh gm;
          ComPtr<ID3D12Resource> vbUpload, ibUpload;
          if (g_debugLog) {
            fprintf(stderr,
                    "CreateDefaultBuffer (Assimp path): vb bytes=%zu ib "
                    "bytes=%zu (mesh verts=%zu idx=%zu)\n",
                    sizeof(Vertex) * vertices.size(),
                    sizeof(uint32_t) * indices.size(), vertices.size(),
                    indices.size());
            fflush(stderr);
          }
          CreateDefaultBuffer(vertices.data(), sizeof(Vertex) * vertices.size(),
                              gm.vertexBuffer, vbUpload,
                              D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
          if (g_debugLog) {
            fprintf(stderr, "CreateDefaultBuffer (Assimp path): vb created\n");
            fflush(stderr);
          }
          CreateDefaultBuffer(indices.data(), sizeof(uint32_t) * indices.size(),
                              gm.indexBuffer, ibUpload,
                              D3D12_RESOURCE_STATE_INDEX_BUFFER);
          if (g_debugLog) {
            fprintf(stderr, "CreateDefaultBuffer (Assimp path): ib created\n");
            fflush(stderr);
          }

          gm.vbView.BufferLocation = gm.vertexBuffer->GetGPUVirtualAddress();
          gm.vbView.SizeInBytes = (UINT)(sizeof(Vertex) * vertices.size());
          gm.vbView.StrideInBytes = sizeof(Vertex);
          gm.ibView.BufferLocation = gm.indexBuffer->GetGPUVirtualAddress();
          gm.ibView.SizeInBytes = (UINT)(sizeof(uint32_t) * indices.size());
          gm.ibView.Format = DXGI_FORMAT_R32_UINT;
          gm.vertexCount = (UINT)vertices.size();
          gm.indexCount = (UINT)indices.size();
          // Keep CPU copies for raypicking
          gm.cpuVertices = vertices;
          gm.cpuIndices = indices;
          for (int c = 0; c < 3; ++c) {
            gm.minBound[c] = minB[c];
            gm.maxBound[c] = maxB[c];
          }
          gm.materialIndex = mesh->mMaterialIndex;
          gm.materialSlot = mesh->mMaterialIndex;

          outMeshes.push_back(std::move(gm));
          // Update progress after each mesh
          processedMeshes++;
          if (s_progressCb && totalMeshes > 0) {
            float p = 0.15f + 0.8f * (processedMeshes / (float)totalMeshes);
            if (p > 0.95f)
              p = 0.95f;
            char buf[256];
            sprintf_s(buf, "Importing meshes: %d/%d", processedMeshes,
                      totalMeshes);
            s_progressCb(p, std::string(buf));
          }
        }

        for (unsigned int i = 0; i < node->mNumChildren; ++i)
          processNode(node->mChildren[i], currentTransform);
      };

  processNode(scene->mRootNode, aiMatrix4x4());
  if (s_progressCb)
    s_progressCb(1.0f, std::string("Import complete: ") + path);
  return true;
}

bool LoadOBJ(const std::string &path, std::vector<GpuMesh> &outMeshes,
             std::vector<Material> *outMaterials,
             std::vector<Texture> *outTextures, const float *rootTranslation,
             std::vector<ImportedSceneNode> *outSceneNodes) {
  return LoadWithAssimp(path, outMeshes, outMaterials, outTextures,
                        rootTranslation, outSceneNodes);
}

bool LoadSTL(const std::string &path, std::vector<GpuMesh> &outMeshes,
             std::vector<Material> *outMaterials,
             std::vector<Texture> *outTextures, const float *rootTranslation,
             std::vector<ImportedSceneNode> *outSceneNodes) {
  return LoadWithAssimp(path, outMeshes, outMaterials, outTextures,
                        rootTranslation, outSceneNodes);
}

// ---------------------------------------------------------------------------
// LoadLTM — proprietary .ltm/.lmod chunked model format
// ---------------------------------------------------------------------------

#pragma pack(push, 1)
struct LtmDdsPixelFormat {
  uint32_t size, flags, fourCC, rgbBitCount;
  uint32_t rBitMask, gBitMask, bBitMask, aBitMask;
};
struct LtmDdsHeader {
  uint32_t magic, size, flags, height, width;
  uint32_t pitchOrLinearSize, depth, mipMapCount;
  uint32_t reserved1[11];
  LtmDdsPixelFormat ddspf;
  uint32_t caps, caps2, caps3, caps4, reserved2;
};
struct LtmDdsHeaderDxt10 {
  uint32_t dxgiFormat, resourceDimension, miscFlag, arraySize, miscFlags2;
};
#pragma pack(pop)

static DXGI_FORMAT LtmFourCCToFormat(uint32_t fcc) {
  switch (fcc) {
  case 0x31545844u: return DXGI_FORMAT_BC1_UNORM; // DXT1
  case 0x33545844u: return DXGI_FORMAT_BC2_UNORM; // DXT3
  case 0x35545844u: return DXGI_FORMAT_BC3_UNORM; // DXT5
  case 0x31495441u: return DXGI_FORMAT_BC4_UNORM; // ATI1
  case 0x32495441u: return DXGI_FORMAT_BC5_UNORM; // ATI2
  case 0x55344342u: return DXGI_FORMAT_BC4_UNORM; // BC4U
  case 0x53344342u: return DXGI_FORMAT_BC4_SNORM; // BC4S
  case 0x55354342u: return DXGI_FORMAT_BC5_UNORM; // BC5U
  case 0x53354342u: return DXGI_FORMAT_BC5_SNORM; // BC5S
  case 0x30315844u: return DXGI_FORMAT_UNKNOWN;   // DX10 — need ext header
  default:          return DXGI_FORMAT_UNKNOWN;
  }
}

static int LtmBCBlockBytes(DXGI_FORMAT fmt) {
  switch (fmt) {
  case DXGI_FORMAT_BC1_UNORM: case DXGI_FORMAT_BC1_UNORM_SRGB:
  case DXGI_FORMAT_BC4_UNORM: case DXGI_FORMAT_BC4_SNORM: return 8;
  default: return 16;
  }
}

static bool LtmIsBlockCompressedFormat(DXGI_FORMAT fmt) {
  switch (fmt) {
  case DXGI_FORMAT_BC1_UNORM:
  case DXGI_FORMAT_BC1_UNORM_SRGB:
  case DXGI_FORMAT_BC2_UNORM:
  case DXGI_FORMAT_BC2_UNORM_SRGB:
  case DXGI_FORMAT_BC3_UNORM:
  case DXGI_FORMAT_BC3_UNORM_SRGB:
  case DXGI_FORMAT_BC4_UNORM:
  case DXGI_FORMAT_BC4_SNORM:
  case DXGI_FORMAT_BC5_UNORM:
  case DXGI_FORMAT_BC5_SNORM:
  case DXGI_FORMAT_BC6H_UF16:
  case DXGI_FORMAT_BC6H_SF16:
  case DXGI_FORMAT_BC7_UNORM:
  case DXGI_FORMAT_BC7_UNORM_SRGB:
    return true;
  default:
    return false;
  }
}

static int LtmBytesPerPixel(DXGI_FORMAT fmt) {
  switch (fmt) {
  case DXGI_FORMAT_R8G8B8A8_UNORM:
  case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
  case DXGI_FORMAT_B8G8R8A8_UNORM:
  case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
  case DXGI_FORMAT_B8G8R8X8_UNORM:
  case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
    return 4;
  default:
    return 0;
  }
}

Texture LoadTextureFromFile(const std::string &path, bool isHDR,
                            TextureUsageSemantic semantic) {
  Texture tex = LoadTextureFromFile(path, isHDR);
  if (tex.resource) {
    ApplyTextureCompressionForUsage(
        tex, isHDR ? TextureUsageSemantic::Hdr : semantic);
  }
  return tex;
}

static bool LtmReadDDSFormat(const uint8_t *ddsData, size_t ddsSize,
                             DXGI_FORMAT *outFormat) {
  if (!ddsData || ddsSize < 128 || !outFormat)
    return false;
  const auto *hdr = reinterpret_cast<const LtmDdsHeader *>(ddsData);
  if (hdr->magic != 0x20534444u)
    return false;

  DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
  if (hdr->ddspf.fourCC == 0x30315844u) {
    if (ddsSize < 148)
      return false;
    const auto *dx10 =
        reinterpret_cast<const LtmDdsHeaderDxt10 *>(ddsData + 128);
    fmt = static_cast<DXGI_FORMAT>(dx10->dxgiFormat);
  } else {
    fmt = LtmFourCCToFormat(hdr->ddspf.fourCC);
    if (fmt == DXGI_FORMAT_UNKNOWN && (hdr->ddspf.flags & 0x40u) &&
        hdr->ddspf.rgbBitCount == 32) {
      fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
    }
  }
  if (fmt == DXGI_FORMAT_UNKNOWN)
    return false;
  *outFormat = fmt;
  return true;
}

static DXGI_FORMAT LtmRemoveSrgbViewFormat(DXGI_FORMAT fmt) {
  switch (fmt) {
  case DXGI_FORMAT_BC1_UNORM_SRGB: return DXGI_FORMAT_BC1_UNORM;
  case DXGI_FORMAT_BC2_UNORM_SRGB: return DXGI_FORMAT_BC2_UNORM;
  case DXGI_FORMAT_BC3_UNORM_SRGB: return DXGI_FORMAT_BC3_UNORM;
  case DXGI_FORMAT_BC7_UNORM_SRGB: return DXGI_FORMAT_BC7_UNORM;
  case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM;
  case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM;
  case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8X8_UNORM;
  default: return fmt;
  }
}

static Texture LtmUploadDDS(const uint8_t *ddsData, size_t ddsSize,
                            DXGI_FORMAT *outAuthoredFormat = nullptr) {
  if (!s_device || ddsSize < 128) return {};
  const auto *hdr = reinterpret_cast<const LtmDdsHeader *>(ddsData);
  if (hdr->magic != 0x20534444u) {
    fprintf(stderr, "LTM: DDS upload rejected (bad magic, size=%zu)\n", ddsSize);
    return {};
  }

  DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
  uint32_t mips   = std::max(1u, hdr->mipMapCount);
  uint32_t W      = hdr->width;
  uint32_t H      = hdr->height;
  const uint8_t *mipPtr = ddsData + 128;

  if (!LtmReadDDSFormat(ddsData, ddsSize, &fmt)) {
    fprintf(stderr,
            "LTM: DDS upload rejected (unsupported FourCC=0x%08X flags=0x%08X rgbBits=%u size=%zu)\n",
            hdr->ddspf.fourCC, hdr->ddspf.flags, hdr->ddspf.rgbBitCount,
            ddsSize);
    return {};
  }
  if (hdr->ddspf.fourCC == 0x30315844u) { // DX10
    if (ddsSize < 148) {
      fprintf(stderr, "LTM: DDS upload rejected (DX10 header missing, size=%zu)\n", ddsSize);
      return {};
    }
    mipPtr = ddsData + 148;
  }

  if (outAuthoredFormat) {
    *outAuthoredFormat = fmt;
  }
  const DXGI_FORMAT resourceFmt = LtmRemoveSrgbViewFormat(fmt);
  const bool blockCompressed = LtmIsBlockCompressedFormat(resourceFmt);
  const int blockBytes = blockCompressed ? LtmBCBlockBytes(resourceFmt) : 0;
  const int bytesPerPixel = blockCompressed ? 0 : LtmBytesPerPixel(resourceFmt);
  if (!blockCompressed && bytesPerPixel == 0) {
    fprintf(stderr,
            "LTM: DDS upload rejected (unsupported uncompressed fmt=%u authoredFmt=%u size=%zu)\n",
            (unsigned)resourceFmt, (unsigned)fmt, ddsSize);
    return {};
  }
  size_t tightMipBytes = 0;
  uint32_t availableMips = 0;
  for (uint32_t mip = 0; mip < mips; ++mip) {
    const uint32_t mipW = std::max(1u, W >> mip);
    const uint32_t mipH = std::max(1u, H >> mip);
    const size_t mipBytes = TightlyPackedMipSize(resourceFmt, mipW, mipH);
    if (mipBytes == 0 || mipPtr + tightMipBytes + mipBytes > ddsData + ddsSize) {
      break;
    }
    tightMipBytes += mipBytes;
    ++availableMips;
  }
  if (availableMips == 0) {
    return {};
  }
  mips = availableMips;

  D3D12_RESOURCE_DESC texDesc = {};
  texDesc.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  texDesc.Width            = W;
  texDesc.Height           = H;
  texDesc.DepthOrArraySize = 1;
  texDesc.MipLevels        = (UINT16)mips;
  texDesc.Format           = resourceFmt;
  texDesc.SampleDesc.Count = 1;
  texDesc.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;

  D3D12_HEAP_PROPERTIES defHeap = {};
  defHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
  ComPtr<ID3D12Resource> texRes;
  if (FAILED(s_device->CreateCommittedResource(
        &defHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texRes))))
    return {};

  std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(mips);
  std::vector<UINT>   numRows(mips);
  std::vector<UINT64> rowBytes(mips);
  UINT64 totalBytes = 0;
  s_device->GetCopyableFootprints(&texDesc, 0, mips, 0,
    footprints.data(), numRows.data(), rowBytes.data(), &totalBytes);

  D3D12_HEAP_PROPERTIES upHeap = {};
  upHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
  D3D12_RESOURCE_DESC bufDesc = {};
  bufDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
  bufDesc.Width            = totalBytes;
  bufDesc.Height = bufDesc.DepthOrArraySize = bufDesc.MipLevels = 1;
  bufDesc.Format           = DXGI_FORMAT_UNKNOWN;
  bufDesc.SampleDesc.Count = 1;
  bufDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ComPtr<ID3D12Resource> uploadBuf;
  if (FAILED(s_device->CreateCommittedResource(
        &upHeap, D3D12_HEAP_FLAG_NONE, &bufDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuf))))
    return {};

  uint8_t *mapped = nullptr;
  uploadBuf->Map(0, nullptr, reinterpret_cast<void **>(&mapped));
  const uint8_t *src = mipPtr;
  for (uint32_t mip = 0; mip < mips; ++mip) {
    uint32_t mW = std::max(1u, W >> mip);
    uint32_t mH = std::max(1u, H >> mip);
    uint32_t srcRows = mH;
    uint32_t srcRowPitch = mW * (uint32_t)bytesPerPixel;
    if (blockCompressed) {
      const uint32_t blkW = (mW + 3) / 4;
      const uint32_t blkH = (mH + 3) / 4;
      srcRows = blkH;
      srcRowPitch = blkW * (uint32_t)blockBytes;
    }
    uint8_t *dst = mapped + footprints[mip].Offset;
    for (uint32_t row = 0; row < srcRows; ++row) {
      if (src + srcRowPitch > ddsData + ddsSize) goto ltm_dds_done;
      memcpy(dst + (size_t)row * footprints[mip].Footprint.RowPitch,
             src + (size_t)row * srcRowPitch, srcRowPitch);
    }
    src += (size_t)srcRows * srcRowPitch;
  }
ltm_dds_done:
  uploadBuf->Unmap(0, nullptr);

  ComPtr<ID3D12CommandAllocator>    ca;
  ComPtr<ID3D12GraphicsCommandList> cl;
  ThrowIfFailed(s_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&ca)));
  ThrowIfFailed(s_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, ca.Get(), nullptr, IID_PPV_ARGS(&cl)));

  for (uint32_t mip = 0; mip < mips; ++mip) {
    D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
    srcLoc.pResource        = uploadBuf.Get();
    srcLoc.Type             = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLoc.PlacedFootprint  = footprints[mip];
    D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
    dstLoc.pResource        = texRes.Get();
    dstLoc.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLoc.SubresourceIndex = mip;
    cl->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
  }

  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource   = texRes.Get();
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  cl->ResourceBarrier(1, &barrier);
  cl->Close();

  ID3D12CommandList *cls[] = {cl.Get()};
  s_queue->ExecuteCommandLists(1, cls);
  WaitForQueueIdle(s_queue.Get());

  Texture tex;
  tex.resource  = texRes;
  tex.width     = W;
  tex.height    = H;
  tex.mipLevels = mips;
  tex.format    = resourceFmt;
  tex.cpuFormat = resourceFmt;
  tex.cpuMipLevels = mips;
  tex.gpuCompressed = IsBlockCompressedTextureFormat(resourceFmt);
  tex.compressionMode = TextureCompressionMode::Off;
  tex.cpuData.assign(mipPtr, mipPtr + tightMipBytes);
  return tex;
}

struct LtmGeoBlock {
  uint32_t       vertexCount = 0;
  uint32_t       indexCount  = 0;
  const uint8_t *vppi = nullptr;
  const uint8_t *vnni = nullptr;
  const uint8_t *vtd0 = nullptr;
  const uint8_t *vttb = nullptr;
  const uint8_t *po32 = nullptr;
  const uint8_t *poda = nullptr;
  std::string    materialName;
  uint16_t       matSlot = 0;
};

enum class LtmTextureSemantic {
  Unknown,
  Diffuse,
  Normal,
  Opacity,
  Emissive,
  Occlusion,
  MetalRough,
  Metalness,
  Roughness,
};

struct LtmTextureEntry {
  uint32_t mapType = 0xffffffffu;
  uint32_t texW = 0;
  uint32_t texH = 0;
  uint32_t texSize = 0;
  uint32_t txft = 0;
  uint32_t tecm = 0;
  uint32_t tent = 0;
  int materialSlot = -1;
  const uint8_t *ddsPtr = nullptr;
  size_t ddsSize = 0;
  int uploadedTextureIndex = -1;
  DXGI_FORMAT uploadedFormat = DXGI_FORMAT_UNKNOWN;
  DXGI_FORMAT authoredFormat = DXGI_FORMAT_UNKNOWN;
  std::string materialName;
};

static int LtmFindNearestMaterialSlot(const uint8_t *base,
                                      const uint8_t *fileEnd,
                                      const uint8_t *beforePos,
                                      size_t backScanBytes = 512 * 1024) {
  if (!base || !fileEnd || !beforePos || beforePos <= base + 8)
    return -1;

  const uint8_t *start = beforePos - (std::min)(backScanBytes, (size_t)(beforePos - base));
  for (const uint8_t *p = beforePos - 8; p >= start; --p) {
    if (memcmp(p, "MSLC", 4) != 0)
      continue;
    if (p + 12 > fileEnd)
      continue;
    const uint32_t sz = *reinterpret_cast<const uint32_t *>(p + 4);
    if (sz < 4 || p + 8 + sz > fileEnd)
      continue;
    const uint32_t slot = *reinterpret_cast<const uint32_t *>(p + 8);
    if (slot <= 0xffffu)
      return static_cast<int>(slot);
  }
  return -1;
}

static const char *LtmTextureSemanticName(LtmTextureSemantic semantic) {
  switch (semantic) {
  case LtmTextureSemantic::Diffuse: return "diffuse";
  case LtmTextureSemantic::Normal: return "normal";
  case LtmTextureSemantic::Opacity: return "opacity";
  case LtmTextureSemantic::Emissive: return "emissive";
  case LtmTextureSemantic::Occlusion: return "occlusion";
  case LtmTextureSemantic::MetalRough: return "metal_rough";
  case LtmTextureSemantic::Metalness: return "metalness";
  case LtmTextureSemantic::Roughness: return "roughness";
  default: return "unknown";
  }
}

static bool LtmIsSrgbFormat(DXGI_FORMAT fmt) {
  switch (fmt) {
  case DXGI_FORMAT_BC1_UNORM_SRGB:
  case DXGI_FORMAT_BC2_UNORM_SRGB:
  case DXGI_FORMAT_BC3_UNORM_SRGB:
  case DXGI_FORMAT_BC7_UNORM_SRGB:
  case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    return true;
  default:
    return false;
  }
}

static LtmTextureSemantic LtmInferSemanticFromEntry(const LtmTextureEntry &entry,
                                                    const Texture *uploadedTex) {
  // mapType values are still not fully decoded. Keep non-zero values mapped
  // where useful, but do not force mapType=0 to diffuse (too broad).
  switch (entry.mapType) {
  case 1: return LtmTextureSemantic::Normal;
  case 2: return LtmTextureSemantic::Opacity;
  case 3: return LtmTextureSemantic::MetalRough;
  case 4: return LtmTextureSemantic::Emissive;
  case 5: return LtmTextureSemantic::Occlusion;
  default: break;
  }

  if (!uploadedTex)
    return LtmTextureSemantic::Unknown;

  // Strong format hints.
  if (LtmIsSrgbFormat(entry.authoredFormat)) {
    return LtmTextureSemantic::Diffuse;
  }
  if (uploadedTex->format == DXGI_FORMAT_BC5_UNORM ||
      uploadedTex->format == DXGI_FORMAT_BC5_SNORM) {
    return LtmTextureSemantic::Normal;
  }
  if (uploadedTex->format == DXGI_FORMAT_BC4_UNORM ||
      uploadedTex->format == DXGI_FORMAT_BC4_SNORM) {
    return LtmTextureSemantic::Roughness;
  }
  if (LtmIsSrgbFormat(uploadedTex->format)) {
    return LtmTextureSemantic::Diffuse;
  }

  return LtmTextureSemantic::Unknown;
}

static bool LtmIsLmodColorCandidateFormat(DXGI_FORMAT fmt) {
  switch (fmt) {
  case DXGI_FORMAT_BC1_UNORM:
  case DXGI_FORMAT_BC1_UNORM_SRGB:
  case DXGI_FORMAT_BC2_UNORM:
  case DXGI_FORMAT_BC2_UNORM_SRGB:
  case DXGI_FORMAT_BC3_UNORM:
  case DXGI_FORMAT_BC3_UNORM_SRGB:
  case DXGI_FORMAT_BC6H_UF16:
  case DXGI_FORMAT_BC6H_SF16:
  case DXGI_FORMAT_BC7_UNORM:
  case DXGI_FORMAT_BC7_UNORM_SRGB:
  case DXGI_FORMAT_R8G8B8A8_UNORM:
  case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
  case DXGI_FORMAT_B8G8R8A8_UNORM:
  case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
  case DXGI_FORMAT_B8G8R8X8_UNORM:
  case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
    return true;
  default:
    return false;
  }
}

static std::string LtmReadUtf16AsciiString(const uint8_t *payload,
                                           uint32_t size) {
  std::string out;
  if (!payload)
    return out;
  const uint32_t chars = size / 2;
  out.reserve(chars);
  for (uint32_t i = 0; i < chars; ++i) {
    const uint8_t lo = payload[i * 2];
    const uint8_t hi = payload[i * 2 + 1];
    if (lo == 0 && hi == 0)
      break;
    if (hi != 0 || lo < 0x20 || lo > 0x7e)
      break;
    out.push_back(static_cast<char>(lo));
  }
  while (!out.empty() && out.back() == ' ')
    out.pop_back();
  return out;
}

static std::string LtmFindNearestUtf16ChunkString(const uint8_t *base,
                                                  const uint8_t *fileEnd,
                                                  const uint8_t *beforePos,
                                                  const char tag[4],
                                                  size_t backScanBytes =
                                                      256 * 1024) {
  if (!base || !fileEnd || !beforePos || beforePos <= base + 8)
    return {};

  const uint8_t *start =
      beforePos - (std::min)(backScanBytes, (size_t)(beforePos - base));
  const uint8_t *p = beforePos - 8;
  while (p >= start) {
    if (memcmp(p, tag, 4) == 0 && p + 8 <= fileEnd) {
      const uint32_t sz = *reinterpret_cast<const uint32_t *>(p + 4);
      if (sz > 0 && sz <= 512 && p + 8 + sz <= fileEnd) {
        std::string text = LtmReadUtf16AsciiString(p + 8, sz);
        if (!text.empty())
          return text;
      }
    }
    if (p == start)
      break;
    --p;
  }
  return {};
}

static bool LtmLooksLikeLmodMaterialName(const std::string &text) {
  if (text.empty() || text.size() > 128)
    return false;

  const char first = text.front();
  if (first == '{' || first == '[')
    return false;

  std::string lower = text;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) {
                   return static_cast<char>(std::tolower(c));
                 });
  if (lower == "text" || lower == "texture" || lower == "buffer" ||
      lower == "3d objectdata" || lower == "oo classinstance" ||
      lower == "oo class instances list") {
    return false;
  }

  bool hasNameChar = false;
  for (char c : text) {
    const unsigned char uc = static_cast<unsigned char>(c);
    if (std::iscntrl(uc))
      return false;
    if (std::isalnum(uc))
      hasNameChar = true;
  }
  return hasNameChar;
}

static std::string LtmFindNearestLmodMaterialName(const uint8_t *base,
                                                  const uint8_t *fileEnd,
                                                  const uint8_t *beforePos,
                                                  size_t backScanBytes =
                                                      512 * 1024) {
  if (!base || !fileEnd || !beforePos || beforePos <= base + 8)
    return {};

  const uint8_t *start =
      beforePos - (std::min)(backScanBytes, (size_t)(beforePos - base));
  const uint8_t *p = beforePos - 8;
  while (p >= start) {
    if (memcmp(p, "STWA", 4) == 0 && p + 8 <= fileEnd) {
      const uint32_t sz = *reinterpret_cast<const uint32_t *>(p + 4);
      if (sz > 0 && sz <= 512 && p + 8 + sz <= fileEnd) {
        std::string text = LtmReadUtf16AsciiString(p + 8, sz);
        if (LtmLooksLikeLmodMaterialName(text))
          return text;
      }
    }
    if (p == start)
      break;
    --p;
  }
  return {};
}

static void LtmApplyLmodTextureOrderHeuristics(
    const std::vector<LtmTextureEntry> &textureEntries,
    std::vector<LtmTextureSemantic> &semanticByEntry) {
  struct MaterialTextureGroup {
    std::string materialName;
    std::vector<size_t> colorEntries;
  };
  std::vector<MaterialTextureGroup> materialGroups;
  std::vector<size_t> unlinkedColorEntries;

  for (size_t ti = 0; ti < textureEntries.size(); ++ti) {
    const LtmTextureEntry &entry = textureEntries[ti];
    if (entry.uploadedTextureIndex < 0)
      continue;

    if (entry.uploadedFormat == DXGI_FORMAT_BC4_UNORM ||
        entry.uploadedFormat == DXGI_FORMAT_BC4_SNORM) {
      semanticByEntry[ti] = LtmTextureSemantic::Roughness;
      continue;
    }

    if (entry.uploadedFormat == DXGI_FORMAT_BC5_UNORM ||
        entry.uploadedFormat == DXGI_FORMAT_BC5_SNORM) {
      if (semanticByEntry[ti] == LtmTextureSemantic::Unknown)
        semanticByEntry[ti] = LtmTextureSemantic::Normal;
      continue;
    }

    const bool colorCandidate =
        LtmIsLmodColorCandidateFormat(entry.authoredFormat) ||
        LtmIsLmodColorCandidateFormat(entry.uploadedFormat);
    if (!colorCandidate)
      continue;

    if (entry.materialName.empty()) {
      unlinkedColorEntries.push_back(ti);
      continue;
    }

    auto groupIt = std::find_if(
        materialGroups.begin(), materialGroups.end(),
        [&](const MaterialTextureGroup &group) {
          return group.materialName == entry.materialName;
        });
    if (groupIt == materialGroups.end()) {
      MaterialTextureGroup group;
      group.materialName = entry.materialName;
      group.colorEntries.push_back(ti);
      materialGroups.push_back(std::move(group));
    } else {
      groupIt->colorEntries.push_back(ti);
    }
  }

  // LMOD DDS headers are commonly authored as linear BC7 even for albedo. The
  // reliable cue in these files is the material run: the first color texture
  // following a materialname belongs in that material's base-color slot.
  bool assignedAnyDiffuse = false;
  for (const MaterialTextureGroup &group : materialGroups) {
    if (group.colorEntries.empty())
      continue;
    const size_t firstColorEntry = group.colorEntries[0];
    if (semanticByEntry[firstColorEntry] == LtmTextureSemantic::Unknown ||
        semanticByEntry[firstColorEntry] == LtmTextureSemantic::Diffuse) {
      semanticByEntry[firstColorEntry] = LtmTextureSemantic::Diffuse;
      assignedAnyDiffuse = true;
    }
  }

  if (!assignedAnyDiffuse && !unlinkedColorEntries.empty()) {
    const size_t firstColorEntry = unlinkedColorEntries[0];
    if (semanticByEntry[firstColorEntry] == LtmTextureSemantic::Unknown ||
        semanticByEntry[firstColorEntry] == LtmTextureSemantic::Diffuse) {
      semanticByEntry[firstColorEntry] = LtmTextureSemantic::Diffuse;
    }
  }
}

static bool LtmPayloadStartsWithUtf16Name(const uint8_t *payload, uint32_t size,
                                          const char *name) {
  if (!payload || !name)
    return false;

  const size_t len = strlen(name);
  if (size < len * 2)
    return false;

  for (size_t i = 0; i < len; ++i) {
    if (payload[i * 2] != static_cast<uint8_t>(name[i]) ||
        payload[i * 2 + 1] != 0) {
      return false;
    }
  }

  if (size >= (len + 1) * 2) {
    const uint16_t next =
        static_cast<uint16_t>(payload[len * 2]) |
        (static_cast<uint16_t>(payload[len * 2 + 1]) << 8);
    return next == 0 || next == 0x20;
  }
  return true;
}

static LtmTextureSemantic LtmSemanticFromUtf16NamePayload(const uint8_t *payload,
                                                          uint32_t size) {
  if (LtmPayloadStartsWithUtf16Name(payload, size, "Diffuse"))
    return LtmTextureSemantic::Diffuse;
  if (LtmPayloadStartsWithUtf16Name(payload, size, "Normal"))
    return LtmTextureSemantic::Normal;
  if (LtmPayloadStartsWithUtf16Name(payload, size, "Opacity") ||
      LtmPayloadStartsWithUtf16Name(payload, size, "Alpha"))
    return LtmTextureSemantic::Opacity;
  if (LtmPayloadStartsWithUtf16Name(payload, size, "Emissive") ||
      LtmPayloadStartsWithUtf16Name(payload, size, "Emission"))
    return LtmTextureSemantic::Emissive;
  if (LtmPayloadStartsWithUtf16Name(payload, size, "Occlusion"))
    return LtmTextureSemantic::Occlusion;
  if (LtmPayloadStartsWithUtf16Name(payload, size, "Roughness"))
    return LtmTextureSemantic::Roughness;
  if (LtmPayloadStartsWithUtf16Name(payload, size, "Metalness"))
    return LtmTextureSemantic::Metalness;
  return LtmTextureSemantic::Unknown;
}

static std::vector<LtmTextureSemantic>
LtmReadLmodTextureSemanticHints(const uint8_t *base, const uint8_t *fileEnd) {
  std::vector<LtmTextureSemantic> hints;
  if (!base || !fileEnd || fileEnd <= base)
    return hints;

  const uint8_t *firstTexm = nullptr;
  for (const uint8_t *p = base; p + 8 <= fileEnd; ++p) {
    if (memcmp(p, "TEXM", 4) != 0)
      continue;
    const uint32_t sz = *reinterpret_cast<const uint32_t *>(p + 4);
    if (sz >= 4 && p + 8 + sz <= fileEnd) {
      firstTexm = p;
      break;
    }
  }
  if (!firstTexm)
    return hints;

  // LMOD stores texture channel names (Diffuse, Emissive, etc.) as IINW
  // entries before the embedded Texture chunks. The DDS headers are often
  // authored as linear BC7, so these names are stronger than format hints.
  bool sawTextureList = false;
  const size_t bytesBeforeTexm = static_cast<size_t>(firstTexm - base);
  const uint8_t *scanEnd = base + (std::min)(bytesBeforeTexm, (size_t)256 * 1024);
  for (const uint8_t *p = base; p + 8 <= scanEnd; ++p) {
    if (memcmp(p, "IINW", 4) != 0)
      continue;
    const uint32_t sz = *reinterpret_cast<const uint32_t *>(p + 4);
    if (sz == 0 || sz > 512 || p + 8 + sz > fileEnd)
      continue;
    const uint8_t *payload = p + 8;
    if (LtmPayloadStartsWithUtf16Name(payload, sz, "TextureList")) {
      sawTextureList = true;
      continue;
    }
    if (!sawTextureList)
      continue;
    if (LtmPayloadStartsWithUtf16Name(payload, sz, "ObjectData") ||
        LtmPayloadStartsWithUtf16Name(payload, sz, "surfaceNr") ||
        LtmPayloadStartsWithUtf16Name(payload, sz, "MaterialInfo")) {
      break;
    }

    const LtmTextureSemantic semantic =
        LtmSemanticFromUtf16NamePayload(payload, sz);
    if (semantic != LtmTextureSemantic::Unknown)
      hints.push_back(semantic);
  }
  return hints;
}

static std::vector<LtmTextureEntry>
LtmScanEmbeddedTextureEntries(const uint8_t *base, const uint8_t *fileEnd,
                              bool captureLmodMaterialNames) {
  std::vector<LtmTextureEntry> entries;
  if (!base || !fileEnd || fileEnd <= base)
    return entries;

  std::string currentLmodTextureMaterialName;
  for (const uint8_t *scan = base; scan + 12 <= fileEnd; ++scan) {
    if (memcmp(scan, "TEXM", 4) != 0)
      continue;

    const uint32_t texmSize = *reinterpret_cast<const uint32_t *>(scan + 4);
    if (texmSize < 4 || scan + 8 + texmSize > fileEnd)
      continue;

    LtmTextureEntry entry;
    entry.mapType = *reinterpret_cast<const uint32_t *>(scan + 8);
    entry.materialSlot = LtmFindNearestMaterialSlot(base, fileEnd, scan);
    if (captureLmodMaterialNames) {
      entry.materialName = LtmFindNearestLmodMaterialName(base, fileEnd, scan);
      if (!entry.materialName.empty())
        currentLmodTextureMaterialName = entry.materialName;
      else
        entry.materialName = currentLmodTextureMaterialName;
    }

    const uint8_t *p = scan + 8 + texmSize;
    for (int guard = 0; guard < 32 && p + 8 <= fileEnd; ++guard) {
      const uint8_t *tag = p;
      const uint32_t sz = *reinterpret_cast<const uint32_t *>(p + 4);
      if (p + 8 + sz > fileEnd)
        break;

      if (memcmp(tag, "TEXW", 4) == 0 && sz >= 4)
        entry.texW = *reinterpret_cast<const uint32_t *>(p + 8);
      else if (memcmp(tag, "TEXH", 4) == 0 && sz >= 4)
        entry.texH = *reinterpret_cast<const uint32_t *>(p + 8);
      else if (memcmp(tag, "TEXS", 4) == 0 && sz >= 4)
        entry.texSize = *reinterpret_cast<const uint32_t *>(p + 8);
      else if (memcmp(tag, "TXFT", 4) == 0 && sz >= 4)
        entry.txft = *reinterpret_cast<const uint32_t *>(p + 8);
      else if (memcmp(tag, "TECM", 4) == 0 && sz >= 4)
        entry.tecm = *reinterpret_cast<const uint32_t *>(p + 8);
      else if (memcmp(tag, "TENT", 4) == 0 && sz >= 4)
        entry.tent = *reinterpret_cast<const uint32_t *>(p + 8);
      else if (memcmp(tag, "TEXT", 4) == 0) {
        const uint8_t *payload = p + 8;
        if (sz >= 4 && memcmp(payload, "DDS ", 4) == 0) {
          entry.ddsPtr = payload;
          entry.ddsSize = sz;
        }
        break;
      } else if (memcmp(tag, "TEXM", 4) == 0) {
        break;
      }

      p += 8 + sz;
    }

    if (entry.ddsPtr && entry.ddsSize)
      entries.push_back(entry);
  }
  return entries;
}

static bool LtmReadWholeFile(const std::filesystem::path &path,
                             std::vector<uint8_t> &data) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f.is_open())
    return false;
  const size_t fileSize = static_cast<size_t>(f.tellg());
  f.seekg(0);
  data.resize(fileSize);
  if (fileSize > 0) {
    f.read(reinterpret_cast<char *>(data.data()),
           static_cast<std::streamsize>(fileSize));
  }
  return f.good() || f.eof();
}

static bool LtmLooksLikeWParallaxLpr(
    const std::filesystem::path &lprPath,
    const std::vector<LtmTextureEntry> &lprTextures) {
  if (lprTextures.size() < 3)
    return false;

  std::string stem = lprPath.stem().string();
  std::transform(stem.begin(), stem.end(), stem.begin(),
                 [](unsigned char c) {
                   return static_cast<char>(std::tolower(c));
                 });
  if (stem.rfind("wp_", 0) != 0 && stem.find("wparallax") == std::string::npos)
    return false;

  const auto isBc6h = [](DXGI_FORMAT fmt) {
    return fmt == DXGI_FORMAT_BC6H_UF16 || fmt == DXGI_FORMAT_BC6H_SF16;
  };
  const auto isBc4 = [](DXGI_FORMAT fmt) {
    return fmt == DXGI_FORMAT_BC4_UNORM || fmt == DXGI_FORMAT_BC4_SNORM;
  };
  return isBc6h(lprTextures[0].authoredFormat) &&
         isBc6h(lprTextures[1].authoredFormat) &&
         isBc4(lprTextures[2].authoredFormat);
}

static void LtmImportLprCompanion(const std::string &lmodPath,
                                  std::vector<Material> *outMaterials,
                                  std::vector<Texture> *outTextures) {
  if (!outMaterials || outMaterials->empty() || !outTextures)
    return;

  std::filesystem::path lprPath(lmodPath);
  lprPath.replace_extension(".lpr");
  if (!std::filesystem::exists(lprPath))
    return;

  std::vector<uint8_t> lprData;
  if (!LtmReadWholeFile(lprPath, lprData) || lprData.empty()) {
    fprintf(stderr, "LMOD: failed to read LPR companion %s\n",
            lprPath.string().c_str());
    return;
  }

  const uint8_t *lprBase = lprData.data();
  const uint8_t *lprEnd = lprBase + lprData.size();
  std::vector<LtmTextureEntry> lprTextures =
      LtmScanEmbeddedTextureEntries(lprBase, lprEnd, false);
  for (LtmTextureEntry &entry : lprTextures) {
    if (!entry.ddsPtr || entry.ddsSize < 128)
      continue;
    if (LtmReadDDSFormat(entry.ddsPtr, entry.ddsSize, &entry.authoredFormat))
      entry.uploadedFormat = LtmRemoveSrgbViewFormat(entry.authoredFormat);
  }
  if (lprTextures.size() < 3) {
    fprintf(stderr,
            "LMOD: LPR companion %s has %zu DDS texture(s); expected day/night/depth.\n",
            lprPath.string().c_str(), lprTextures.size());
    return;
  }
  if (!LtmLooksLikeWParallaxLpr(lprPath, lprTextures)) {
    fprintf(stderr,
            "LMOD: LPR companion %s is not a recognized wParallax atlas set; skipping special import.\n",
            lprPath.string().c_str());
    return;
  }

  auto uploadCompanionTexture = [&](size_t entryIndex) -> int {
    if (entryIndex >= lprTextures.size())
      return -1;
    LtmTextureEntry &entry = lprTextures[entryIndex];
    Texture tex = LtmUploadDDS(entry.ddsPtr, entry.ddsSize,
                               &entry.authoredFormat);
    if (!tex.resource) {
      fprintf(stderr,
              "LMOD: failed to upload LPR texture %zu from %s\n",
              entryIndex, lprPath.string().c_str());
      return -1;
    }
    entry.uploadedTextureIndex = static_cast<int>(outTextures->size());
    entry.uploadedFormat = tex.format;
    outTextures->push_back(std::move(tex));
    return entry.uploadedTextureIndex;
  };

  const int dayTexture = uploadCompanionTexture(0);
  const int nightTexture = uploadCompanionTexture(1);
  const int alphaTexture = uploadCompanionTexture(2);

  Material &mat = (*outMaterials)[0];
  if (dayTexture >= 0) {
    mat.diffuseTexture = -1;
    mat.parallaxTexture = dayTexture;
    mat.parallaxMode = Material::kParallaxModeWindowBox;
    mat.parallaxDepthScale = 0.0f;
    mat.parallaxRoomDepth = 1.0f;
    mat.parallaxWindowAspect = 1.0f;
    mat.parallaxUvScale[0] = 1.0f;
    mat.parallaxUvScale[1] = 1.0f;
    mat.parallaxUvOffset[0] = 0.0f;
    mat.parallaxUvOffset[1] = 0.0f;
  }
  if (nightTexture >= 0) {
    mat.emissiveTexture = nightTexture;
    mat.emissiveTextureAmount = 1.0f;
    mat.emissiveColor[0] = 1.0f;
    mat.emissiveColor[1] = 1.0f;
    mat.emissiveColor[2] = 1.0f;
    mat.emissiveColor[3] = 1.0f;
    mat.emissiveIntensity = (std::max)(mat.emissiveIntensity, 1.0f);
  }
  if (alphaTexture >= 0) {
    mat.opacityTexture = alphaTexture;
    mat.opacityTextureAmount = 1.0f;
    mat.alphaMode = "MASK";
    mat.alphaCutoff = 0.02f;
  }
  mat.doubleSided = true;

  fprintf(stderr,
          "LMOD: wParallax LPR %s imported day=%d night=%d alpha=%d -> material %s\n",
          lprPath.string().c_str(), dayTexture, nightTexture, alphaTexture,
          mat.name);
}

// Scan for a 4-byte tag starting at or after `from`, up to `limit`.
// Returns pointer to the tag if found and its payload is within bounds, else nullptr.
static const uint8_t *LtmScanTag(const uint8_t *from, const uint8_t *limit,
                                  const char tag[4], uint32_t *outSize) {
  for (const uint8_t *p = from; p + 8 <= limit; ++p) {
    if (memcmp(p, tag, 4) == 0) {
      *outSize = *reinterpret_cast<const uint32_t *>(p + 4);
      if (p + 8 + *outSize <= limit)
        return p + 8; // payload pointer
    }
  }
  return nullptr;
}

// Returns the surfaceShaderNr value from a sibling .ltm.inn companion file,
// or -1 if the file is absent or the property is not found. The .inn is an
// EverMotion asset-browser metadata file whose "OO ClassInstance" block
// stores a float32 surfaceShaderNr identifying the canonical material slot.
static int LtmReadInnSurfaceShaderNr(const std::string &ltmPath) {
  std::filesystem::path p(ltmPath);
  std::vector<std::string> innCandidates;
  innCandidates.push_back(ltmPath + ".inn");
  {
    std::filesystem::path alt = p;
    alt.replace_extension(".inn");
    if (alt.string() != innCandidates[0])
      innCandidates.push_back(alt.string());
  }

  std::ifstream fi;
  std::string innPath;
  for (const std::string &candidate : innCandidates) {
    fi.open(candidate, std::ios::binary | std::ios::ate);
    if (fi.is_open()) {
      innPath = candidate;
      break;
    }
    fi.clear();
  }
  if (!fi.is_open()) {
    fprintf(stderr, "LTM: .inn companion not found for %s\n", ltmPath.c_str());
    return -1;
  }
  const size_t innSize = static_cast<size_t>(fi.tellg());
  fi.seekg(0);
  std::vector<uint8_t> buf(innSize);
  fi.read(reinterpret_cast<char *>(buf.data()), static_cast<std::streamsize>(innSize));
  fi.close();
  fprintf(stderr, "LTM: using companion metadata %s\n", innPath.c_str());

  // UTF-16LE: "surfaceShaderNr"
  static const uint8_t kName[] = {
    0x73,0x00,0x75,0x00,0x72,0x00,0x66,0x00,0x61,0x00,0x63,0x00,0x65,0x00,
    0x53,0x00,0x68,0x00,0x61,0x00,0x64,0x00,0x65,0x00,0x72,0x00,0x4e,0x00,
    0x72,0x00
  };
  static const size_t kNameLen = sizeof(kName);

  const uint8_t *base    = buf.data();
  const uint8_t *fileEnd = base + innSize;
  bool sawSurfaceShaderName = false;

  for (const uint8_t *p = base; p + 8 <= fileEnd; ++p) {
    if (memcmp(p, "IINW", 4) != 0) continue;
    const uint32_t sz = *reinterpret_cast<const uint32_t *>(p + 4);
    if (sz < kNameLen || p + 8 + sz > fileEnd) continue;
    if (memcmp(p + 8, kName, kNameLen) != 0) continue;
    sawSurfaceShaderName = true;
    // Found the IINW for "surfaceShaderNr". Scan forward for IIVE.
    const uint8_t *scan  = p + 8 + sz;
    const uint8_t *limit = (std::min)(scan + 4096, fileEnd);
    for (const uint8_t *q = scan; q + 8 <= limit; ++q) {
      if (memcmp(q, "IIVE", 4) != 0) continue;
      const uint32_t vsz = *reinterpret_cast<const uint32_t *>(q + 4);
      if (vsz == 4 && q + 12 <= fileEnd) {
        float fval;
        memcpy(&fval, q + 8, 4);
        fprintf(stderr,
                "LTM: .inn surfaceShaderNr=%.3f parsed from %s\n",
                (double)fval, innPath.c_str());
        return static_cast<int>(fval + 0.5f);
      }
    }
  }
  if (sawSurfaceShaderName) {
    fprintf(stderr,
            "LTM: .inn has surfaceShaderNr field but no nearby IIVE value in %s\n",
            innPath.c_str());
  }
  return -1;
}

bool LoadLTM(const std::string &path, std::vector<GpuMesh> &outMeshes,
             std::vector<Material> *outMaterials,
             std::vector<Texture> *outTextures,
             std::vector<ImportedSceneNode> *outSceneNodes) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f.is_open()) {
    fprintf(stderr, "LoadLTM: cannot open %s\n", path.c_str());
    return false;
  }
  size_t fileSize = static_cast<size_t>(f.tellg());
  f.seekg(0);
  std::vector<uint8_t> data(fileSize);
  f.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(fileSize));
  f.close();

  const uint8_t *const base    = data.data();
  const uint8_t *const fileEnd = base + fileSize;

  // ---- Scan for geometry blocks ----------------------------------------
  // The outer chunk structure has an IC** opaque section that defeats a linear
  // walker. Instead, we scan directly for VRCO tags and walk the known fixed
  // geometry sequence forward from each hit.
  //
  // Confirmed sequence (all chunks use tag[4] + uint32_le_size + payload):
  //   VRCO(4)  → vertex_count
  //   VCPU(1)  → flag byte
  //   VPPI(vc×12) → float32 xyz positions
  //   VNNI(vc×6)  → int16  xyz normals  (÷32767)
  //   VTD0(vc×8)  → float32 uv0
  //   VTO0(24)    → uv0 transform, skip
  //   VTD1(vc×8)  → float32 uv1, skip
  //   VTO1(24)    → uv1 transform, skip
  //   VTTB(vc×6)  → int16  xyz tangents (÷32767)
  //   VCNC(vc×4)  → uint16 matSlot per vertex (lo word)
  //   MBCT(4)     → 0xFFFFFFFF, skip
  //   POCO(4)     → index_count
  //   PO32(ic×4)  → uint32 triangle indices

  std::vector<LtmGeoBlock> geoBlocks;
  std::vector<LtmTextureEntry> textureEntries;
  std::string ext = std::filesystem::path(path).extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  const bool isLmod = ext == ".lmod";

  // Walk the file scanning for VRCO
  for (const uint8_t *scan = base; scan + 8 <= fileEnd; ) {
    if (memcmp(scan, "VRCO", 4) != 0) { ++scan; continue; }

    uint32_t vrcoSize = *reinterpret_cast<const uint32_t *>(scan + 4);
    if (vrcoSize < 4 || scan + 8 + vrcoSize > fileEnd) { ++scan; continue; }
    uint32_t vc = *reinterpret_cast<const uint32_t *>(scan + 8);
    if (vc == 0 || vc > 10000000) { ++scan; continue; } // sanity

    // Walk forward through the fixed sequence
    LtmGeoBlock gb;
    gb.vertexCount = vc;
    bool valid = true;
    const uint8_t *p = scan + 8 + vrcoSize; // after VRCO payload

    // Helper: consume a specific tag, optionally capture payload ptr
    auto consume = [&](const char tag[4], const uint8_t **capture) -> bool {
      if (p + 8 > fileEnd) return false;
      if (memcmp(p, tag, 4) != 0) return false;
      uint32_t sz = *reinterpret_cast<const uint32_t *>(p + 4);
      if (p + 8 + sz > fileEnd) return false;
      if (capture) *capture = p + 8;
      p += 8 + sz;
      return true;
    };
    auto skip = [&](const char tag[4]) -> bool { return consume(tag, nullptr); };

    const uint8_t *vcpu = nullptr, *vtd0 = nullptr, *vtd1 = nullptr;
    const uint8_t *vto0 = nullptr, *vto1 = nullptr, *mbct = nullptr;
    const uint8_t *poco_ptr = nullptr;

    if (!consume("VCPU", &vcpu))           { ++scan; continue; }
    if (!consume("VPPI", &gb.vppi))        { ++scan; continue; }
    if (!consume("VNNI", &gb.vnni))        { ++scan; continue; }
    if (!consume("VTD0", &vtd0))           { ++scan; continue; }
    gb.vtd0 = vtd0;
    if (!skip("VTO0"))                     { ++scan; continue; }
    // VTD1 and VTO1 are optional UV channel 1; skip them
    skip("VTD1");
    skip("VTO1");
    // Some LMOD meshes omit tangents; keep the block and use the fallback
    // tangent generated below.
    consume("VTTB", &gb.vttb);
    // VCNC: per-vertex material slot
    if (p + 8 <= fileEnd && memcmp(p, "VCNC", 4) == 0) {
      uint32_t sz = *reinterpret_cast<const uint32_t *>(p + 4);
      if (p + 8 + sz <= fileEnd && sz >= 2) {
        gb.matSlot = *reinterpret_cast<const uint16_t *>(p + 8);
      }
      p += 8 + sz;
    }
    skip("MBCT");
    // POCO: index count
    if (p + 8 + 4 <= fileEnd && memcmp(p, "POCO", 4) == 0) {
      uint32_t sz = *reinterpret_cast<const uint32_t *>(p + 4);
      if (sz >= 4) gb.indexCount = *reinterpret_cast<const uint32_t *>(p + 8);
      p += 8 + sz;
    }
    // PO32/PODA: index data. PODA is a 16-bit index stream used by some LMODs.
    if (p + 8 <= fileEnd && memcmp(p, "PO32", 4) == 0) {
      uint32_t sz = *reinterpret_cast<const uint32_t *>(p + 4);
      if (p + 8 + sz <= fileEnd) {
        gb.po32 = p + 8;
        gb.indexCount = sz / 4;
        p += 8 + sz;
      }
    } else if (p + 8 <= fileEnd && memcmp(p, "PODA", 4) == 0) {
      uint32_t sz = *reinterpret_cast<const uint32_t *>(p + 4);
      if (p + 8 + sz <= fileEnd) {
        gb.poda = p + 8;
        gb.indexCount = sz / 2;
        p += 8 + sz;
      }
    }

    if (gb.vppi && (gb.po32 || gb.poda) && gb.indexCount > 0) {
      if (isLmod) {
        gb.materialName =
            LtmFindNearestLmodMaterialName(base, fileEnd, scan);
      }
      geoBlocks.push_back(gb);
      scan = p; // continue scanning after this block
    } else {
      ++scan;
    }
  }

  // ---- Scan for embedded textures (TEXM + TEXT groups) ------------------
  textureEntries = LtmScanEmbeddedTextureEntries(base, fileEnd, isLmod);

  if (geoBlocks.empty()) {
    fprintf(stderr, "LoadLTM: no geometry found in %s\n", path.c_str());
    return false;
  }
  fprintf(stderr, "LTM: %zu mesh block(s), %zu DDS texture(s) - %s\n",
          geoBlocks.size(), textureEntries.size(), path.c_str());

  const std::vector<LtmTextureSemantic> textureSemanticHints =
      isLmod ? LtmReadLmodTextureSemanticHints(base, fileEnd)
             : std::vector<LtmTextureSemantic>();
  if (!textureSemanticHints.empty()) {
    fprintf(stderr, "LMOD: %zu texture semantic hint(s):",
            textureSemanticHints.size());
    for (LtmTextureSemantic semantic : textureSemanticHints)
      fprintf(stderr, " %s", LtmTextureSemanticName(semantic));
    fprintf(stderr, "\n");
  }

  // De-duplicate material slots.
  std::vector<uint16_t> uniqueSlots;
  std::vector<std::string> uniqueMaterialNames;
  for (size_t bi = 0; bi < geoBlocks.size(); ++bi) {
    const auto &gb = geoBlocks[bi];
    if (isLmod) {
      std::string name = gb.materialName.empty()
                             ? ("LMOD_mesh_" + std::to_string(bi))
                             : gb.materialName;
      bool found = false;
      for (const std::string &existing : uniqueMaterialNames) {
        if (existing == name) {
          found = true;
          break;
        }
      }
      if (!found)
        uniqueMaterialNames.push_back(name);
    } else {
      bool found = false;
      for (auto s : uniqueSlots) if (s == gb.matSlot) { found = true; break; }
      if (!found) uniqueSlots.push_back(gb.matSlot);
    }
  }

  // Read companion .inn to find the primary surface shader slot.
  const int innShaderNr = LtmReadInnSurfaceShaderNr(path);
  int primaryMi = -1;
  if (!isLmod && innShaderNr >= 0) {
    for (int si = 0; si < (int)uniqueSlots.size(); ++si) {
      if ((int)uniqueSlots[(size_t)si] == innShaderNr) {
        primaryMi = si;
        break;
      }
    }
    fprintf(stderr, "LTM: .inn surfaceShaderNr=%d -> primaryMi=%d\n",
            innShaderNr, primaryMi);
  }

  std::vector<int> slotToMaterialIndex(65536, -1);
  if (outMaterials) {
    if (isLmod) {
      for (const std::string &name : uniqueMaterialNames) {
        Material mat;
        snprintf(mat.name, sizeof(mat.name), "%s", name.c_str());
        std::string lowerName = name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                       [](unsigned char c) {
                         return static_cast<char>(std::tolower(c));
                       });
        if (lowerName.find("glass") != std::string::npos) {
          mat.diffuseColor[0] = 0.02f;
          mat.diffuseColor[1] = 0.02f;
          mat.diffuseColor[2] = 0.025f;
          mat.roughness = 0.02f;
          mat.specularWeight = 1.0f;
        }
        outMaterials->push_back(mat);
      }
    } else {
      for (uint16_t slot : uniqueSlots) {
        Material mat;
        snprintf(mat.name, sizeof(mat.name), "LTM_mat_%u", (unsigned)slot);
        slotToMaterialIndex[slot] = static_cast<int>(outMaterials->size());
        outMaterials->push_back(mat);
      }
    }
  }

  const std::string stem = std::filesystem::path(path).stem().string();

  for (size_t bi = 0; bi < geoBlocks.size(); ++bi) {
    const LtmGeoBlock &gb = geoBlocks[bi];
    const uint32_t vc = gb.vertexCount;
    const uint32_t ic = gb.indexCount;

    std::vector<Vertex> verts(vc);
    for (uint32_t i = 0; i < vc; ++i) {
      Vertex &v = verts[i];
      memcpy(v.pos, gb.vppi + (size_t)i * 12, 12);

      if (gb.vnni) {
        const int16_t *n = reinterpret_cast<const int16_t *>(gb.vnni + (size_t)i * 6);
        v.normal[0] = n[0] / 32767.0f;
        v.normal[1] = n[1] / 32767.0f;
        v.normal[2] = n[2] / 32767.0f;
      } else { v.normal[0] = v.normal[1] = 0.0f; v.normal[2] = 1.0f; }

      if (gb.vttb) {
        const int16_t *t = reinterpret_cast<const int16_t *>(gb.vttb + (size_t)i * 6);
        v.tangent[0] = t[0] / 32767.0f;
        v.tangent[1] = t[1] / 32767.0f;
        v.tangent[2] = t[2] / 32767.0f;
        v.tangent[3] = 1.0f;
      } else { v.tangent[0] = 1.0f; v.tangent[1] = v.tangent[2] = 0.0f; v.tangent[3] = 1.0f; }

      if (gb.vtd0) memcpy(v.uv, gb.vtd0 + (size_t)i * 8, 8);
    }

    std::vector<uint32_t> inds(ic);
    if (gb.po32) {
      memcpy(inds.data(), gb.po32, (size_t)ic * 4);
    } else if (gb.poda) {
      for (uint32_t i = 0; i < ic; ++i) {
        uint16_t idx = 0;
        memcpy(&idx, gb.poda + (size_t)i * 2, 2);
        inds[i] = idx;
      }
    }

    int localMatIdx = 0;
    if (isLmod) {
      const std::string name = gb.materialName.empty()
                                   ? ("LMOD_mesh_" + std::to_string(bi))
                                   : gb.materialName;
      for (int mi = 0; mi < (int)uniqueMaterialNames.size(); ++mi) {
        if (uniqueMaterialNames[(size_t)mi] == name) {
          localMatIdx = mi;
          break;
        }
      }
    } else {
      for (int si = 0; si < (int)uniqueSlots.size(); ++si)
        if (uniqueSlots[si] == gb.matSlot) { localMatIdx = si; break; }
    }

    GpuMesh gm = LoadMeshFromMemory(verts, inds);
    gm.materialIndex = localMatIdx;
    gm.materialSlot  = localMatIdx;
    outMeshes.push_back(std::move(gm));

    if (outSceneNodes) {
      ImportedSceneNode snode;
      snode.name = stem + "_mesh" + std::to_string(bi);
      snode.meshIndices.push_back(bi);
      outSceneNodes->push_back(snode);
    }
  }

  if (outTextures) {
    std::vector<LtmTextureSemantic> semanticByEntry(textureEntries.size(),
                                                    LtmTextureSemantic::Unknown);
    for (size_t ti = 0; ti < textureEntries.size(); ++ti) {
      LtmTextureEntry &entry = textureEntries[ti];
      Texture tex = LtmUploadDDS(entry.ddsPtr, entry.ddsSize,
                                 &entry.authoredFormat);
      if (tex.resource) {
        entry.uploadedTextureIndex = static_cast<int>(outTextures->size());
        entry.uploadedFormat = tex.format;
        outTextures->push_back(std::move(tex));
      } else {
        fprintf(stderr,
                "LTM: warning - failed to upload DDS texture (entry=%zu mapType=%u materialSlot=%d tex=%ux%u txft=%u tecm=%u)\n",
                ti, entry.mapType, entry.materialSlot, entry.texW, entry.texH,
                entry.txft, entry.tecm);
      }
    }

    // First-pass semantic inference from explicit metadata and format hints.
    for (size_t ti = 0; ti < textureEntries.size(); ++ti) {
      const LtmTextureEntry &entry = textureEntries[ti];
      const Texture *uploadedTex =
          (entry.uploadedTextureIndex >= 0 &&
           (size_t)entry.uploadedTextureIndex < outTextures->size())
              ? &(*outTextures)[(size_t)entry.uploadedTextureIndex]
              : nullptr;
      semanticByEntry[ti] = LtmInferSemanticFromEntry(entry, uploadedTex);
      if (!isLmod && ti < textureSemanticHints.size() &&
          textureSemanticHints[ti] != LtmTextureSemantic::Unknown) {
        semanticByEntry[ti] = textureSemanticHints[ti];
      }
    }

    if (isLmod) {
      LtmApplyLmodTextureOrderHeuristics(textureEntries, semanticByEntry);
    }

    // Second-pass pairing heuristic: same-size sRGB + linear textures are
    // typically color/normal pairs in these files.
    for (size_t ti = 0; ti < textureEntries.size(); ++ti) {
      const LtmTextureEntry &a = textureEntries[ti];
      if (a.uploadedTextureIndex < 0 || !LtmIsSrgbFormat(a.authoredFormat))
        continue;
      if (semanticByEntry[ti] == LtmTextureSemantic::Unknown)
        semanticByEntry[ti] = LtmTextureSemantic::Diffuse;

      size_t best = textureEntries.size();
      size_t bestDistance = (size_t)-1;
      for (size_t tj = 0; tj < textureEntries.size(); ++tj) {
        if (tj == ti)
          continue;
        const LtmTextureEntry &b = textureEntries[tj];
        if (b.uploadedTextureIndex < 0)
          continue;
        if (LtmIsSrgbFormat(b.authoredFormat))
          continue;
        if (semanticByEntry[tj] != LtmTextureSemantic::Unknown &&
            semanticByEntry[tj] != LtmTextureSemantic::Normal)
          continue;
        if (a.texW != 0 && b.texW != 0 && a.texW != b.texW)
          continue;
        if (a.texH != 0 && b.texH != 0 && a.texH != b.texH)
          continue;
        size_t d = (ti > tj) ? (ti - tj) : (tj - ti);
        if (d < bestDistance) {
          best = tj;
          bestDistance = d;
        }
      }
      if (best != textureEntries.size())
        semanticByEntry[best] = LtmTextureSemantic::Normal;
    }

    // Remaining unknown linear LTM maps are more likely packed surface data
    // than base color. LMOD texture streams can also contain thumbnails and
    // alternate color payloads, so leave those unclassified unless metadata or
    // the material-run heuristic above identified the slot.
    for (size_t ti = 0; ti < textureEntries.size(); ++ti) {
      if (semanticByEntry[ti] != LtmTextureSemantic::Unknown)
        continue;
      const LtmTextureEntry &entry = textureEntries[ti];
      if (entry.uploadedTextureIndex < 0)
        continue;
      if (isLmod && !LtmIsSrgbFormat(entry.authoredFormat))
        continue;
      semanticByEntry[ti] = LtmIsSrgbFormat(entry.authoredFormat)
                                ? LtmTextureSemantic::Diffuse
                                : LtmTextureSemantic::MetalRough;
    }

    if (outMaterials && !outMaterials->empty()) {
      std::vector<std::vector<const LtmTextureEntry *>> perMaterial(outMaterials->size());
      std::vector<std::vector<size_t>> perMaterialEntryIndices(outMaterials->size());
      std::vector<const LtmTextureEntry *> globalFallback;
      std::vector<size_t> globalFallbackIndices;
      bool hasMaterialLinkedTextures = false;

      for (size_t ti = 0; ti < textureEntries.size(); ++ti) {
        const LtmTextureEntry &entry = textureEntries[ti];
        if (entry.uploadedTextureIndex < 0)
          continue;
        if (isLmod && !entry.materialName.empty()) {
          bool linkedByName = false;
          for (size_t mi = 0; mi < uniqueMaterialNames.size() &&
                              mi < perMaterial.size(); ++mi) {
            if (uniqueMaterialNames[mi] == entry.materialName) {
              perMaterial[mi].push_back(&entry);
              perMaterialEntryIndices[mi].push_back(ti);
              hasMaterialLinkedTextures = true;
              linkedByName = true;
              break;
            }
          }
          if (linkedByName)
            continue;
        }
        if (entry.materialSlot >= 0 && entry.materialSlot < (int)slotToMaterialIndex.size()) {
          int mi = slotToMaterialIndex[(size_t)entry.materialSlot];
          if (mi >= 0 && mi < (int)perMaterial.size()) {
            perMaterial[(size_t)mi].push_back(&entry);
            perMaterialEntryIndices[(size_t)mi].push_back(ti);
            hasMaterialLinkedTextures = true;
            continue;
          }
        }
        globalFallback.push_back(&entry);
        globalFallbackIndices.push_back(ti);
      }

      auto assignSemanticTexture = [](Material &mat, LtmTextureSemantic semantic,
                                      int textureIndex) {
        switch (semantic) {
        case LtmTextureSemantic::Diffuse:
          if (mat.diffuseTexture < 0) mat.diffuseTexture = textureIndex;
          break;
        case LtmTextureSemantic::Normal:
          if (mat.normalTexture < 0) mat.normalTexture = textureIndex;
          break;
        case LtmTextureSemantic::Opacity:
          if (mat.opacityTexture < 0) {
            mat.opacityTexture = textureIndex;
            mat.alphaMode = "MASK";
          }
          break;
        case LtmTextureSemantic::Emissive:
          if (mat.emissiveTexture < 0) mat.emissiveTexture = textureIndex;
          break;
        case LtmTextureSemantic::Occlusion:
          if (mat.occlusionTexture < 0) mat.occlusionTexture = textureIndex;
          break;
        case LtmTextureSemantic::MetalRough:
          if (mat.metalRoughTexture < 0) mat.metalRoughTexture = textureIndex;
          break;
        case LtmTextureSemantic::Metalness:
          if (mat.metalnessTexture < 0) mat.metalnessTexture = textureIndex;
          break;
        case LtmTextureSemantic::Roughness:
          if (mat.roughnessGlossTexture < 0) mat.roughnessGlossTexture = textureIndex;
          break;
        default:
          if (mat.diffuseTexture < 0) mat.diffuseTexture = textureIndex;
          else if (mat.normalTexture < 0) mat.normalTexture = textureIndex;
          else if (mat.metalRoughTexture < 0) mat.metalRoughTexture = textureIndex;
          else if (mat.occlusionTexture < 0) mat.occlusionTexture = textureIndex;
          break;
        }
      };

      for (size_t mi = 0; mi < outMaterials->size(); ++mi) {
        Material &mat = (*outMaterials)[mi];
        std::vector<const LtmTextureEntry *> candidates = perMaterial[mi];
        std::vector<size_t> candidateIndices = perMaterialEntryIndices[mi];
        if (candidates.empty())
          candidates = globalFallback;
        if (candidateIndices.empty())
          candidateIndices = globalFallbackIndices;

        // When textures are explicitly linked to materials, apply those first.
        if (hasMaterialLinkedTextures) {
          for (size_t ci = 0; ci < candidates.size() && ci < candidateIndices.size(); ++ci) {
            const LtmTextureEntry *entry = candidates[ci];
            const LtmTextureSemantic semantic = semanticByEntry[candidateIndices[ci]];
            if (isLmod && semantic == LtmTextureSemantic::Unknown)
              continue;
            assignSemanticTexture(mat, semantic, entry->uploadedTextureIndex);
          }
        }

        // If metadata does not link textures to materials, distribute texture
        // sets across materials so each material gets distinct maps.
        if (!hasMaterialLinkedTextures) {
          std::vector<int> diffuseTex;
          std::vector<int> normalTex;
          std::vector<int> packedTex;
          std::vector<int> emissiveTex;
          std::vector<uint32_t> packedTexSizes; // parallel to packedTex
          for (size_t ti = 0; ti < textureEntries.size(); ++ti) {
            const LtmTextureEntry &entry = textureEntries[ti];
            if (entry.uploadedTextureIndex < 0)
              continue;
            switch (semanticByEntry[ti]) {
            case LtmTextureSemantic::Diffuse:
              diffuseTex.push_back(entry.uploadedTextureIndex);
              break;
            case LtmTextureSemantic::Normal:
              normalTex.push_back(entry.uploadedTextureIndex);
              break;
            case LtmTextureSemantic::MetalRough:
            case LtmTextureSemantic::Roughness:
            case LtmTextureSemantic::Metalness:
              packedTex.push_back(entry.uploadedTextureIndex);
              packedTexSizes.push_back(entry.texSize);
              break;
            case LtmTextureSemantic::Emissive:
              emissiveTex.push_back(entry.uploadedTextureIndex);
              break;
            default:
              break;
            }
          }

          // EverMotion LTM assets embed viewer/preview textures BEFORE the
          // first ICSI material-group boundary. These appear as leading entries
          // in the sorted semantic arrays and must be skipped.
          //
          // For roughness, tiny 16x16 BC4 placeholder tiles (TEXS ≤ 1024 bytes)
          // are filler entries that don't correspond to any material slot — they
          // are excluded from the substantive set used for assignment.
          //
          // Offset rule: skip leading (nType − nMats) entries so that material 0
          // always gets the first post-preview texture of each semantic type.
          const int nMats = static_cast<int>(outMaterials->size());

          std::vector<int> substantivePacked;
          for (size_t i = 0; i < packedTex.size(); ++i) {
            if (packedTexSizes[i] > 1024)
              substantivePacked.push_back(packedTex[i]);
          }

          const int diffOff  = isLmod ? 0 : std::max(0, (int)diffuseTex.size()       - nMats);
          const int normOff  = isLmod ? 0 : std::max(0, (int)normalTex.size()        - nMats);
          const int packOff  = isLmod ? 0 : std::max(0, (int)substantivePacked.size() - nMats);
          const int setIdx   = static_cast<int>(mi);

          if (mat.diffuseTexture < 0 && !diffuseTex.empty())
            mat.diffuseTexture = diffuseTex[(setIdx + diffOff) % (int)diffuseTex.size()];
          if (mat.normalTexture < 0 && !normalTex.empty())
            mat.normalTexture = normalTex[(setIdx + normOff) % (int)normalTex.size()];
          if (mat.metalRoughTexture < 0 && !substantivePacked.empty())
            mat.metalRoughTexture = substantivePacked[(setIdx + packOff) % (int)substantivePacked.size()];
          if (mat.emissiveTexture < 0 && !emissiveTex.empty())
            mat.emissiveTexture = emissiveTex[setIdx % (int)emissiveTex.size()];

          const int slotId = (mi < uniqueSlots.size())
                                 ? static_cast<int>(uniqueSlots[mi]) : -1;
          fprintf(stderr,
              "LTM: fallback set map material %s slot=%d mi=%zu "
              "diffOff=%d normOff=%d packOff=%d -> diff=%d norm=%d mr=%d\n",
              mat.name, slotId, mi,
              diffOff, normOff, packOff,
              mat.diffuseTexture, mat.normalTexture, mat.metalRoughTexture);
        }

        // LTM foliage assets commonly encode cutout opacity in base-color
        // alpha. LMOD diffuse DDS alpha is not reliably authored opacity;
        // LMOD still becomes masked above when it has an explicit opacity map.
        if (!isLmod && mat.diffuseTexture >= 0 && mat.alphaMode == "OPAQUE") {
          mat.alphaMode = "MASK";
          mat.alphaCutoff = (std::clamp)(mat.alphaCutoff, 0.2f, 0.5f);
        }

        fprintf(stderr,
                "LTM: material %s textures: diff=%d normal=%d mr=%d occ=%d em=%d op=%d\n",
                mat.name, mat.diffuseTexture, mat.normalTexture,
                mat.metalRoughTexture, mat.occlusionTexture,
                mat.emissiveTexture, mat.opacityTexture);
      }

      for (size_t ti = 0; ti < textureEntries.size(); ++ti) {
        const LtmTextureEntry &entry = textureEntries[ti];
        if (entry.uploadedTextureIndex < 0)
          continue;
        LtmTextureSemantic semantic = semanticByEntry[ti];
        fprintf(stderr,
                "LTM: tex entry %zu -> texIdx=%d mapType=%u materialSlot=%d semantic=%s tex=%ux%u txft=%u tecm=%u fmt=%u authoredFmt=%u\n",
                ti, entry.uploadedTextureIndex, entry.mapType,
                entry.materialSlot, LtmTextureSemanticName(semantic),
                entry.texW, entry.texH, entry.txft, entry.tecm,
                (unsigned)entry.uploadedFormat,
                (unsigned)entry.authoredFormat);
      }
    }
  }

  if (isLmod) {
    LtmImportLprCompanion(path, outMaterials, outTextures);
  }

  return true;
}


bool LoadModel(const std::string &path, std::vector<GpuMesh> &outMeshes,
               std::vector<Material> *outMaterials,
               std::vector<Texture> *outTextures,
               const float *rootTranslation,
               std::vector<ImportedSceneNode> *outSceneNodes) {
  std::string ext = std::filesystem::path(path).extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  if (ext == ".ltm" || ext == ".lmod") {
    return LoadLTM(path, outMeshes, outMaterials, outTextures, outSceneNodes);
  } else if (ext == ".skp") {
    return LoadSkp(path, outMeshes, outMaterials, outTextures, rootTranslation,
                   outSceneNodes);
  } else if (ext == ".gltf" || ext == ".glb") {
    return LoadGltf(path, outMeshes, outMaterials, outTextures,
                    rootTranslation, outSceneNodes);
  } else {
    return LoadWithAssimp(path, outMeshes, outMaterials, outTextures,
                          rootTranslation, outSceneNodes);
  }
}

Texture LoadTextureFromEncodedMemory(const void *src, size_t size,
                                     bool isHDRHint) {
  if (!s_device || !src || size == 0) {
    return {};
  }

  const auto *bytes = static_cast<const unsigned char *>(src);
  int width = 0;
  int height = 0;
  int comp = 0;

  EXRVersion exrVersion;
  if (ParseEXRVersionFromMemory(&exrVersion, bytes, size) == TINYEXR_SUCCESS) {
    float *data = nullptr;
    const char *err = nullptr;
    if (LoadEXRFromMemory(&data, &width, &height, bytes, size, &err) ==
        TINYEXR_SUCCESS) {
      Texture tex;
      CreateGpuTexture(data, width, height, 4, DXGI_FORMAT_R32G32B32A32_FLOAT,
                       tex);
      free(data);
      return tex;
    }
    if (err) {
      fprintf(stderr, "tinyexr error: %s\n", err);
      FreeEXRErrorMessage(err);
    }
  }

  auto loadHdr = [&]() -> Texture {
    float *fdata =
        stbi_loadf_from_memory(bytes, static_cast<int>(size), &width, &height,
                               &comp, 4);
    if (!fdata) {
      return {};
    }
    Texture tex;
    CreateGpuTexture(fdata, width, height, 4, DXGI_FORMAT_R32G32B32A32_FLOAT,
                     tex);
    stbi_image_free(fdata);
    return tex;
  };

  auto loadLdr = [&]() -> Texture {
    unsigned char *data =
        stbi_load_from_memory(bytes, static_cast<int>(size), &width, &height,
                              &comp, 4);
    if (!data) {
      return {};
    }
    Texture tex;
    CreateGpuTexture(data, width, height, 4, DXGI_FORMAT_R8G8B8A8_UNORM, tex);
    stbi_image_free(data);
    return tex;
  };

  if (isHDRHint) {
    Texture tex = loadHdr();
    if (tex.resource) {
      return tex;
    }
    return loadLdr();
  }

  Texture tex = loadLdr();
  if (tex.resource) {
    return tex;
  }
  return loadHdr();
}

Texture LoadTextureFromEncodedMemory(const void *src, size_t size,
                                     bool isHDRHint,
                                     TextureUsageSemantic semantic) {
  Texture tex = LoadTextureFromEncodedMemory(src, size, isHDRHint);
  if (tex.resource) {
    ApplyTextureCompressionForUsage(
        tex, isHDRHint ? TextureUsageSemantic::Hdr : semantic);
  }
  return tex;
}

Texture LoadTextureFromMemory(const void *src, int width, int height,
                              DXGI_FORMAT format) {
  Texture tex;
  CreateGpuTexture(src, width, height, 4, format, tex);
  return tex;
}

Texture LoadTextureFromMemoryMipChain(const void *src, size_t srcSize,
                                      int width, int height,
                                      DXGI_FORMAT format,
                                      uint32_t mipLevels) {
  Texture tex;
  if (!s_device || !src || srcSize == 0 || width <= 0 || height <= 0) {
    return tex;
  }

  const uint32_t requestedMips = std::max(1u, mipLevels);
  const uint32_t baseW = static_cast<uint32_t>(width);
  const uint32_t baseH = static_cast<uint32_t>(height);
  const size_t baseBytes = TightlyPackedMipSize(format, baseW, baseH);
  if (baseBytes == 0 || srcSize < baseBytes) {
    return tex;
  }

  if (!IsBlockCompressedTextureFormat(format) && srcSize == baseBytes) {
    return LoadTextureFromMemory(src, width, height, format);
  }

  size_t tightBytes = 0;
  uint32_t usableMips = 0;
  for (uint32_t mip = 0; mip < requestedMips; ++mip) {
    const uint32_t mipW = std::max(1u, baseW >> mip);
    const uint32_t mipH = std::max(1u, baseH >> mip);
    const size_t mipBytes = TightlyPackedMipSize(format, mipW, mipH);
    if (mipBytes == 0 || tightBytes + mipBytes > srcSize) {
      break;
    }
    tightBytes += mipBytes;
    ++usableMips;
  }
  if (usableMips == 0) {
    return tex;
  }

  D3D12_RESOURCE_DESC texDesc = {};
  texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  texDesc.Width = static_cast<UINT>(width);
  texDesc.Height = static_cast<UINT>(height);
  texDesc.DepthOrArraySize = 1;
  texDesc.MipLevels = static_cast<UINT16>(usableMips);
  texDesc.Format = format;
  texDesc.SampleDesc.Count = 1;
  texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

  D3D12_HEAP_PROPERTIES defaultHeapProps = {D3D12_HEAP_TYPE_DEFAULT,
                                            D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                                            D3D12_MEMORY_POOL_UNKNOWN, 1, 1};
  if (FAILED(s_device->CreateCommittedResource(
          &defaultHeapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
          D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
          IID_PPV_ARGS(&tex.resource)))) {
    return {};
  }

  std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(usableMips);
  std::vector<UINT> numRows(usableMips);
  std::vector<UINT64> rowBytes(usableMips);
  UINT64 totalBytes = 0;
  s_device->GetCopyableFootprints(&texDesc, 0, usableMips, 0,
                                  footprints.data(), numRows.data(),
                                  rowBytes.data(), &totalBytes);

  D3D12_HEAP_PROPERTIES uploadHeapProps = {D3D12_HEAP_TYPE_UPLOAD,
                                           D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                                           D3D12_MEMORY_POOL_UNKNOWN, 1, 1};
  D3D12_RESOURCE_DESC bufDesc = {};
  bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  bufDesc.Width = totalBytes;
  bufDesc.Height = 1;
  bufDesc.DepthOrArraySize = 1;
  bufDesc.MipLevels = 1;
  bufDesc.Format = DXGI_FORMAT_UNKNOWN;
  bufDesc.SampleDesc.Count = 1;
  bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

  ComPtr<ID3D12Resource> uploadBuffer;
  ThrowIfFailed(s_device->CreateCommittedResource(
      &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer)));

  uint8_t *mapped = nullptr;
  ThrowIfFailed(uploadBuffer->Map(0, nullptr,
                                  reinterpret_cast<void **>(&mapped)));

  const uint8_t *tightSrc = static_cast<const uint8_t *>(src);
  size_t srcOffset = 0;
  for (uint32_t mip = 0; mip < usableMips; ++mip) {
    const uint32_t mipW = std::max(1u, baseW >> mip);
    const uint32_t mipH = std::max(1u, baseH >> mip);
    UINT rows = mipH;
    size_t rowPitch = static_cast<size_t>(mipW) * TextureBytesPerPixel(format);
    if (IsBlockCompressedTextureFormat(format)) {
      rows = (mipH + 3u) / 4u;
      rowPitch = static_cast<size_t>((mipW + 3u) / 4u) *
                 TextureBlockBytes(format);
    }

    uint8_t *dst = mapped + footprints[mip].Offset;
    for (UINT row = 0; row < rows; ++row) {
      memcpy(dst + static_cast<size_t>(row) *
                       footprints[mip].Footprint.RowPitch,
             tightSrc + srcOffset + static_cast<size_t>(row) * rowPitch,
             rowPitch);
    }
    srcOffset += rows * rowPitch;
  }
  uploadBuffer->Unmap(0, nullptr);

  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> cmdList;
  ThrowIfFailed(s_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                 IID_PPV_ARGS(&allocator)));
  ThrowIfFailed(s_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                            allocator.Get(), nullptr,
                                            IID_PPV_ARGS(&cmdList)));

  for (uint32_t mip = 0; mip < usableMips; ++mip) {
    D3D12_TEXTURE_COPY_LOCATION dst = {
        tex.resource.Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, mip};
    D3D12_TEXTURE_COPY_LOCATION srcLoc = {
        uploadBuffer.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
        footprints[mip]};
    cmdList->CopyTextureRegion(&dst, 0, 0, 0, &srcLoc, nullptr);
  }

  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = tex.resource.Get();
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  cmdList->ResourceBarrier(1, &barrier);

  ExecuteCommandListAndWait(cmdList.Get());

  tex.width = static_cast<UINT>(width);
  tex.height = static_cast<UINT>(height);
  tex.format = format;
  tex.mipLevels = usableMips;
  tex.cpuFormat = format;
  tex.cpuMipLevels = usableMips;
  tex.gpuCompressed = IsBlockCompressedTextureFormat(format);
  tex.compressionMode = TextureCompressionMode::Off;
  tex.cpuData.assign(tightSrc, tightSrc + tightBytes);
  return tex;
}

static bool TextureCpuDataIsRgba8(const Texture &texture) {
  return texture.cpuFormat == DXGI_FORMAT_R8G8B8A8_UNORM &&
         texture.cpuMipLevels == 1 && texture.width > 0 && texture.height > 0 &&
         texture.cpuData.size() >=
             static_cast<size_t>(texture.width) * texture.height * 4;
}

static bool TextureHasUsefulAlpha(const uint8_t *rgba, uint32_t width,
                                  uint32_t height) {
  if (!rgba) {
    return false;
  }
  const size_t pixelCount = static_cast<size_t>(width) * height;
  for (size_t i = 0; i < pixelCount; ++i) {
    if (rgba[i * 4 + 3] < 250) {
      return true;
    }
  }
  return false;
}

static void DownsampleRgba2x(const std::vector<uint8_t> &src, uint32_t srcW,
                             uint32_t srcH, std::vector<uint8_t> &dst,
                             uint32_t &dstW, uint32_t &dstH) {
  dstW = std::max(1u, srcW >> 1);
  dstH = std::max(1u, srcH >> 1);
  dst.assign(static_cast<size_t>(dstW) * dstH * 4, 0);

  for (uint32_t y = 0; y < dstH; ++y) {
    for (uint32_t x = 0; x < dstW; ++x) {
      uint32_t sum[4] = {};
      uint32_t count = 0;
      for (uint32_t oy = 0; oy < 2; ++oy) {
        for (uint32_t ox = 0; ox < 2; ++ox) {
          const uint32_t sx = std::min(srcW - 1, x * 2 + ox);
          const uint32_t sy = std::min(srcH - 1, y * 2 + oy);
          const uint8_t *p = src.data() + (static_cast<size_t>(sy) * srcW + sx) * 4;
          sum[0] += p[0];
          sum[1] += p[1];
          sum[2] += p[2];
          sum[3] += p[3];
          ++count;
        }
      }
      uint8_t *d = dst.data() + (static_cast<size_t>(y) * dstW + x) * 4;
      d[0] = static_cast<uint8_t>(sum[0] / count);
      d[1] = static_cast<uint8_t>(sum[1] / count);
      d[2] = static_cast<uint8_t>(sum[2] / count);
      d[3] = static_cast<uint8_t>(sum[3] / count);
    }
  }
}

static void FillRgbaBlock(const uint8_t *rgba, uint32_t width, uint32_t height,
                          uint32_t blockX, uint32_t blockY,
                          uint8_t block[64]) {
  for (uint32_t y = 0; y < 4; ++y) {
    for (uint32_t x = 0; x < 4; ++x) {
      const uint32_t sx = std::min(width - 1, blockX * 4 + x);
      const uint32_t sy = std::min(height - 1, blockY * 4 + y);
      const uint8_t *src = rgba + (static_cast<size_t>(sy) * width + sx) * 4;
      uint8_t *dst = block + (y * 4 + x) * 4;
      dst[0] = src[0];
      dst[1] = src[1];
      dst[2] = src[2];
      dst[3] = src[3];
    }
  }
}

static void EncodeBc4Block(const uint8_t values[16], uint8_t outBlock[8]) {
  uint8_t minValue = 255;
  uint8_t maxValue = 0;
  for (int i = 0; i < 16; ++i) {
    minValue = std::min(minValue, values[i]);
    maxValue = std::max(maxValue, values[i]);
  }

  outBlock[0] = maxValue;
  outBlock[1] = minValue;

  uint8_t palette[8] = {};
  palette[0] = maxValue;
  palette[1] = minValue;
  if (maxValue > minValue) {
    palette[2] = static_cast<uint8_t>((6 * maxValue + 1 * minValue + 3) / 7);
    palette[3] = static_cast<uint8_t>((5 * maxValue + 2 * minValue + 3) / 7);
    palette[4] = static_cast<uint8_t>((4 * maxValue + 3 * minValue + 3) / 7);
    palette[5] = static_cast<uint8_t>((3 * maxValue + 4 * minValue + 3) / 7);
    palette[6] = static_cast<uint8_t>((2 * maxValue + 5 * minValue + 3) / 7);
    palette[7] = static_cast<uint8_t>((1 * maxValue + 6 * minValue + 3) / 7);
  } else {
    palette[2] = static_cast<uint8_t>((4 * maxValue + 1 * minValue + 2) / 5);
    palette[3] = static_cast<uint8_t>((3 * maxValue + 2 * minValue + 2) / 5);
    palette[4] = static_cast<uint8_t>((2 * maxValue + 3 * minValue + 2) / 5);
    palette[5] = static_cast<uint8_t>((1 * maxValue + 4 * minValue + 2) / 5);
    palette[6] = 0;
    palette[7] = 255;
  }

  uint64_t packed = 0;
  for (int i = 0; i < 16; ++i) {
    int bestIndex = 0;
    int bestError = 256 * 256;
    for (int p = 0; p < 8; ++p) {
      const int error = std::abs(static_cast<int>(values[i]) -
                                 static_cast<int>(palette[p]));
      if (error < bestError) {
        bestError = error;
        bestIndex = p;
      }
    }
    packed |= (static_cast<uint64_t>(bestIndex) & 0x7ull) << (i * 3);
  }

  for (int i = 0; i < 6; ++i) {
    outBlock[2 + i] = static_cast<uint8_t>((packed >> (8 * i)) & 0xffu);
  }
}

static void CompressMipToBc(const uint8_t *rgba, uint32_t width,
                            uint32_t height, DXGI_FORMAT format,
                            std::vector<uint8_t> &out) {
  const uint32_t blocksX = (width + 3u) / 4u;
  const uint32_t blocksY = (height + 3u) / 4u;
  const size_t blockBytes = TextureBlockBytes(format);
  out.resize(static_cast<size_t>(blocksX) * blocksY * blockBytes);

  uint8_t block[64] = {};
  uint8_t channel[16] = {};
  for (uint32_t by = 0; by < blocksY; ++by) {
    for (uint32_t bx = 0; bx < blocksX; ++bx) {
      FillRgbaBlock(rgba, width, height, bx, by, block);
      uint8_t *dst = out.data() + (static_cast<size_t>(by) * blocksX + bx) * blockBytes;
      if (format == DXGI_FORMAT_BC1_UNORM) {
        stb_compress_dxt_block(dst, block, 0, STB_DXT_HIGHQUAL);
      } else if (format == DXGI_FORMAT_BC3_UNORM) {
        stb_compress_dxt_block(dst, block, 1, STB_DXT_HIGHQUAL);
      } else if (format == DXGI_FORMAT_BC4_UNORM) {
        for (int i = 0; i < 16; ++i) {
          channel[i] = block[i * 4 + 0];
        }
        EncodeBc4Block(channel, dst);
      }
    }
  }
}

static DXGI_FORMAT ChooseCompressedFormat(TextureUsageSemantic semantic,
                                          TextureCompressionMode mode,
                                          bool hasAlpha) {
  if (mode == TextureCompressionMode::Off) {
    return DXGI_FORMAT_UNKNOWN;
  }
  switch (semantic) {
  case TextureUsageSemantic::Scalar:
    return DXGI_FORMAT_BC4_UNORM;
  case TextureUsageSemantic::ColorAlpha:
  case TextureUsageSemantic::Normal:
  case TextureUsageSemantic::PackedSurface:
    return DXGI_FORMAT_BC3_UNORM;
  case TextureUsageSemantic::Color:
  case TextureUsageSemantic::Emissive:
  case TextureUsageSemantic::Unknown:
    if (hasAlpha || mode == TextureCompressionMode::HighQuality) {
      return DXGI_FORMAT_BC3_UNORM;
    }
    return DXGI_FORMAT_BC1_UNORM;
  default:
    return DXGI_FORMAT_UNKNOWN;
  }
}

bool ApplyTextureCompressionForUsage(Texture &texture,
                                     TextureUsageSemantic semantic) {
  texture.usageSemantic = semantic;

  if (!s_device || texture.width == 0 || texture.height == 0 ||
      texture.cpuData.empty()) {
    return false;
  }

  if (semantic == TextureUsageSemantic::Hdr ||
      texture.cpuFormat == DXGI_FORMAT_R32G32B32A32_FLOAT ||
      IsBlockCompressedTextureFormat(texture.cpuFormat)) {
    texture.compressionMode = TextureCompressionMode::Off;
    texture.gpuCompressed = IsBlockCompressedTextureFormat(texture.format);
    return false;
  }

  if (!TextureCpuDataIsRgba8(texture)) {
    return false;
  }

  if (s_textureCompressionMode == TextureCompressionMode::Off) {
    texture.compressionMode = TextureCompressionMode::Off;
    const uint32_t expectedMipLevels =
        ComputeMipLevels(texture.width, texture.height);
    if (texture.gpuCompressed || texture.format != texture.cpuFormat ||
        texture.mipLevels != expectedMipLevels) {
      Texture uncompressed = LoadTextureFromMemory(
          texture.cpuData.data(), static_cast<int>(texture.width),
          static_cast<int>(texture.height), texture.cpuFormat);
      if (uncompressed.resource) {
        texture.resource = std::move(uncompressed.resource);
        texture.format = texture.cpuFormat;
        texture.mipLevels = 1;
        texture.gpuCompressed = false;
        return true;
      }
    }
    texture.gpuCompressed = false;
    return false;
  }

  const uint8_t *base = texture.cpuData.data();
  const bool hasAlpha = semantic == TextureUsageSemantic::ColorAlpha ||
                        TextureHasUsefulAlpha(base, texture.width,
                                              texture.height);
  const DXGI_FORMAT compressedFormat =
      ChooseCompressedFormat(semantic, s_textureCompressionMode, hasAlpha);
  if (compressedFormat == DXGI_FORMAT_UNKNOWN) {
    return false;
  }

  uint32_t expectedMipLevels = 1;
  for (uint32_t w = texture.width, h = texture.height;
       w > 1 || h > 1;
       w = std::max(1u, w >> 1), h = std::max(1u, h >> 1)) {
    ++expectedMipLevels;
  }
  if (texture.gpuCompressed && texture.format == compressedFormat &&
      texture.mipLevels == expectedMipLevels) {
    texture.compressionMode = s_textureCompressionMode;
    return false;
  }

  std::vector<uint8_t> compressedMipChain;
  std::vector<uint8_t> current(base, base + static_cast<size_t>(texture.width) *
                                             texture.height * 4);
  uint32_t width = texture.width;
  uint32_t height = texture.height;
  uint32_t mipLevels = 0;
  while (true) {
    std::vector<uint8_t> compressedMip;
    CompressMipToBc(current.data(), width, height, compressedFormat,
                    compressedMip);
    compressedMipChain.insert(compressedMipChain.end(), compressedMip.begin(),
                              compressedMip.end());
    ++mipLevels;
    if (width == 1 && height == 1) {
      break;
    }
    std::vector<uint8_t> next;
    uint32_t nextW = 1;
    uint32_t nextH = 1;
    DownsampleRgba2x(current, width, height, next, nextW, nextH);
    current = std::move(next);
    width = nextW;
    height = nextH;
  }

  Texture compressed = LoadTextureFromMemoryMipChain(
      compressedMipChain.data(), compressedMipChain.size(),
      static_cast<int>(texture.width), static_cast<int>(texture.height),
      compressedFormat, mipLevels);
  if (!compressed.resource) {
    return false;
  }

  texture.resource = std::move(compressed.resource);
  texture.format = compressedFormat;
  texture.mipLevels = compressed.mipLevels;
  texture.compressionMode = s_textureCompressionMode;
  texture.gpuCompressed = true;
  return true;
}

GpuMesh LoadMeshFromMemory(const std::vector<Vertex> &vertices,
                           const std::vector<uint32_t> &indices) {
  GpuMesh mesh;
  mesh.vertexCount = (UINT)vertices.size();
  mesh.indexCount = (UINT)indices.size();
  mesh.cpuVertices = vertices;
  mesh.cpuIndices = indices;

  if (vertices.empty() || indices.empty()) {
    // Return a valid but "empty" mesh instead of trying to create 0-length
    // D3D12 buffers
    return mesh;
  }

  if (!s_deferGpuUpload) {
    ComPtr<ID3D12Resource> vUpload, iUpload;
    CreateDefaultBuffer(vertices.data(), vertices.size() * sizeof(Vertex),
                        mesh.vertexBuffer, vUpload,
                        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    CreateDefaultBuffer(indices.data(), indices.size() * sizeof(uint32_t),
                        mesh.indexBuffer, iUpload,
                        D3D12_RESOURCE_STATE_INDEX_BUFFER);

    mesh.vbView.BufferLocation = mesh.vertexBuffer->GetGPUVirtualAddress();
    mesh.vbView.StrideInBytes = sizeof(Vertex);
    mesh.vbView.SizeInBytes = (UINT)(vertices.size() * sizeof(Vertex));

    mesh.ibView.BufferLocation = mesh.indexBuffer->GetGPUVirtualAddress();
    mesh.ibView.Format = DXGI_FORMAT_R32_UINT;
    mesh.ibView.SizeInBytes = (UINT)(indices.size() * sizeof(uint32_t));
  }

  // Calculate Bounds
  mesh.minBound[0] = mesh.minBound[1] = mesh.minBound[2] = 1e30f;
  mesh.maxBound[0] = mesh.maxBound[1] = mesh.maxBound[2] = -1e30f;
  for (const auto &v : vertices) {
    for (int i = 0; i < 3; ++i) {
      mesh.minBound[i] = std::min(mesh.minBound[i], v.pos[i]);
      mesh.maxBound[i] = std::max(mesh.maxBound[i], v.pos[i]);
    }
  }

  return mesh;
}

void UploadMeshes(std::vector<GpuMesh> &meshes) {
  if (!s_device || !s_queue || meshes.empty()) {
    return;
  }

  std::vector<size_t> pendingMeshIndices;
  pendingMeshIndices.reserve(meshes.size());
  for (size_t meshIndex = 0; meshIndex < meshes.size(); ++meshIndex) {
    const GpuMesh &mesh = meshes[meshIndex];
    if (mesh.vertexBuffer && mesh.indexBuffer) {
      continue;
    }
    if (mesh.cpuVertices.empty() || mesh.cpuIndices.empty()) {
      continue;
    }
    pendingMeshIndices.push_back(meshIndex);
  }

  if (pendingMeshIndices.empty()) {
    return;
  }

  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> cmdList;
  ThrowIfFailed(s_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                 IID_PPV_ARGS(&allocator)));
  ThrowIfFailed(s_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                            allocator.Get(), nullptr,
                                            IID_PPV_ARGS(&cmdList)));

  std::vector<ComPtr<ID3D12Resource>> uploadBuffers;
  uploadBuffers.reserve(pendingMeshIndices.size() * 2);

  for (size_t meshIndex : pendingMeshIndices) {
    GpuMesh &mesh = meshes[meshIndex];
    ComPtr<ID3D12Resource> vUpload;
    ComPtr<ID3D12Resource> iUpload;

    CreateDefaultBufferQueued(mesh.cpuVertices.data(),
                              mesh.cpuVertices.size() * sizeof(Vertex),
                              cmdList.Get(), mesh.vertexBuffer, vUpload,
                              D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    CreateDefaultBufferQueued(mesh.cpuIndices.data(),
                              mesh.cpuIndices.size() * sizeof(uint32_t),
                              cmdList.Get(), mesh.indexBuffer, iUpload,
                              D3D12_RESOURCE_STATE_INDEX_BUFFER);

    uploadBuffers.push_back(vUpload);
    uploadBuffers.push_back(iUpload);

    mesh.vbView.BufferLocation = mesh.vertexBuffer->GetGPUVirtualAddress();
    mesh.vbView.StrideInBytes = sizeof(Vertex);
    mesh.vbView.SizeInBytes =
        static_cast<UINT>(mesh.cpuVertices.size() * sizeof(Vertex));

    mesh.ibView.BufferLocation = mesh.indexBuffer->GetGPUVirtualAddress();
    mesh.ibView.Format = DXGI_FORMAT_R32_UINT;
    mesh.ibView.SizeInBytes =
        static_cast<UINT>(mesh.cpuIndices.size() * sizeof(uint32_t));
  }

  ExecuteCommandListAndWait(cmdList.Get());
}

inline float lerp(float a, float b, float t) { return a + t * (b - a); }

Texture LoadIES(const std::string &path) {
  std::ifstream file(path);
  if (!file.is_open())
    return {};

  std::string line;
  if (!std::getline(file, line))
    return {};
  // Skip to data
  while (std::getline(file, line)) {
    if (line.find("TILT=") != std::string::npos)
      break;
  }
  if (line.find("TILT=NONE") == std::string::npos) {
    // If there is tilt data, skip the 4 lines of tilt info
    for (int i = 0; i < 4; ++i)
      std::getline(file, line);
  }

  int numLamps, numVerticalAngles, numHorizontalAngles, phototype, unitType;
  float lumens, multiplier, width, length, height;

  file >> numLamps >> lumens >> multiplier >> numVerticalAngles >>
      numHorizontalAngles >> phototype >> unitType >> width >> length >> height;

  float ballMult, volt, watts;
  file >> ballMult >> volt >> watts;

  std::vector<float> verticalAngles(numVerticalAngles);
  for (int i = 0; i < numVerticalAngles; ++i)
    file >> verticalAngles[i];

  std::vector<float> horizontalAngles(numHorizontalAngles);
  for (int i = 0; i < numHorizontalAngles; ++i)
    file >> horizontalAngles[i];

  std::vector<float> candelaValues(numVerticalAngles * numHorizontalAngles);
  for (int i = 0; i < numVerticalAngles * numHorizontalAngles; ++i) {
    file >> candelaValues[i];
  }

  const int texW = 256;
  const int texH = 256;
  std::vector<float> bakedData(texW * texH * 4, 0.0f);

  for (int y = 0; y < texH; ++y) {
    float theta = (float)y / (float)(texH - 1) * 180.0f;
    for (int x = 0; x < texW; ++x) {
      float phi = (float)x / (float)(texW - 1) * 360.0f;

      // Wrap phi if needed (IES often stores 0-90 or 0-180 and assumes
      // symmetry)
      float lookPhi = phi;
      if (numHorizontalAngles > 1) {
        float maxH = horizontalAngles.back();
        if (maxH == 90.0f) {
          // Quadrant symmetry
          lookPhi = fmodf(phi, 90.0f);
          if (((int)(phi / 90.0f) % 2) == 1)
            lookPhi = 90.0f - lookPhi;
        } else if (maxH == 180.0f) {
          // Half symmetry
          lookPhi = fmodf(phi, 180.0f);
          if (((int)(phi / 180.0f) % 2) == 1)
            lookPhi = 180.0f - lookPhi;
        }
      } else {
        lookPhi = 0.0f;
      }

      // Linear interpolation for vertical
      int v0 = 0;
      while (v0 < numVerticalAngles - 2 && verticalAngles[v0 + 1] < theta)
        v0++;
      int v1 = std::min(v0 + 1, numVerticalAngles - 1);
      float vLerp = (theta - verticalAngles[v0]) /
                    std::max(1e-5f, verticalAngles[v1] - verticalAngles[v0]);
      vLerp = std::clamp(vLerp, 0.0f, 1.0f);

      // Linear interpolation for horizontal
      int h0 = 0;
      while (h0 < numHorizontalAngles - 2 && horizontalAngles[h0 + 1] < lookPhi)
        h0++;
      int h1 = std::min(h0 + 1, numHorizontalAngles - 1);
      float hLerp =
          (lookPhi - horizontalAngles[h0]) /
          std::max(1e-5f, horizontalAngles[h1] - horizontalAngles[h0]);
      hLerp = std::clamp(hLerp, 0.0f, 1.0f);

      float c00 = candelaValues[h0 * numVerticalAngles + v0];
      float c01 = candelaValues[h0 * numVerticalAngles + v1];
      float c10 = candelaValues[h1 * numVerticalAngles + v0];
      float c11 = candelaValues[h1 * numVerticalAngles + v1];

      float val = lerp(lerp(c00, c01, vLerp), lerp(c10, c11, vLerp), hLerp) *
                  multiplier;

      int pixelIdx = (y * texW + x) * 4;
      bakedData[pixelIdx + 0] = val;
      bakedData[pixelIdx + 1] = val;
      bakedData[pixelIdx + 2] = val;
      bakedData[pixelIdx + 3] = 1.0f;
    }
  }

  return LoadTextureFromMemory(bakedData.data(), texW, texH,
                               DXGI_FORMAT_R32G32B32A32_FLOAT);
}

} // namespace Asset
