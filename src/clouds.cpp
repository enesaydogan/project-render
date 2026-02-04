#include "clouds.h"
#include "dxr_helpers.h" // For Align, etc.
#include <d3dx12/d3dx12.h>
#include <vector>
#include <random>
#include <algorithm>
#include <cmath>

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
            lerp(
                lerp(grad(getHash(X,Y,Z), x, y, z), grad(getHash(X+1,Y,Z), x-1, y, z), u),
                lerp(grad(getHash(X,Y+1,Z), x, y-1, z), grad(getHash(X+1,Y+1,Z), x-1, y-1, z), u),
                v
            ),
            lerp(
                lerp(grad(getHash(X,Y,Z+1), x, y, z-1), grad(getHash(X+1,Y,Z+1), x-1, y, z-1), u),
                lerp(grad(getHash(X,Y+1,Z+1), x, y-1, z-1), grad(getHash(X+1,Y+1,Z+1), x-1, y-1, z-1), u),
                v
            ),
            w
        );
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
            lerp(
                lerp(grad(getHash(X,Y,Z), x, y, z), grad(getHash(X+1,Y,Z), x-1, y, z), u),
                lerp(grad(getHash(X,Y+1,Z), x, y-1, z), grad(getHash(X+1,Y+1,Z), x-1, y-1, z), u),
                v
            ),
            lerp(
                lerp(grad(getHash(X,Y,Z+1), x, y, z-1), grad(getHash(X+1,Y,Z+1), x-1, y, z-1), u),
                lerp(grad(getHash(X,Y+1,Z+1), x, y-1, z-1), grad(getHash(X+1,Y+1,Z+1), x-1, y-1, z-1), u),
                v
            ),
            w
        );
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
        uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ (uint32_t)z * 83492791u ^ seed;
        return HashU32(h);
    }

    static inline float U32To01(uint32_t h) {
        // 24-bit mantissa
        return (float)(h & 0x00FFFFFFu) / (float)0x01000000u;
    }

    // Tileable 3D Worley F1 (nearest feature) in [0..1], where 1 is cell center-ish.
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
                    if (cx < 0) sx -= (float)cellCount;
                    if (cx >= cellCount) sx += (float)cellCount;
                    if (cy < 0) sy -= (float)cellCount;
                    if (cy >= cellCount) sy += (float)cellCount;
                    if (cz < 0) sz -= (float)cellCount;
                    if (cz >= cellCount) sz += (float)cellCount;

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

    float WorleyFBM(float x, float y, float z, int c0, int c1, int c2, uint32_t seed) {
        float w0 = WorleyF1(x, y, z, c0, seed);
        float w1 = WorleyF1(x, y, z, c1, seed ^ 0x9E3779B9u);
        float w2 = WorleyF1(x, y, z, c2, seed ^ 0xB5297A4Du);
        // Weighted sum (classic Schneider-style)
        return (w0 * 0.625f + w1 * 0.25f + w2 * 0.125f);
    }

    CloudParams MakeDefaultCloudParams() {
        CloudParams p = {};

        // Defaults tuned for archviz-friendly cumulus (less smoky, more defined).
        p.density = 4.22f;
        p.absorption = 0.417f;
        p.coverage = 0.117f;
        p.scattering = 0.027f;
        p.steps = 128; // Screenshot says 128
        p.sunIntensity = 3.0f;
        p.cloudTop = 680.0f;
        p.cloudBottom = 240.0f;
        p.windSpeed = 0.0f; // Slider is 0.0

        p.baseScale = 0.00051f;
        p.detailScale = 0.01000f;
        p.coverageScale = 0.00022f;
        p.coverageVariation = 0.25f; // From slider
        p.erosion = 0.65f;
        p.warpStrength = 0.75f;
        p.shapePower = 1.85f;
        p.powderStrength = 0.45f;

        p.shadowSteps = 8;
        p.shadowStepSize = 120.0f;
        p.shadowLod = 2.0f;

        p.maxSteps = 512;
        p.verticalStepMeters = 25.0f;
        p.shadowEvery = 4;
        p.shadowDensityThreshold = 0.05f;

        p.timeSeconds = 0.0f;
        p._pad = { 0, 0, 0 };
        return p;
    }
}

void CloudManager::Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList) {
    if (m_initialized) return;

    m_params = MakeDefaultCloudParams();

    CreateConstantBuffer(device);
    CreateTextures(device, cmdList);

    m_initialized = true;
}

void CloudManager::ResetToDefaults() {
    m_params = MakeDefaultCloudParams();
    if (m_initialized) {
        UpdateConstantBuffer();
    }
}

void CloudManager::Update(float dt) {
    if (!m_initialized) return;
    m_params.timeSeconds += dt;
    UpdateConstantBuffer();
}

void CloudManager::CreateConstantBuffer(ID3D12Device* device) {
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
    
    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc, 
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, 
        IID_PPV_ARGS(&m_constantBuffer)));

    m_constantBuffer->SetName(L"CloudConstantBuffer");
    
    D3D12_RANGE readRange = { 0, 0 };
    ThrowIfFailed(m_constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_cbMappedData)));
}

void CloudManager::UpdateConstantBuffer() {
    if (m_cbMappedData) {
        memcpy(m_cbMappedData, &m_params, sizeof(CloudParams));
    }
}

// Helper for remapping
static float Remap(float val, float minVal, float maxVal, float newMin, float newMax) {
    return newMin + (val - minVal) * (newMax - newMin) / (maxVal - minVal);
}

void CloudManager::CreateTextures(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList) {
    fprintf(stderr, "CloudManager: Generating 1:1 Compliant Noise Textures (Base + Detail)...\n");
    
    // --- 1. Base Shape Texture (128x128x128 RGBA8) ---
    // R: Perlin-Worley (Base Shape)
    // G: Worley FBM (Freq 1)
    // B: Worley FBM (Freq 2)
    // A: Worley FBM (Freq 3)
    {
        const UINT width = 128;
        const UINT height = 128;
        const UINT depth = 128;
        const UINT textureDataSize = width * height * depth * 4;
        std::vector<UINT8> noiseData(textureDataSize);

        const uint32_t worleySeed = 1337u;

        for (int z = 0; z < (int)depth; ++z) {
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
                    for(int i=0; i<5; ++i) {
                        float n = GradientNoiseTiled(u * scale, v * scale, w * scale, (int)scale);
                        perlin += n * amp;
                        maxVal += amp;
                        scale *= 2.0f;
                        amp *= 0.5f;
                    }
                    perlin = (perlin / maxVal) * 0.5f + 0.5f; // [0,1]

                    // Worley FBMs for Base
                    // Enscape frequencies: 4, 8, 16? roughly
                    float wf1 = WorleyFBM(u, v, w, 4, 8, 16, worleySeed);
                    float wf2 = WorleyFBM(u, v, w, 8, 16, 32, worleySeed ^ 0x12345678);
                    float wf3 = WorleyFBM(u, v, w, 16, 32, 64, worleySeed ^ 0x87654321);

                    // Perlin-Worley Remap: 
                    // Remap Perlin using the inverted Worley as the low-bound
                    float worleyBase = wf1; // Use the lowest freq worley for the shape erosion
                    float perlinWorley = Remap(perlin, worleyBase, 1.0f, 0.0f, 1.0f);
                    perlinWorley = (std::max)(0.0f, (std::min)(1.0f, perlinWorley));

                    // Warp Noise (Low Freq Perlin) in Alpha
                    // Use scale 2.0 for warp (must be integer for tiling)
                    float warp = GradientNoiseTiled(u * 2.0f, v * 2.0f + 5.5f, w * 2.0f + 1.2f, 2);
                    warp = warp * 0.5f + 0.5f;

                    const UINT idx = (UINT)((z * (width * height) + y * width + x) * 4);
                    noiseData[idx + 0] = (UINT8)(perlinWorley * 255.0f);
                    noiseData[idx + 1] = (UINT8)((std::max)(0.0f, wf1) * 255.0f); // Worley 1
                    noiseData[idx + 2] = (UINT8)((std::max)(0.0f, wf2) * 255.0f); // Worley 2
                    noiseData[idx + 3] = (UINT8)((std::max)(0.0f, warp) * 255.0f); // Warp (Perlin)
                }
            }
        }

        // Create Resource
        D3D12_RESOURCE_DESC texDesc = {};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
        texDesc.Width = width;
        texDesc.Height = height;
        texDesc.DepthOrArraySize = depth;
        texDesc.MipLevels = 1;
        texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texDesc.SampleDesc.Count = 1;
        texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        D3D12_HEAP_PROPERTIES defaultHeap = {};
        defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

        ThrowIfFailed(device->CreateCommittedResource(
            &defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&m_baseTexture)));
        m_baseTexture->SetName(L"CloudBaseTexture");

        // Upload
        const UINT64 uploadSize = GetRequiredIntermediateSize(m_baseTexture.Get(), 0, 1);
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
        
        D3D12_SUBRESOURCE_DATA srcData = {};
        srcData.pData = noiseData.data();
        srcData.RowPitch = width * 4;
        srcData.SlicePitch = srcData.RowPitch * height;

        UpdateSubresources(cmdList, m_baseTexture.Get(), uploadBuffer.Get(), 0, 0, 1, &srcData);
        
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_baseTexture.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
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

        for (int z = 0; z < (int)depth; ++z) {
            for (int y = 0; y < (int)height; ++y) {
                for (int x = 0; x < (int)width; ++x) {
                    float u = (float)x / (float)width;
                    float v = (float)y / (float)height;
                    float w = (float)z / (float)depth;

                    // Detail frequencies. Since texture is small, we repeat fewer times?
                    // Actually, Detail noise is typically tiled heavily in the shader. 
                    // Standard FBM mix.
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

        D3D12_RESOURCE_DESC texDesc = {};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
        texDesc.Width = width;
        texDesc.Height = height;
        texDesc.DepthOrArraySize = depth;
        texDesc.MipLevels = 1;
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

        const UINT64 uploadSize = GetRequiredIntermediateSize(m_detailTexture.Get(), 0, 1);
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
        
        D3D12_SUBRESOURCE_DATA srcData = {};
        srcData.pData = noiseData.data();
        srcData.RowPitch = width * 4;
        srcData.SlicePitch = srcData.RowPitch * height;

        UpdateSubresources(cmdList, m_detailTexture.Get(), uploadBuffer.Get(), 0, 0, 1, &srcData);
        
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_detailTexture.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);

        m_uploadBuffers.push_back(uploadBuffer);
    }
}
