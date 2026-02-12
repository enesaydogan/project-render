#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "ibl_manager.h"
#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include "d3d12_helpers.h"
#include "dxc_wrapper.h"

// Simple analytic approximation of CIE 1931 color matching functions.
inline float Gaussian(float x, float alpha, float mu, float sigma1, float sigma2) {
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
inline void XYZtoRGB(float x, float y, float z, float& r, float& g, float& b) {
    r = 3.2404542f * x - 1.5371385f * y - 0.4985314f * z;
    g = -0.9692660f * x + 1.8760108f * y + 0.0415560f * z;
    b = 0.0556434f * x - 0.2040259f * y + 1.0572252f * z;
}

bool IBLManager::Initialize(ID3D12Device* device, ID3D12CommandQueue* queue) {
    m_device = device;
    m_queue = queue;
    return true;
}

bool IBLManager::LoadEnvironmentMap(const std::string& path) {
    if (!m_device) return false;

    bool isHDR = false;
    if (path.find(".hdr") != std::string::npos || path.find(".exr") != std::string::npos) {
        isHDR = true;
    }

    Asset::Texture tex = Asset::LoadTextureFromFile(path, isHDR);
    if (!tex.resource) {
        std::cerr << "Failed to load environment map: " << path << std::endl;
        return false;
    }

    std::cout << "Loaded environment map: " << path << " (" << tex.width << "x" << tex.height << ")" << std::endl;

    m_fileTexture = tex;
    m_source = IBLSource::File;
    m_envMap = m_fileTexture;
    
    CreateDescriptor();
    return true;
}

bool IBLManager::InitializeSkyModel(const std::string& datasetPath) {
    m_pragueSkyModel = std::make_unique<PragueSkyModel>();
    try {
        m_pragueSkyModel->initialize(datasetPath);
        std::cout << "PragueSkyModel initialized from " << datasetPath << std::endl;
        m_skyInitialized = true;
        m_skyDirty = true;
        // Default update
        UpdateSkyModel();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize PragueSkyModel: " << e.what() << std::endl;
        m_skyInitialized = false;
        return false;
    }
}

void IBLManager::UpdateSkyModel() {
    if (m_source == IBLSource::PragueSkyModel && m_skyDirty && m_skyInitialized) {
        UpdateTextureFromSkyModel();
        m_envMap = m_proceduralTexture;
        CreateDescriptor();
        m_skyDirty = false;
    }
}

void IBLManager::SetIBLSource(IBLSource source) {
    if (m_source != source) {
        m_source = source;
        if (m_source == IBLSource::File) {
            if (m_fileTexture.resource) {
                m_envMap = m_fileTexture;
                CreateDescriptor();
            } else {
                std::cerr << "Cannot switch to File IBL: no file loaded." << std::endl;
                m_source = IBLSource::PragueSkyModel; // revert
            }
        } else {
             if (m_skyInitialized) {
                 if (m_skyDirty || !m_proceduralTexture.resource) {
                    UpdateTextureFromSkyModel();
                 }
                 m_envMap = m_proceduralTexture;
                 CreateDescriptor();
             } else {
                 std::cerr << "Cannot switch to Sky Model: not initialized." << std::endl;
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
        
        m_device->CreateShaderResourceView(m_envMap.resource.Get(), &srvDesc, m_cpuHandle);
    }
}

// Helper to create a texture resource from data (RGBA32Float)
static bool CreateTexFromData(ID3D12Device* device, ID3D12CommandQueue* queue, UINT width, UINT height, const std::vector<float>& data, Asset::Texture& outTex) {
    if (data.size() != width * height * 4) return false;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Alignment = 0;
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R32G32B32_FLOAT; // Note: Use R32G32B32 for IBL if supported, but usually A32 is better aligned.
    // Actually source is 4 floats. R32G32B32A32_FLOAT
    texDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ComPtr<ID3D12Resource> texture;
    HRESULT hr = device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture));
    if (FAILED(hr)) {
        std::cerr << "CreateTexFromData: Failed to create texture resource. HR=0x" << std::hex << hr << std::dec << std::endl;
        return false;
    }

    // Calculate footprint
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT numRows = 0;
    UINT64 rowSizeInBytes = 0;
    UINT64 totalBytes = 0;
    device->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, &numRows, &rowSizeInBytes, &totalBytes);

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
    hr = device->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer));
    if (FAILED(hr)) {
        std::cerr << "CreateTexFromData: Failed to create upload buffer. HR=0x" << std::hex << hr << std::dec << std::endl;
        return false;
    }

    // Map and Copy
    BYTE* pData = nullptr;
    // Map with empty range reading
    D3D12_RANGE readRange = {0, 0};
    hr = uploadBuffer->Map(0, &readRange, (void**)&pData);
    if (FAILED(hr)) return false;

    // Source Row Pitch is usually packed: width * 16 bytes
    UINT srcRowPitch = width * 16;
    
    for (UINT y = 0; y < height; ++y) {
        BYTE* destRow = pData + footprint.Offset + y * footprint.Footprint.RowPitch;
        const BYTE* srcRow = (const BYTE*)data.data() + y * srcRowPitch;
        memcpy(destRow, srcRow, srcRowPitch);
    }
    
    uploadBuffer->Unmap(0, nullptr);

    // Copy Command
    ComPtr<ID3D12CommandAllocator> cmdAlloc;
    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAlloc));
    ComPtr<ID3D12GraphicsCommandList> cmdList;
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc.Get(), nullptr, IID_PPV_ARGS(&cmdList));

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
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &barrier);

    cmdList->Close();
    ID3D12CommandList* lists[] = { cmdList.Get() };
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
    
    std::cout << "CreateTexFromData: Texture uploaded successfully. " << width << "x" << height << std::endl;

    return true;
}

void IBLManager::UpdateTextureFromSkyModel() {
    if (!m_skyInitialized) return;

    const UINT width = 256*2;
    const UINT height = 128*2;
    std::vector<float> pixels(width * height * 4);

    // Altitude should be in meters [0, 15000]. Origin is at sea level.
    PragueSkyModel::Vector3 viewpoint = {0.0, 0.0, (double)m_altitude};
    PragueSkyModel::Vector3 sunDir = {
        std::cos(m_solarAzimuth) * std::cos(m_solarElevation),
        std::sin(m_solarAzimuth) * std::cos(m_solarElevation),
        std::sin(m_solarElevation)
    };

    const double PI = 3.14159265358979323846;
    const float startLambda = 380.0f;
    const float endLambda = 780.0f;
    const float stepLambda = 20.0f; 

    // Simple loop (can be parallelized)
    for (int y = 0; y < (int)height; ++y) {
        for (int x = 0; x < (int)width; ++x) {
            float u = (float)x / (float)width;
            float v = (float)y / (float)height;
            
            float theta = v * (float)PI; 
            float phi = u * 2.0f * (float)PI; 

            // Y-up coordinate system to match skybox.hlsl usually
            // skybox.hlsl: uv.y = acos(dir.y) / PI  => y = cos(theta)
            // skybox.hlsl: uv.x = atan2(dir.x, dir.z) ...
            
            double dy = std::cos(theta);
            double sinTheta = std::sin(theta);
            // Inverted dx and dz to match the shader's atan2(x, z) / 2PI + 0.5 convention
            // This aligns the texture's sun hotspot with the analytic sun disc
            double dx = sinTheta * -std::sin(phi); 
            double dz = sinTheta * -std::cos(phi);
            
            // Texture is Y-up, but PragueSkyModel expects Z-up.
            // We map our Y (up) to Model Z (up).
            // Our Z (forward) to Model Y.
            // Our X (right) to Model X.
            PragueSkyModel::Vector3 viewDir = {dx, dz, dy};

            if (dy < -0.01) {
                // Ground / Below horizon - Gray
                pixels[(y * width + x) * 4 + 0] = 0.2f;
                pixels[(y * width + x) * 4 + 1] = 0.2f;
                pixels[(y * width + x) * 4 + 2] = 0.2f;
                pixels[(y * width + x) * 4 + 3] = 1.0f;
                continue;
            }

            auto params = m_pragueSkyModel->computeParameters(
                viewpoint, 
                viewDir, 
                m_solarElevation, 
                m_solarAzimuth, 
                m_visibility, 
                m_albedo
            );
            
            // Custom Sun Disk logic
            // Z-up dot product. viewDir and sunDir are both Z-up.
            // sunDir computed at start of function... wait, sunDir depends on solarAzimuth/Elevation.
            float X = 0, Y = 0, Z = 0;
            
            // Integration
            for (float l = startLambda; l <= endLambda; l += stepLambda) {
                double rad = m_pragueSkyModel->skyRadiance(params, l);
                
                // Scale sky model output
                float val = (float)rad * m_skyIntensity * 0.05f; 
                
                X += val * cieX(l) * stepLambda;
                Y += val * cieY(l) * stepLambda;
                Z += val * cieZ(l) * stepLambda;
            }
            
            // Normalize approximate luminance (hacky exposure)
            // If X/Y/Z are too huge, we might need a factor.
            // Standard sky usually ~few thousands. 
            // Our shader tone mapper takes generic units, but let's apply a small scale to bring it to ~1.0 range if needed.
            // Let's try raw first.

            float r, g, b;
            XYZtoRGB(X, Y, Z, r, g, b);

            // Debug center pixel
            if (x == width/2 && y == height/2) {
                 std::cout << "[DEBUG] Center RGB: " << r << ", " << g << ", " << b 
                           << " (Unscaled X:" << X << " Y:" << Y << ")" << std::endl;
            }

            pixels[(y * width + x) * 4 + 0] = std::max(0.0f, r);
            pixels[(y * width + x) * 4 + 1] = std::max(0.0f, g);
            pixels[(y * width + x) * 4 + 2] = std::max(0.0f, b);
            pixels[(y * width + x) * 4 + 3] = 1.0f;
        }
    }

    CreateTexFromData(m_device.Get(), m_queue.Get(), width, height, pixels, m_proceduralTexture);
}

DirectX::XMFLOAT3 IBLManager::GetSunColor() const {
    if (!m_skyInitialized || !m_pragueSkyModel) {
        return { 1.0f, 1.0f, 1.0f };
    }

    PragueSkyModel::Vector3 viewpoint = {0.0, 0.0, (double)m_altitude};
    PragueSkyModel::Vector3 sunDir = {
        std::cos(m_solarAzimuth) * std::cos(m_solarElevation),
        std::sin(m_solarAzimuth) * std::cos(m_solarElevation),
        std::sin(m_solarElevation)
    };

    auto params = m_pragueSkyModel->computeParameters(
        viewpoint,
        sunDir,
        m_solarElevation,
        m_solarAzimuth,
        m_visibility,
        m_albedo
    );

    const float startLambda = 380.0f;
    const float endLambda = 780.0f;
    const float stepLambda = 20.0f;

    float X = 0, Y = 0, Z = 0;

    for (float l = startLambda; l <= endLambda; l += stepLambda) {
        double rad = m_pragueSkyModel->sunRadiance(params, l);
        // Scale sun radiance: Physical values are huge (~1e7 to 1e9), so we apply a normalization factor
        // to bring it into a usable HDR range.
        // The effective intensity is controlled by g_cameraData.lightColor.w (Sun Intensity key in UI).
        float val = (float)rad * 0.000005f;
        X += val * cieX(l) * stepLambda;
        Y += val * cieY(l) * stepLambda;
        Z += val * cieZ(l) * stepLambda;
    }

    float r, g, b;
    XYZtoRGB(X, Y, Z, r, g, b);

    return { std::max(0.0f, r), std::max(0.0f, g), std::max(0.0f, b) };
}
