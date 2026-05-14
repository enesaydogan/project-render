#include "clouds.h"
#include "dxr_helpers.h" // For Align, etc.
#include "dxc_wrapper.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <string>
#include <vector>
#include <thread>

using namespace DirectX;

// Improved Perlin-Gradient Noise Generation
namespace {
// Hash function
int Hash(int n) {
  n = (n << 13) ^ n;
  return (n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff;
}

// helper lerp is not standard in <cmath> or <algorithm> for float before C++20
float lerp(float a, float b, float t) { return a + t * (b - a); }

float GradientNoise(float x, float y, float z) {
  int X = (int)floor(x);
  int Y = (int)floor(y);
  int Z = (int)floor(z);

  x -= floor(x);
  y -= floor(y);
  z -= floor(z);

  float u = x * x * x * (x * (x * 6.0f - 15.0f) + 10.0f);
  float v = y * y * y * (y * (y * 6.0f - 15.0f) + 10.0f);
  float w = z * z * z * (z * (z * 6.0f - 15.0f) + 10.0f);

  auto grad = [](int hash, float x, float y, float z) {
    int h = hash & 15;
    float u = h < 8 ? x : y;
    float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
  };

  auto getHash = [](int x, int y, int z) {
    int h = x * 374761393 + y * 668265263 + z * 1274126177;
    h = (h ^ (h >> 13)) * 1274126177;
    return h ^ (h >> 16);
  };

  return lerp(
      lerp(lerp(grad(getHash(X, Y, Z), x, y, z),
                grad(getHash(X + 1, Y, Z), x - 1, y, z), u),
           lerp(grad(getHash(X, Y + 1, Z), x, y - 1, z),
                grad(getHash(X + 1, Y + 1, Z), x - 1, y - 1, z), u),
           v),
      lerp(lerp(grad(getHash(X, Y, Z + 1), x, y, z - 1),
                grad(getHash(X + 1, Y, Z + 1), x - 1, y, z - 1), u),
           lerp(grad(getHash(X, Y + 1, Z + 1), x, y - 1, z - 1),
                grad(getHash(X + 1, Y + 1, Z + 1), x - 1, y - 1, z - 1), u),
           v),
      w);
}

// Tiled version for seamless textures
float GradientNoiseTiled(float x, float y, float z, int tile) {
  int X = (int)floor(x);
  int Y = (int)floor(y);
  int Z = (int)floor(z);

  x -= floor(x);
  y -= floor(y);
  z -= floor(z);

  float u = x * x * x * (x * (x * 6.0f - 15.0f) + 10.0f);
  float v = y * y * y * (y * (y * 6.0f - 15.0f) + 10.0f);
  float w = z * z * z * (z * (z * 6.0f - 15.0f) + 10.0f);

  auto grad = [](int hash, float x, float y, float z) {
    int h = hash & 15;
    float u = h < 8 ? x : y;
    float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
  };

  auto getHash = [tile](int x, int y, int z) {
    // Check negatives too to be safe, though inputs are positive here
    x = ((x % tile) + tile) % tile;
    y = ((y % tile) + tile) % tile;
    z = ((z % tile) + tile) % tile;

    int h = x * 374761393 + y * 668265263 + z * 1274126177;
    h = (h ^ (h >> 13)) * 1274126177;
    return h ^ (h >> 16);
  };

  return lerp(
      lerp(lerp(grad(getHash(X, Y, Z), x, y, z),
                grad(getHash(X + 1, Y, Z), x - 1, y, z), u),
           lerp(grad(getHash(X, Y + 1, Z), x, y - 1, z),
                grad(getHash(X + 1, Y + 1, Z), x - 1, y - 1, z), u),
           v),
      lerp(lerp(grad(getHash(X, Y, Z + 1), x, y, z - 1),
                grad(getHash(X + 1, Y, Z + 1), x - 1, y, z - 1), u),
           lerp(grad(getHash(X, Y + 1, Z + 1), x, y - 1, z - 1),
                grad(getHash(X + 1, Y + 1, Z + 1), x - 1, y - 1, z - 1), u),
           v),
      w);
}

static inline uint32_t HashU32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352d;
  x ^= x >> 15;
  x *= 0x846ca68b;
  x ^= x >> 16;
  return x;
}

static inline uint32_t Hash3i(int x, int y, int z, uint32_t seed) {
  uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^
               (uint32_t)z * 83492791u ^ seed;
  return HashU32(h);
}

static inline float U32To01(uint32_t h) {
  // 24-bit mantissa
  return (float)(h & 0x00FFFFFFu) / (float)0x01000000u;
}

// Tileable 3D Worley F1 (nearest feature) in [0..1], where 1 is cell
// center-ish.
float WorleyF1(float x, float y, float z, int cellCount, uint32_t seed) {
  // Position in [0..cellCount)
  float fx = x * (float)cellCount;
  float fy = y * (float)cellCount;
  float fz = z * (float)cellCount;

  int ix = (int)floorf(fx);
  int iy = (int)floorf(fy);
  int iz = (int)floorf(fz);

  float px = fx;
  float py = fy;
  float pz = fz;

  float minDist2 = 1e30f;

  for (int dz = -1; dz <= 1; ++dz) {
    for (int dy = -1; dy <= 1; ++dy) {
      for (int dx = -1; dx <= 1; ++dx) {
        int cx = ix + dx;
        int cy = iy + dy;
        int cz = iz + dz;

        // Wrap for tiling
        int wx = ((cx % cellCount) + cellCount) % cellCount;
        int wy = ((cy % cellCount) + cellCount) % cellCount;
        int wz = ((cz % cellCount) + cellCount) % cellCount;

        uint32_t h0 = Hash3i(wx, wy, wz, seed);
        uint32_t h1 = HashU32(h0 ^ 0xA53A5F1Du);
        uint32_t h2 = HashU32(h0 ^ 0xC3E2D1B1u);

        float rx = (float)wx + U32To01(h0);
        float ry = (float)wy + U32To01(h1);
        float rz = (float)wz + U32To01(h2);

        // If neighbor cell was wrapped, shift feature point accordingly
        float sx = rx;
        float sy = ry;
        float sz = rz;
        if (cx < 0)
          sx -= (float)cellCount;
        if (cx >= cellCount)
          sx += (float)cellCount;
        if (cy < 0)
          sy -= (float)cellCount;
        if (cy >= cellCount)
          sy += (float)cellCount;
        if (cz < 0)
          sz -= (float)cellCount;
        if (cz >= cellCount)
          sz += (float)cellCount;

        float dxp = sx - px;
        float dyp = sy - py;
        float dzp = sz - pz;
        float d2 = dxp * dxp + dyp * dyp + dzp * dzp;
        minDist2 = (std::min)(minDist2, d2);
      }
    }
  }

  float minDist = sqrtf(minDist2);
  // Normalize: maximum possible within a cell neighborhood is ~sqrt(3)
  float norm = (float)cellCount / 1.73205080757f;
  float d = (std::min)(1.0f, minDist * norm);
  return 1.0f - d;
}

float WorleyFBM(float x, float y, float z, int c0, int c1, int c2,
                uint32_t seed) {
  float w0 = WorleyF1(x, y, z, c0, seed);
  float w1 = WorleyF1(x, y, z, c1, seed ^ 0x9E3779B9u);
  float w2 = WorleyF1(x, y, z, c2, seed ^ 0xB5297A4Du);
  // Weighted sum (classic Schneider-style)
  return (w0 * 0.625f + w1 * 0.25f + w2 * 0.125f);
}

std::vector<std::vector<UINT8>>
BuildRgba8MipChain3D(const std::vector<UINT8> &base, UINT width, UINT height,
                     UINT depth) {
  std::vector<std::vector<UINT8>> mips;
  mips.push_back(base);

  UINT srcW = width;
  UINT srcH = height;
  UINT srcD = depth;
  while (srcW > 1 || srcH > 1 || srcD > 1) {
    const UINT dstW = (std::max)(1u, srcW >> 1);
    const UINT dstH = (std::max)(1u, srcH >> 1);
    const UINT dstD = (std::max)(1u, srcD >> 1);
    const std::vector<UINT8> &src = mips.back();
    std::vector<UINT8> dst((size_t)dstW * dstH * dstD * 4);

    for (UINT z = 0; z < dstD; ++z) {
      for (UINT y = 0; y < dstH; ++y) {
        for (UINT x = 0; x < dstW; ++x) {
          UINT sum[4] = {0, 0, 0, 0};
          UINT count = 0;
          for (UINT dz = 0; dz < 2; ++dz) {
            const UINT sz = (std::min)(srcD - 1, z * 2 + dz);
            for (UINT dy = 0; dy < 2; ++dy) {
              const UINT sy = (std::min)(srcH - 1, y * 2 + dy);
              for (UINT dx = 0; dx < 2; ++dx) {
                const UINT sx = (std::min)(srcW - 1, x * 2 + dx);
                const size_t srcIdx =
                    ((size_t)sz * srcW * srcH + (size_t)sy * srcW + sx) * 4;
                sum[0] += src[srcIdx + 0];
                sum[1] += src[srcIdx + 1];
                sum[2] += src[srcIdx + 2];
                sum[3] += src[srcIdx + 3];
                ++count;
              }
            }
          }

          const size_t dstIdx =
              ((size_t)z * dstW * dstH + (size_t)y * dstW + x) * 4;
          dst[dstIdx + 0] = (UINT8)((sum[0] + count / 2) / count);
          dst[dstIdx + 1] = (UINT8)((sum[1] + count / 2) / count);
          dst[dstIdx + 2] = (UINT8)((sum[2] + count / 2) / count);
          dst[dstIdx + 3] = (UINT8)((sum[3] + count / 2) / count);
        }
      }
    }

    mips.push_back(std::move(dst));
    srcW = dstW;
    srcH = dstH;
    srcD = dstD;
  }

  return mips;
}

CloudParams MakeDefaultCloudParams() {
  CloudParams p = {};

  // Baked-cloud quality defaults (higher fidelity than old real-time profile).
  p.density = 1.05f;
  p.absorption = 0.22f;
  p.coverage = 0.46f;
  p.scattering = 0.74f;
  p.steps = 128;
  p.sunIntensity = 1.10f;
  p.cloudTop = 5200.0f;
  p.cloudBottom = 2400.0f;
  p.windSpeed = 0.012f;

  p.baseScale = 0.00018f;
  p.detailScale = 0.00220f;
  p.coverageScale = 0.000080f;
  p.coverageVariation = 0.62f;
  p.erosion = 0.48f;
  p.warpStrength = 0.62f;
  p.shapePower = 1.22f;
  p.powderStrength = 0.90f;
  p.cirrusAmount = 0.06f;
  p.cloudShadowStrength = 0.34f;
  p._pad0 = 0.0f;
  p._pad1 = 0.0f;

  p.shadowSteps = 16;
  p.shadowStepSize = 180.0f;
  p.shadowLod = 1.0f;

  p.maxSteps = 320;
  p.verticalStepMeters = 18.0f;
  p.shadowEvery = 2;
  p.shadowDensityThreshold = 0.025f;

  p.timeSeconds = 0.0f;
  p.previewBakeSamples = 1;
  p.finalBakeSamples = 4;
  p.bakeJitterStrength = 0.85f;
  p.multiScatterBoost = 0.55f;
  p.silverLiningStrength = 0.80f;
  p.cloudType = 0.18f;
  p.groundBounceStrength = 0.85f;
  p.shadowSoftness = 0.45f;
  p._pad = {0, 0, 0};
  return p;
}
} // namespace

extern DescriptorHeapAllocator g_cbvSrvAllocator; // allocator from main.cpp

void CloudManager::Initialize(ID3D12Device *device,
                              ID3D12GraphicsCommandList *cmdList) {
  if (m_initialized)
    return;

  m_params = MakeDefaultCloudParams();

  CreateConstantBuffers(device);
  CreateTextures(device, cmdList);

  // Create persistent descriptors (CBV + BaseTex SRV + DetailTex SRV + BakedSky SRV)
  CreateDescriptors(device);

  // Mark first bake requested so baked-sky is populated at startup. Start with
  // the preview texture; the full 4K bake is scheduled after that so DXR scene
  // loads never inject a large cloud job into the first active path-trace frame.
  m_lastObservedParams = m_params;
  m_lastBakedParams = m_params;
  m_bakeRequested = true;
  m_bakeInProgress = false;
  m_bakeNextRow = 0;
  m_pendingParamBake = true;
  m_pendingPreviewBake = true;
  m_pendingFinalBake = true;
  m_requestedBakeQuality = BakeQuality::Preview;
  m_activeBakedSkyTexture = BakedSkyTexture::Final;
  m_secondsSinceParamEdit = 0.0f;
  m_secondsSinceBake = 0.0f;

  m_initialized = true;
}

ID3D12Resource *CloudManager::GetBakedSkyTexture() const {
  return m_activeBakedSkyTexture == BakedSkyTexture::Preview
             ? m_previewBakedSkyTexture.Get()
             : m_finalBakedSkyTexture.Get();
}

void CloudManager::RequestBake() {
  m_bakeRequested = true;
  m_bakeInProgress = false;
  m_bakeNextRow = 0;
  m_pendingParamBake = true;
  m_pendingPreviewBake = true;
  m_pendingFinalBake = true;
  m_requestedBakeQuality = BakeQuality::Final;
  m_secondsSinceParamEdit = 0.0f;
}

void CloudManager::RequestPreviewBake() {
  m_bakeRequested = true;
  m_bakeInProgress = false;
  m_bakeNextRow = 0;
  m_pendingParamBake = true;
  m_pendingPreviewBake = true;
  m_pendingFinalBake = true;
  m_requestedBakeQuality = BakeQuality::Preview;
  m_secondsSinceParamEdit = 0.0f;
}

void CloudManager::CreateDescriptors(ID3D12Device *device) {
  // Allocate 4 contiguous persistent descriptors: CBV, Base SRV, Detail SRV, BakedSky SRV
  DescriptorAllocation alloc = g_cbvSrvAllocator.AllocatePersistent(4);
  m_cpuHandle = alloc.cpu;
  m_gpuHandle = alloc.gpu;

  UINT descSize = device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  // 1) CBV (b10, space2)
  D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
  cbvDesc.BufferLocation =
      m_constantBuffers[0]
          ->GetGPUVirtualAddress(); // Descriptor points to frame 0 initially
  cbvDesc.SizeInBytes = (sizeof(CloudParams) + 255) & ~255;
  device->CreateConstantBufferView(&cbvDesc, m_cpuHandle);

  // 2) Base Texture SRV (t10, space2)
  D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = m_cpuHandle;
  srvCpu.ptr += (SIZE_T)descSize * 1;

  D3D12_RESOURCE_DESC noiseDesc = m_baseTexture->GetDesc();
  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  srvDesc.Format = noiseDesc.Format;
  srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
  srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srvDesc.Texture3D.MipLevels = noiseDesc.MipLevels;
  srvDesc.Texture3D.MostDetailedMip = 0;
  srvDesc.Texture3D.ResourceMinLODClamp = 0.0f;
  device->CreateShaderResourceView(m_baseTexture.Get(), &srvDesc, srvCpu);

  // 3) Detail Texture SRV (t11, space2)
  D3D12_CPU_DESCRIPTOR_HANDLE srvCpuDetail = m_cpuHandle;
  srvCpuDetail.ptr += (SIZE_T)descSize * 2;

  D3D12_RESOURCE_DESC detailDesc = m_detailTexture->GetDesc();
  D3D12_SHADER_RESOURCE_VIEW_DESC srvDescDetail = {};
  srvDescDetail.Format = detailDesc.Format;
  srvDescDetail.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
  srvDescDetail.Shader4ComponentMapping =
      D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srvDescDetail.Texture3D.MipLevels = detailDesc.MipLevels;
  srvDescDetail.Texture3D.MostDetailedMip = 0;
  srvDescDetail.Texture3D.ResourceMinLODClamp = 0.0f;
  device->CreateShaderResourceView(m_detailTexture.Get(), &srvDescDetail,
                                   srvCpuDetail);

  // 4) Baked Sky SRV (t12, space2). This descriptor is repointed after each
  // preview/final bake so raster and DXR sample the newest cloud bake.
  UpdateBakedSkySRV(device, GetBakedSkyTexture());

  // Allocate persistent UAV descriptors used by the bake CS.
  DescriptorAllocation uavAlloc = g_cbvSrvAllocator.AllocatePersistent(2);
  m_previewBakedSkyUAVCpuHandle = uavAlloc.cpu;
  m_previewBakedSkyUAVGpuHandle = uavAlloc.gpu;
  m_finalBakedSkyUAVCpuHandle = uavAlloc.cpu;
  m_finalBakedSkyUAVGpuHandle = uavAlloc.gpu;
  m_finalBakedSkyUAVCpuHandle.ptr += (SIZE_T)descSize;
  m_finalBakedSkyUAVGpuHandle.ptr += (UINT64)descSize;

  D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
  uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
  uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  uavDesc.Texture2D.MipSlice = 0;
  device->CreateUnorderedAccessView(m_previewBakedSkyTexture.Get(), nullptr,
                                    &uavDesc,
                                    m_previewBakedSkyUAVCpuHandle);
  device->CreateUnorderedAccessView(m_finalBakedSkyTexture.Get(), nullptr,
                                    &uavDesc,
                                    m_finalBakedSkyUAVCpuHandle);
}

void CloudManager::UpdateBakedSkySRV(ID3D12Device *device,
                                     ID3D12Resource *resource) {
  if (!device || !resource || m_cpuHandle.ptr == 0) {
    return;
  }

  const UINT descSize = device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  D3D12_CPU_DESCRIPTOR_HANDLE srvCpuBaked = m_cpuHandle;
  srvCpuBaked.ptr += (SIZE_T)descSize * 3;

  D3D12_SHADER_RESOURCE_VIEW_DESC srvDescBaked = {};
  srvDescBaked.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
  srvDescBaked.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srvDescBaked.Texture2D.MipLevels = 1;
  srvDescBaked.Texture2D.MostDetailedMip = 0;
  srvDescBaked.Shader4ComponentMapping =
      D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  device->CreateShaderResourceView(resource, &srvDescBaked, srvCpuBaked);
}

void CloudManager::ResetToDefaults() {
  m_params = MakeDefaultCloudParams();
  if (m_initialized) {
    UpdateConstantBuffer();
  }
}

void CloudManager::Update(float dt, UINT frameIndex) {
  if (!m_initialized)
    return;

  const float safeDt = (std::max)(dt, 0.0f);

  // Advance animation time
  m_params.timeSeconds += safeDt;
  m_secondsSinceBake += safeDt;
  m_secondsSinceParamEdit += safeDt;

  // Detect parameter changes (excluding time) and schedule a trailing-edge bake
  // to avoid rebaking every frame while dragging UI sliders.
  CloudParams a = m_params;
  CloudParams b = m_lastObservedParams;
  a.timeSeconds = 0.0f;
  b.timeSeconds = 0.0f;
  if (memcmp(&a, &b, sizeof(CloudParams)) != 0) {
    m_lastObservedParams = m_params;
    m_lastObservedParams.timeSeconds = 0.0f;
    m_bakeInProgress = false;
    m_bakeNextRow = 0;
    m_pendingParamBake = true;
    m_pendingPreviewBake = true;
    m_pendingFinalBake = true;
    m_secondsSinceParamEdit = 0.0f;
  }

  static constexpr float kPreviewBakeDebounceSeconds = 0.06f;
  static constexpr float kPreviewBakeMinIntervalSeconds = 0.14f;
  static constexpr float kFinalBakeDebounceSeconds = 0.75f;
  static constexpr float kFinalBakeMinIntervalSeconds = 0.25f;
  if (m_pendingParamBake && !m_bakeRequested) {
    if (m_pendingFinalBake &&
        m_secondsSinceParamEdit >= kFinalBakeDebounceSeconds &&
        m_secondsSinceBake >= kFinalBakeMinIntervalSeconds) {
      m_bakeRequested = true;
      m_requestedBakeQuality = BakeQuality::Final;
    } else if (m_pendingPreviewBake &&
               m_secondsSinceParamEdit >= kPreviewBakeDebounceSeconds &&
               m_secondsSinceBake >= kPreviewBakeMinIntervalSeconds) {
      m_bakeRequested = true;
      m_requestedBakeQuality = BakeQuality::Preview;
    }
  }

  m_currentFrame = frameIndex % 3;
  UpdateConstantBuffer();
}

void CloudManager::CreateConstantBuffers(ID3D12Device *device) {
  UINT size = (sizeof(CloudParams) + 255) & ~255;

  D3D12_HEAP_PROPERTIES heapProps = {};
  heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

  D3D12_RESOURCE_DESC bufDesc = {};
  bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  bufDesc.Width = size;
  bufDesc.Height = 1;
  bufDesc.DepthOrArraySize = 1;
  bufDesc.MipLevels = 1;
  bufDesc.Format = DXGI_FORMAT_UNKNOWN;
  bufDesc.SampleDesc.Count = 1;
  bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  bufDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

  for (int i = 0; i < 3; ++i) {
    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&m_constantBuffers[i])));

    std::wstring name = L"CloudConstantBuffer_" + std::to_wstring(i);
    m_constantBuffers[i]->SetName(name.c_str());

    D3D12_RANGE readRange = {0, 0};
    ThrowIfFailed(m_constantBuffers[i]->Map(
        0, &readRange, reinterpret_cast<void **>(&m_cbMappedData[i])));
  }
}

void CloudManager::UpdateConstantBuffer() {
  if (m_cbMappedData[m_currentFrame]) {
    memcpy(m_cbMappedData[m_currentFrame], &m_params, sizeof(CloudParams));
  }
}

// Compile bake CS + create root signature / PSO (lazy)
static void CreateBakePipelineIfNeeded(ID3D12Device* device,
                                       Microsoft::WRL::ComPtr<ID3D12RootSignature>& outRootSig,
                                       Microsoft::WRL::ComPtr<ID3D12PipelineState>& outPSO) {
  if (outPSO && outRootSig) return;
  if (!device) return;

  // Root signature: b0 = CameraCB, table = (b10 + t10..t12, space2), table u0 = UAV
  // Root parameters:
  // 0: Camera CB (b0)
  // 1: Cloud CB (b10, space2) - root CBV for simplicity
  // 2: SRV table (t10..t12, space2)
  // 3: UAV table (u0)
  // 4: bake row constants (b11, space2): startY, endY
  D3D12_ROOT_PARAMETER params[5] = {};

  // b0 (Camera)
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  params[0].Descriptor.ShaderRegister = 0;
  params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  // b10 (Cloud CB) as root CBV (space2)
  params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  params[1].Descriptor.ShaderRegister = 10;
  params[1].Descriptor.RegisterSpace = 2;
  params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  // SRV table t10..t12 (space2)
  D3D12_DESCRIPTOR_RANGE srvRange = {};
  srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  srvRange.NumDescriptors = 3; // NoiseTex(t10), DetailTex(t11), BakedSky(t12)
  srvRange.BaseShaderRegister = 10;
  srvRange.RegisterSpace = 2;
  srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[2].DescriptorTable.NumDescriptorRanges = 1;
  params[2].DescriptorTable.pDescriptorRanges = &srvRange;
  params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  // UAV table (u0)
  D3D12_DESCRIPTOR_RANGE uavRange = {};
  uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  uavRange.NumDescriptors = 1;
  uavRange.BaseShaderRegister = 0;
  uavRange.RegisterSpace = 0;
  uavRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[3].DescriptorTable.NumDescriptorRanges = 1;
  params[3].DescriptorTable.pDescriptorRanges = &uavRange;
  params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  params[4].Constants.ShaderRegister = 11;
  params[4].Constants.RegisterSpace = 2;
  params[4].Constants.Num32BitValues = 4;
  params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  // Add a static linear sampler (binding s0) to match shader's 'linearSampler'
  D3D12_STATIC_SAMPLER_DESC staticSampler = {};
  staticSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  staticSampler.MipLODBias = 0;
  staticSampler.MaxAnisotropy = 1;
  staticSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
  staticSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
  staticSampler.MinLOD = 0.0f;
  staticSampler.MaxLOD = D3D12_FLOAT32_MAX;
  staticSampler.ShaderRegister = 0; // s0
  staticSampler.RegisterSpace = 2; // match LinearWrapSampler : register(s0, space2) in clouds.hlsl
  staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
  rsDesc.NumParameters = _countof(params);
  rsDesc.pParameters = params;
  rsDesc.NumStaticSamplers = 1;
  rsDesc.pStaticSamplers = &staticSampler;
  rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

  Microsoft::WRL::ComPtr<ID3DBlob> sig;
  Microsoft::WRL::ComPtr<ID3DBlob> err;
  HRESULT hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err);
  if (FAILED(hr)) {
    if (err) fprintf(stderr, "CreateBakePipeline: RootSig serialize error: %s\n", (char*)err->GetBufferPointer());
    return;
  }
  ThrowIfFailed(device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&outRootSig)));

  // Compile CS
  DxcHelper localDxc;
  ComPtr<IDxcBlob> cs;
  try {
    cs = localDxc.Compile(L"shaders/sky_bake_cs.hlsl", L"CSMain", L"cs_6_3", {});
  } catch (const std::exception &e) {
    fprintf(stderr, "CreateBakePipeline: CS compile failed: %s\n", e.what());
    return;
  }
  if (!cs) return;

  D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature = outRootSig.Get();
  psoDesc.CS.pShaderBytecode = cs->GetBufferPointer();
  psoDesc.CS.BytecodeLength = cs->GetBufferSize();
  HRESULT hrCreate = device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&outPSO));
  if (FAILED(hrCreate)) {
    fprintf(stderr, "CreateBakePipelineIfNeeded: CreateComputePipelineState failed. HR=0x%08x, CS size=%zu, rootSig=%p\n", (unsigned)hrCreate, (size_t)cs->GetBufferSize(), (void*)psoDesc.pRootSignature);
    return;
  }
}

void CloudManager::BakeSky(ID3D12GraphicsCommandList *cmdList,
                           ID3D12Resource *cameraCB,
                           bool backgroundBudget) {
  ID3D12Resource *targetTexture =
      m_requestedBakeQuality == BakeQuality::Preview
          ? m_previewBakedSkyTexture.Get()
          : m_finalBakedSkyTexture.Get();
  D3D12_GPU_DESCRIPTOR_HANDLE targetUAV =
      m_requestedBakeQuality == BakeQuality::Preview
          ? m_previewBakedSkyUAVGpuHandle
          : m_finalBakedSkyUAVGpuHandle;
  D3D12_CPU_DESCRIPTOR_HANDLE targetUAVCpu =
      m_requestedBakeQuality == BakeQuality::Preview
          ? m_previewBakedSkyUAVCpuHandle
          : m_finalBakedSkyUAVCpuHandle;

  if (!m_initialized || !cmdList || !targetTexture) {
    return;
  }

  // Create bake PSO/root-signature lazily
  ID3D12Device *device = nullptr;
  cmdList->GetDevice(IID_PPV_ARGS(&device));
  Microsoft::WRL::ComPtr<ID3D12RootSignature> localRootSig = m_bakeRootSig;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> localPSO = m_bakePSO;
  CreateBakePipelineIfNeeded(device, localRootSig, localPSO);
  m_bakeRootSig = localRootSig;
  m_bakePSO = localPSO;
  if (!m_bakePSO || !m_bakeRootSig) {
    if (device) device->Release();
    return;
  }

  // Bind descriptor heap (global CBV/SRV/UAV heap)
  ID3D12DescriptorHeap *heaps[] = { g_cbvSrvAllocator.Heap() };
  cmdList->SetDescriptorHeaps(1, heaps);

  // Set compute pipeline & root signature
  cmdList->SetPipelineState(m_bakePSO.Get());
  cmdList->SetComputeRootSignature(m_bakeRootSig.Get());

  // b0 = camera CB (optional)
  if (cameraCB) {
    cmdList->SetComputeRootConstantBufferView(0, cameraCB->GetGPUVirtualAddress());
  }

  // b10 = Cloud CB (root CBV)
  cmdList->SetComputeRootConstantBufferView(1, GetConstantBufferAddr());

  // t10..t12 = SRV descriptor table (skip the first descriptor which is CBV in our persistent block)
  // m_gpuHandle points to the contiguous CBV+SRV descriptors for clouds; advance by one descriptor to reach the SRV start
  {
    ID3D12Device *dev = nullptr;
    cmdList->GetDevice(IID_PPV_ARGS(&dev));
    UINT descSize = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    if (dev) dev->Release();

    D3D12_GPU_DESCRIPTOR_HANDLE srvTable = m_gpuHandle;
    srvTable.ptr += (UINT64)descSize * 1; // move past CBV to first SRV
    cmdList->SetComputeRootDescriptorTable(2, srvTable);
  }

  // Root table (3) = UAV for baked sky
  cmdList->SetComputeRootDescriptorTable(3, targetUAV);

  D3D12_RESOURCE_DESC desc = targetTexture->GetDesc();
  const UINT textureHeight = (UINT)desc.Height;
  const bool startingBake = !m_bakeInProgress || m_bakeNextRow >= textureHeight;
  if (startingBake) {
    m_bakeInProgress = true;
    m_bakeNextRow = 0;
  }

  // Transition baked texture to UAV
  {
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = targetTexture;
    barrier.Transition.StateBefore =
      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrier);
  }

  if (startingBake) {
    FLOAT clearValue[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    cmdList->ClearUnorderedAccessViewFloat(targetUAV, targetUAVCpu,
                                           targetTexture, clearValue, 0,
                                           nullptr);
    D3D12_RESOURCE_BARRIER clearBarrier = {};
    clearBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    clearBarrier.UAV.pResource = targetTexture;
    cmdList->ResourceBarrier(1, &clearBarrier);
  }

  // Dispatch
  const UINT requestedSamples =
      m_requestedBakeQuality == BakeQuality::Preview
          ? (UINT)(std::max)(1, m_params.previewBakeSamples)
          : (UINT)(std::max)(1, m_params.finalBakeSamples);
  const UINT rowsPerSlice =
      m_requestedBakeQuality == BakeQuality::Preview
          ? (backgroundBudget ? 8u : 64u)
          : (std::max)(2u, 64u / (std::min)(requestedSamples, 32u));
  const UINT startY = m_bakeNextRow;
  const UINT endY = (std::min)(textureHeight, startY + rowsPerSlice);
  UINT bakeConstants[4] = {startY, endY, 0u, 0u};
  cmdList->SetComputeRoot32BitConstants(4, 4, bakeConstants, 0);

  const UINT tgX = 16, tgY = 16;
  UINT dispatchX = (UINT)((desc.Width + tgX - 1) / tgX);
  UINT dispatchY = (UINT)(((endY - startY) + tgY - 1) / tgY);
  cmdList->Dispatch(dispatchX, dispatchY, 1);
  m_bakeNextRow = endY;

  // UAV barrier & transition back to SRV for sampling
  D3D12_RESOURCE_BARRIER uavBarrier = {};
  uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  uavBarrier.UAV.pResource = targetTexture;
  cmdList->ResourceBarrier(1, &uavBarrier);

  {
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = targetTexture;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter =
      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrier);
  }

  const bool bakeComplete = m_bakeNextRow >= textureHeight;
  if (!bakeComplete) {
    if (device) device->Release();
    return;
  }

  // Mark baked state
  const bool bakedPreview = m_requestedBakeQuality == BakeQuality::Preview;
  m_activeBakedSkyTexture =
      bakedPreview ? BakedSkyTexture::Preview : BakedSkyTexture::Final;
  UpdateBakedSkySRV(device, targetTexture);

  m_bakeRequested = false;
  m_bakeInProgress = false;
  m_bakeNextRow = 0;
  m_secondsSinceBake = 0.0f;
  m_lastObservedParams = m_params;
  m_lastObservedParams.timeSeconds = 0.0f;
  if (bakedPreview) {
    m_pendingPreviewBake = false;
  } else {
    m_pendingParamBake = false;
    m_pendingPreviewBake = false;
    m_pendingFinalBake = false;
    m_secondsSinceParamEdit = 0.0f;
    m_lastBakedParams = m_params;
  }
  if (device) device->Release();
}

// Helper for remapping
static float Remap(float val, float minVal, float maxVal, float newMin,
                   float newMax) {
  return newMin + (val - minVal) * (newMax - newMin) / (maxVal - minVal);
}

void CloudManager::CreateTextures(ID3D12Device *device,
                                  ID3D12GraphicsCommandList *cmdList) {
  fprintf(stderr, "CloudManager: Generating 1:1 Compliant Noise Textures (Base "
                  "+ Detail)...\n");

  // --- 1. Base Shape Texture (128x128x128 RGBA8) ---
  // R: Perlin-Worley (Base Shape)
  // G: Worley FBM (Freq 1)
  // B: Worley FBM (Freq 2)
  // A: low-frequency warp helper
  {
    const UINT width = 128;
    const UINT height = 128;
    const UINT depth = 128;
    const UINT textureDataSize = width * height * depth * 4;
    std::vector<UINT8> noiseData(textureDataSize);

    const uint32_t worleySeed = 1337u;

    // Parallelize over Z slices to utilize multiple CPU cores at startup.
    unsigned hwThreads = std::thread::hardware_concurrency();
    if (hwThreads == 0) hwThreads = 1;
    unsigned numThreads = (unsigned)std::min<unsigned>(hwThreads, depth);
    unsigned chunk = (depth + numThreads - 1) / numThreads;

    std::vector<std::thread> threads;
    threads.reserve(numThreads);

    for (unsigned t = 0; t < numThreads; ++t) {
      int z0 = (int)std::min<unsigned>(t * chunk, depth);
      int z1 = (int)std::min<unsigned>((t + 1) * chunk, depth);
      threads.emplace_back([=, &noiseData]() {
        for (int z = z0; z < z1; ++z) {
          for (int y = 0; y < (int)height; ++y) {
            for (int x = 0; x < (int)width; ++x) {
              float u = (float)x / (float)width;
              float v = (float)y / (float)height;
              float w = (float)z / (float)depth;

              // Perlin FBM (7 Octaves technically, but 5 is usually enough)
              float perlin = 0.0f;
              float scale = 4.0f; // Base freq
              float amp = 1.0f;
              float maxVal = 0.0f;
              for (int i = 0; i < 5; ++i) {
                float n = GradientNoiseTiled(u * scale, v * scale, w * scale, (int)scale);
                perlin += n * amp;
                maxVal += amp;
                scale *= 2.0f;
                amp *= 0.5f;
              }
              perlin = (perlin / maxVal) * 0.5f + 0.5f; // [0,1]

              // Worley FBMs for Base
              float wf1 = WorleyFBM(u, v, w, 4, 8, 16, worleySeed);
              float wf2 = WorleyFBM(u, v, w, 8, 16, 32, worleySeed ^ 0x12345678);
              float worleyBase = wf1;
              float perlinWorley = Remap(perlin, worleyBase, 1.0f, 0.0f, 1.0f);
              perlinWorley = (std::max)(0.0f, (std::min)(1.0f, perlinWorley));

              float warp = GradientNoiseTiled(u * 2.0f, v * 2.0f + 5.5f, w * 2.0f + 1.2f, 2);
              warp = warp * 0.5f + 0.5f;

              const UINT idx = (UINT)((z * (width * height) + y * width + x) * 4);
              noiseData[idx + 0] = (UINT8)(perlinWorley * 255.0f);
              noiseData[idx + 1] = (UINT8)((std::max)(0.0f, wf1) * 255.0f);
              noiseData[idx + 2] = (UINT8)((std::max)(0.0f, wf2) * 255.0f);
              noiseData[idx + 3] = (UINT8)((std::max)(0.0f, warp) * 255.0f);
            }
          }
        }
      });
    }

    for (auto &th : threads) th.join();

    std::vector<std::vector<UINT8>> mips =
        BuildRgba8MipChain3D(noiseData, width, height, depth);

    // Create Resource
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.DepthOrArraySize = depth;
    texDesc.MipLevels = (UINT16)mips.size();
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    ThrowIfFailed(device->CreateCommittedResource(
        &defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_baseTexture)));
    m_baseTexture->SetName(L"CloudBaseTexture");

    // Upload
    const UINT64 uploadSize =
        GetRequiredIntermediateSize(m_baseTexture.Get(), 0, (UINT)mips.size());
    Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer;
    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = uploadSize;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ThrowIfFailed(device->CreateCommittedResource(
        &uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&uploadBuffer)));

    std::vector<D3D12_SUBRESOURCE_DATA> srcData(mips.size());
    UINT mipW = width;
    UINT mipH = height;
    for (size_t mip = 0; mip < mips.size(); ++mip) {
      srcData[mip].pData = mips[mip].data();
      srcData[mip].RowPitch = (LONG_PTR)mipW * 4;
      srcData[mip].SlicePitch = srcData[mip].RowPitch * mipH;
      mipW = (std::max)(1u, mipW >> 1);
      mipH = (std::max)(1u, mipH >> 1);
    }

    UpdateSubresources(cmdList, m_baseTexture.Get(), uploadBuffer.Get(), 0, 0,
                       (UINT)srcData.size(), srcData.data());

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_baseTexture.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrier);

    m_uploadBuffers.push_back(uploadBuffer);
  }

  // --- 2. Detail Texture (32x32x32 RGBA8) ---
  // R: Worley low
  // G: Worley med
  // B: Worley high
  // A: Unused
  {
    const UINT width = 32;
    const UINT height = 32;
    const UINT depth = 32;
    const UINT textureDataSize = width * height * depth * 4;
    std::vector<UINT8> noiseData(textureDataSize);
    const uint32_t detailSeed = 9999u;

    // Parallelize detail texture generation similarly to base texture.
    {
      unsigned hwThreads = std::thread::hardware_concurrency();
      if (hwThreads == 0) hwThreads = 1;
      unsigned numThreads = (unsigned)std::min<unsigned>(hwThreads, depth);
      unsigned chunk = (depth + numThreads - 1) / numThreads;

      std::vector<std::thread> threads;
      threads.reserve(numThreads);

      for (unsigned t = 0; t < numThreads; ++t) {
        int z0 = (int)std::min<unsigned>(t * chunk, depth);
        int z1 = (int)std::min<unsigned>((t + 1) * chunk, depth);
        threads.emplace_back([=, &noiseData]() {
          for (int z = z0; z < z1; ++z) {
            for (int y = 0; y < (int)height; ++y) {
              for (int x = 0; x < (int)width; ++x) {
                float u = (float)x / (float)width;
                float v = (float)y / (float)height;
                float w = (float)z / (float)depth;

                float d1 = WorleyFBM(u, v, w, 2, 4, 8, detailSeed);
                float d2 = WorleyFBM(u, v, w, 4, 8, 16, detailSeed ^ 0x11223344);
                float d3 = WorleyFBM(u, v, w, 8, 16, 32, detailSeed ^ 0xAABBCCDD);

                const UINT idx = (UINT)((z * (width * height) + y * width + x) * 4);
                noiseData[idx + 0] = (UINT8)((std::max)(0.0f, d1) * 255.0f);
                noiseData[idx + 1] = (UINT8)((std::max)(0.0f, d2) * 255.0f);
                noiseData[idx + 2] = (UINT8)((std::max)(0.0f, d3) * 255.0f);
                noiseData[idx + 3] = 255;
              }
            }
          }
        });
      }

      for (auto &th : threads) th.join();
    }

    std::vector<std::vector<UINT8>> mips =
        BuildRgba8MipChain3D(noiseData, width, height, depth);

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.DepthOrArraySize = depth;
    texDesc.MipLevels = (UINT16)mips.size();
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    ThrowIfFailed(device->CreateCommittedResource(
        &defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&m_detailTexture)));
    m_detailTexture->SetName(L"CloudDetailTexture");

    const UINT64 uploadSize =
        GetRequiredIntermediateSize(m_detailTexture.Get(), 0, (UINT)mips.size());
    Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer;
    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = uploadSize;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ThrowIfFailed(device->CreateCommittedResource(
        &uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&uploadBuffer)));

    std::vector<D3D12_SUBRESOURCE_DATA> srcData(mips.size());
    UINT mipW = width;
    UINT mipH = height;
    for (size_t mip = 0; mip < mips.size(); ++mip) {
      srcData[mip].pData = mips[mip].data();
      srcData[mip].RowPitch = (LONG_PTR)mipW * 4;
      srcData[mip].SlicePitch = srcData[mip].RowPitch * mipH;
      mipW = (std::max)(1u, mipW >> 1);
      mipH = (std::max)(1u, mipH >> 1);
    }

    UpdateSubresources(cmdList, m_detailTexture.Get(), uploadBuffer.Get(), 0, 0,
                       (UINT)srcData.size(), srcData.data());

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_detailTexture.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrier);

    m_uploadBuffers.push_back(uploadBuffer);
  }

  // --- 3. Baked Sky Textures (Latitude-Longitude) ---
  {
    auto createBakedSky = [device](UINT width, UINT height, const wchar_t *name,
                                   Microsoft::WRL::ComPtr<ID3D12Resource> &out) {
      D3D12_RESOURCE_DESC skyDesc = {};
      skyDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
      skyDesc.Width = width;
      skyDesc.Height = height;
      skyDesc.DepthOrArraySize = 1;
      skyDesc.MipLevels = 1;
      skyDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; // HDR enough for clouds
      skyDesc.SampleDesc.Count = 1;
      skyDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
      skyDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

      D3D12_HEAP_PROPERTIES defaultHeap = {};
      defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

      ThrowIfFailed(device->CreateCommittedResource(
          &defaultHeap, D3D12_HEAP_FLAG_NONE, &skyDesc,
          D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
              D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
          nullptr, IID_PPV_ARGS(&out)));
      out->SetName(name);
    };

    createBakedSky(1024, 512, L"PreviewBakedCloudSkyTexture",
                   m_previewBakedSkyTexture);
    createBakedSky(4096, 2048, L"FinalBakedCloudSkyTexture",
                   m_finalBakedSkyTexture);
  }
}

